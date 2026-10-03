/*
 * TextureView.cpp -- see TextureView.h.
 */

#include "TextureView.h"

#include <algorithm>
#include <chrono>
#include <memory>

#include <QElapsedTimer>
#include <QFileInfo>
#include <QSettings>

#include <wx/aui/auibar.h>
#include <wx/clipbrd.h>
#include <wx/filedlg.h>
#include <wx/numformatter.h>
#include <wx/statline.h>
#include <wx/stdpaths.h>

#include "modelcanvas.h"
#include "modelviewer.h"
#include "UiArt.h"
#include "UiControls.h"
#include "UiStyle.h"
#include "UnityAssetAccess.h"
#include "util.h"
#include "video.h"
#include "logger/Logger.h"

namespace
{
  enum
  {
    ID_TV_ALPHA_ON = wxID_HIGHEST + 6300,
    ID_TV_ALPHA_OFF,
    ID_TV_ALPHA_ONLY,
  };

  const wxString Dot(L" \u00b7 ");
  const wxString Times(L" \u00d7 ");
  const wxString Ellipsis(L"\u2026");

  wxString wx(const QString & s) { return wxString(s.toStdWString()); }
  QString qs(const wxString & s) { return QString::fromWCharArray(s.wc_str()); }

  wxString thousands(long long n) { return wxNumberFormatter::ToString((long)n); }

  wxString sizeText(qint64 bytes)
  {
    if (bytes < 1024 * 1024)
      return wxString::Format(_("%s KB"), thousands((bytes + 1023) / 1024));
    return wxString::Format(_("%.1f MB"), bytes / (1024.0 * 1024.0));
  }

  bool sameTexture(const TextureEntry & a, const TextureEntry & b)
  {
    return a.unnamed == b.unnamed && a.fileDataId == b.fileDataId && (a.unnamed || a.path == b.path);
  }
}

qint64 TextureView::nowNs()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

TextureView::TextureView(wxWindow * parent, ModelViewer * viewer)
  : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxNO_BORDER, wxT("textureView")),
    m_viewer(viewer)
{
  Hide();
  buildLayout();
  loadSettings();
  Bind(wxEVT_CHAR_HOOK, &TextureView::OnCharHook, this);
  Bind(wxEVT_IDLE, &TextureView::OnIdle, this);
}

TextureView::~TextureView()
{
  shutdown();
}

void TextureView::shutdown()
{
  m_shutdown = true;
  m_decodePending = false;
}

// ------------------------------------------------------------------------------------------- layout

void TextureView::buildLayout()
{
  const int xs = FromDIP(UiStyle::XS), s = FromDIP(UiStyle::S), m = FromDIP(UiStyle::M);
  UiStyle::applyPanel(this);

  // The view controls, above the texture: how it is drawn, nothing else (it is sized by itself). Alpha is
  // one segmented control (UiToolBarArt), the background a native choice beside it.
  m_tools = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxAUI_TB_TEXT | wxAUI_TB_HORZ_TEXT | wxAUI_TB_PLAIN_BACKGROUND | wxAUI_TB_NO_AUTORESIZE);
  m_tools->SetName(wxT("textureTools"));
  m_tools->SetArtProvider(new UiToolBarArt(true));
  m_tools->SetBackgroundColour(UiStyle::palette().panelBackground);
  m_tools->SetToolBorderPadding(0);
  m_tools->SetToolPacking(0);
  m_tools->SetToolSeparation(FromDIP(25));
  m_tools->SetMargins(m, s, s, s);
  m_tools->AddLabel(wxID_ANY, _("Alpha"));
  m_tools->AddTool(ID_TV_ALPHA_ON, _("On"), wxNullBitmap, _("Alpha on: the texture over the background, as RGBA (A)"),
                   wxITEM_RADIO);
  m_tools->AddTool(ID_TV_ALPHA_OFF, _("Off"), wxNullBitmap,
                   _("Alpha off: RGB only, every pixel opaque, to see colour under transparency (A)"), wxITEM_RADIO);
  m_tools->AddTool(ID_TV_ALPHA_ONLY, _("Only"), wxNullBitmap,
                   _("Alpha only: the alpha channel as grey, white = opaque (Shift+A)"), wxITEM_RADIO);
  m_tools->AddSeparator();
  m_tools->AddLabel(wxID_ANY, _("Background"));
  m_background = new wxChoice(m_tools, wxID_ANY);
  UiSetAccessibleName(m_background, _("Background"));   // its label is drawn by the toolbar, not a window
  m_background->SetName(wxT("textureBackground"));
  m_background->Append(_("Checkerboard"));
  m_background->Append(_("Dark"));
  m_background->Append(_("Light"));
  m_background->SetSelection(0);
  m_tools->AddControl(m_background);
  m_tools->Realize();

  m_preview = new TexturePreview(this);
  m_preview->setMessage(_("Select a texture in Browse"));
  m_preview->onImagePainted = [this](double ms) {
    // The pick's first picture only: a sharper level painted later (a larger window) is not the pick's.
    Timing & t = m_timing;
    if (t.selected == 0 || t.shown != 0)
      return;
    t.paintMs = ms;
    t.shown = nowNs();
    LOG_INFO << "[texview] timing" << (m_current.entry.unnamed ? QString::number(m_current.entry.fileDataId) : m_current.entry.path)
             << (t.cached ? "from the cache," : "") << "level" << t.level << "pick->read"
             << QString::number((t.decodeStart - t.selected) / 1e6, 'f', 1) << "read" << QString::number(t.readMs, 'f', 1)
             << "check" << QString::number(t.checkMs, 'f', 2) << "decode" << QString::number(t.decodeMs, 'f', 1)
             << "image" << QString::number(t.imageMs, 'f', 1) << "paint" << QString::number(t.paintMs, 'f', 1)
             << "pick->shown" << QString::number((t.shown - t.selected) / 1e6, 'f', 1) << "ms; cache" << m_cache.size()
             << "textures," << (m_cacheBytes >> 20) << "MB";
  };
  m_preview->onNeedsDetail = [this](int side) { sharpen(side); };

  // What the file is: name, then its folder and its facts, each with its (secondary) copy button.
  m_name = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                            wxST_ELLIPSIZE_MIDDLE | wxST_NO_AUTORESIZE);
  m_name->SetName(wxT("textureName"));
  m_name->SetFont(UiStyle::font(UiStyle::Type::Title));
  wxBoxSizer * pathRow = new wxBoxSizer(wxHORIZONTAL);
  m_path = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                            wxST_ELLIPSIZE_MIDDLE | wxST_NO_AUTORESIZE);
  m_path->SetName(wxT("texturePath"));
  UiStyle::setRole(m_path, UiStyle::Role::SecondaryText);
  m_copyPath = new UiButton(this, wxID_ANY, _("Copy path"), UiButton::Kind::Subtle, UiIcon::Copy, wxBU_EXACTFIT | wxBU_LEFT);
  m_copyPath->SetName(wxT("textureCopyPath"));
  pathRow->Add(m_path, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, s);
  pathRow->Add(m_copyPath, 0, wxALIGN_CENTER_VERTICAL);
  wxBoxSizer * factsRow = new wxBoxSizer(wxHORIZONTAL);
  m_facts = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                             wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
  m_facts->SetName(wxT("textureFacts"));
  m_copyId = new UiButton(this, wxID_ANY, _("Copy FileDataID"), UiButton::Kind::Subtle, UiIcon::Copy, wxBU_EXACTFIT | wxBU_LEFT);
  m_copyId->SetName(wxT("textureCopyId"));
  // The two copy buttons, one above the other, as wide as each other.
  const int copyWidth = (std::max)(m_copyPath->GetBestSize().x, m_copyId->GetBestSize().x);
  m_copyPath->SetMinSize(wxSize(copyWidth, -1));
  m_copyId->SetMinSize(wxSize(copyWidth, -1));
  factsRow->Add(m_facts, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, s);
  factsRow->Add(m_copyId, 0, wxALIGN_CENTER_VERTICAL);

  // The exports, under what they save: the PNG first as the primary action (the usual one), the
  // original file beside it as a secondary one, as wide as each other.
  m_exportPng = new UiButton(this, wxID_ANY, _("Export PNG"), UiButton::Kind::Primary, UiIcon::Export);
  m_exportPng->SetName(wxT("textureExportPng"));
  m_exportPng->SetToolTip(_("Save the texture as a PNG at its full resolution, with its alpha (Ctrl+S)"));
  m_exportBlp = new UiButton(this, wxID_ANY, _("Export BLP"), UiButton::Kind::Secondary, UiIcon::Export);
  m_exportBlp->SetName(wxT("textureExportBlp"));
  m_exportBlp->SetToolTip(_("Save the original file from the game client, byte for byte (Ctrl+Shift+S)"));
  // (Wide enough for the original-file label a file that is not a BLP gets: showInfo.)
  m_exportBlp->SetLabel(_("Export original file"));
  const int widest = (std::max)(m_exportPng->GetBestSize().x, m_exportBlp->GetBestSize().x);
  m_exportBlp->SetLabel(_("Export BLP"));
  const wxSize exportSize((std::max)(FromDIP(132), widest), FromDIP(UiStyle::ButtonHeight));
  m_exportPng->SetMinSize(exportSize);
  m_exportBlp->SetMinSize(exportSize);
  wxBoxSizer * exportRow = new wxBoxSizer(wxHORIZONTAL);
  exportRow->Add(m_exportPng, 0, wxRIGHT, s);
  exportRow->Add(m_exportBlp, 0);

  wxBoxSizer * info = new wxBoxSizer(wxVERTICAL);
  info->Add(m_name, 0, wxEXPAND | wxBOTTOM, xs);
  info->Add(pathRow, 0, wxEXPAND | wxBOTTOM, xs);
  info->Add(factsRow, 0, wxEXPAND | wxBOTTOM, m);
  info->Add(exportRow, 0);

  wxBoxSizer * sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_tools, 0, wxEXPAND);
  sizer->Add(m_preview, 1, wxEXPAND);
  sizer->Add(UiStyle::separator(this), 0, wxEXPAND);
  sizer->Add(info, 0, wxEXPAND | wxALL, m);
  SetSizer(sizer);

  m_background->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
    m_preview->setBackground((TexturePreview::Background)m_background->GetSelection());
    saveSettings();
  });
  m_tools->Bind(wxEVT_TOOL, [this](wxCommandEvent & e) {
    switch (e.GetId())
    {
      case ID_TV_ALPHA_ON: setAlphaView(TexturePreview::AlphaView::On); break;
      case ID_TV_ALPHA_OFF: setAlphaView(TexturePreview::AlphaView::Off); break;
      case ID_TV_ALPHA_ONLY: setAlphaView(TexturePreview::AlphaView::Only); break;
      default: e.Skip(); break;
    }
  });
  m_copyPath->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { copyPath(); });
  m_copyId->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { copyFileDataId(); });
  m_exportPng->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { exportPngInteractive(); });
  m_exportBlp->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { exportBlpInteractive(); });

  showInfo();
}

void TextureView::loadSettings()
{
  m_loadingSettings = true;
  QSettings config(qs(cfgPath), QSettings::IniFormat);
  const int background = config.value("TextureViewer/Background", 0).toInt();
  m_background->SetSelection(std::max(0, std::min(2, background)));
  m_preview->setBackground((TexturePreview::Background)m_background->GetSelection());

  const int alpha = std::max(0, std::min(2, config.value("TextureViewer/AlphaView", 0).toInt()));
  setAlphaView((TexturePreview::AlphaView)alpha);
  m_loadingSettings = false;

  // What earlier versions kept and nothing reads now goes: the zoom (the texture is sized by itself)
  // and the separate window's placement and list width.
  // (The window's placement was a group: Window/x, Window/y and so on.)
  const QStringList kept = config.allKeys();
  bool removed = false;
  for (const QString key : { "TextureViewer/Zoom", "TextureViewer/Window", "TextureViewer/Geometry", "TextureViewer/BrowserWidth" })
    for (const QString & k : kept)
      if (k == key || k.startsWith(key + "/"))
      {
        config.remove(key);   // the key and everything under it
        removed = true;
        break;
      }
  if (removed)
    config.sync();
}

void TextureView::saveSettings()
{
  if (m_shutdown || m_loadingSettings)
    return;
  QSettings config(qs(cfgPath), QSettings::IniFormat);
  config.setValue("TextureViewer/Background", m_background->GetSelection());
  config.setValue("TextureViewer/AlphaView", (int)m_preview->alphaView());
  config.sync();
}

bool TextureView::clientReady() const
{
  return !m_shutdown && UnityAssetAccess::hasActiveClient() && !UnityAssetAccess::isClientLoading();
}

bool TextureView::decoderReady() const
{
  return !m_shutdown && m_viewer && m_viewer->canvas && video.render && m_viewer->canvas->init;
}

void TextureView::status(const wxString & text)
{
  if (m_viewer && m_viewer->GetStatusBar())
    m_viewer->SetStatusText(text, 0);
}

// ---------------------------------------------------------------------------------------- showing

bool TextureView::isCurrent(const TextureEntry & entry) const
{
  return sameTexture(entry, m_current.entry);
}

TextureView::Selection TextureView::selection() const
{
  const Current & c = m_current;
  if (m_decodePending)
    return Selection::Loading;
  if (c.decoded)
    return Selection::Shown;
  if (c.valid)
    return Selection::Refused;
  if (!c.error.isEmpty())
    return Selection::Failed;
  return c.entry.fileDataId > 0 || !c.entry.path.isEmpty() ? Selection::Loading : Selection::None;
}

void TextureView::show(const TextureEntry & entry, bool lookup)
{
  if (m_shutdown)
    return;
  // The texture already selected -- being read, or read and not refused for now only -- picked again
  // (a click on its row, Enter, the row selected again by a search): nothing to redo. A look-up row is
  // the file it found.
  const bool same = lookup ? (m_current.isLookup && m_current.entry.fileDataId == entry.fileDataId)
                           : (!m_current.isLookup && isCurrent(entry));
  if (same && (m_decodePending || (m_current.valid && !m_current.retry)))
  {
    lookupFound();
    showInfo();
    return;
  }
  m_timing = Timing();
  m_timing.selected = nowNs();

  // Decoded before: at once.
  if (const Current * hit = cacheFind(cacheKey(entry, lookup)))
  {
    m_decodePending = false;
    m_current = *hit;
    lookupFound();
    m_timing.cached = true;
    m_timing.decodeStart = m_timing.selected;
    m_timing.level = m_current.imageLevel;
    if (m_current.decoded)
      m_preview->setImage(m_current.image, QSize(m_current.width, m_current.height));
    else
      showReason();
    // Kept from a smaller level, and now seen with Alpha Off: level 0 for that view.
    if (m_current.decoded && m_current.imageLevel != 0 && m_preview->alphaView() == TexturePreview::AlphaView::Off)
      sharpen(0);
    showInfo();
    selectionChanged();
    return;
  }

  // The selection now: its name, folder and FileDataID; read and decoded once the queued input and the
  // repaint are done. The picture of the texture before stays until this one replaces it.
  m_current = Current();
  m_current.entry = entry;
  m_current.isLookup = lookup;
  m_pending = entry;
  m_pendingLookup = lookup;
  m_decodePending = true;
  if (!m_preview->hasImage())
  {
    const wxString name = entry.unnamed ? wxString::Format(_("FileDataID %d"), entry.fileDataId) : wx(entry.name());
    m_preview->setMessage(wxString::Format(_("Reading %s"), name) + Ellipsis);
  }
  showInfo();
  selectionChanged();
  schedule();
}

void TextureView::showNow(const TextureEntry & entry, bool lookup)
{
  show(entry, lookup);
  if (m_decodePending)
  {
    m_decodePending = false;
    decodeNow();
    selectionChanged();
  }
}

void TextureView::schedule()
{
  // Out of Textures mode it waits for the next entry (entered).
  if (!m_active || m_shutdown)
    return;
  // At the next idle event: wx sends one only when the message queue is empty, so after the input
  // already queued (a later pick replaces this one) and after the repaint of the row and the facts.
  // Not CallAfter: wx runs those before it takes the next message, so every row a burst of clicks
  // passed would be read and decoded. No timer: a tick adds 10-16 ms and arrives behind nothing in
  // particular.
  wxWakeUpIdle();
}

void TextureView::OnIdle(wxIdleEvent & event)
{
  event.Skip();
  if (!m_decodePending || !m_active || m_shutdown || m_keysRepeating)
    return;
  m_decodePending = false;
  decodeNow();
  // What depends on the read -- the facts in the status bar, a looked-up file's name -- follows it.
  selectionChanged();
}

void TextureView::setKeysRepeating(bool repeating)
{
  if (repeating == m_keysRepeating)
    return;
  m_keysRepeating = repeating;
  // Let go: the row the key stopped on decodes now.
  if (!repeating && m_decodePending)
    schedule();
}

void TextureView::lookupFound()
{
  // A look-up row made again by a search, picked: it names the file it found (TextureBrowse) -- the
  // first pick read the file and named it; this one is served without reading.
  const Current & c = m_current;
  if (c.isLookup && c.valid && !c.entry.unnamed && !c.entry.path.isEmpty() && onLookupResolved)
    onLookupResolved(c.entry.fileDataId, c.entry.path);
}

void TextureView::showReason()
{
  const Current & c = m_current;
  const wxString originalStill = isBlp(c) ? _("Export BLP still saves the original file as it is.")
                                          : _("Export original file still saves the file as it is.");
  if (!c.info.supported)
    m_preview->setMessage(c.info.headerRead && c.info.magic != "BLP2" ? _("Not a BLP texture") : _("Preview not available"),
                          wx(c.info.reason), originalStill);
  else
    m_preview->setMessage(_("Preview not available"), wx(TextureDecode::decoderProblem(decoderReady(), c.info)), originalStill);
}

void TextureView::decodeNow()
{
  if (!clientReady())
  {
    emptyState();
    return;
  }

  Current c;
  c.isLookup = m_pendingLookup;
  c.entry = m_pending;
  const QString key = cacheKey(m_pending, m_pendingLookup);
  const wxString shownName = c.entry.unnamed ? wxString::Format(_("FileDataID %d"), c.entry.fileDataId) : wx(c.entry.name());

  m_timing.decodeStart = nowNs();
  QElapsedTimer stage;
  stage.start();
  const TextureDecode::Source source = c.isLookup || c.entry.unnamed ? TextureDecode::readId(c.entry.fileDataId)
                                                                     : TextureDecode::read(c.entry);
  m_timing.checkMs = source.checkMs;
  m_timing.readMs = stage.nsecsElapsed() / 1e6 - source.checkMs;
  if (!source.ok)
  {
    // Not kept: a file in use or not readable now may be readable later.
    c.error = source.error;
    m_current = c;
    m_preview->setMessage(_("Cannot read this file"), wx(source.error));
    showInfo();
    return;
  }
  c.valid = true;
  c.info = source.info;
  c.bytes = source.bytes.size();

  if (c.isLookup && source.file)
  {
    const QString found = source.file->fullname();
    // A named file that is not a .blp texture (a model, say) is shown under its own name; the index's
    // name for a file it has no name for ("File%08x.unk") is not a name.
    if (!(found.startsWith("File") && found.endsWith(".unk")))
    {
      c.entry.path = found;
      c.entry.nameStart = found.lastIndexOf('/') + 1;
      c.entry.unnamed = false;
    }
    if (onLookupResolved)
      onLookupResolved(c.entry.fileDataId, found);
  }

  if (!c.info.supported)
  {
    // What the header says does not change: kept.
    m_current = c;
    showReason();
    cachePut(key, c);
    showInfo();
    return;
  }
  if (!TextureDecode::decoderProblem(decoderReady(), c.info).isEmpty())
  {
    // Refused for now only (the decoder not running yet): not kept, and picking it again tries again.
    c.retry = true;
    m_current = c;
    showReason();
    showInfo();
    return;
  }

  // The level to preview: the smallest of the texture's own levels still at least the size shown, when
  // the area shows it at half its size or less (it is only ever reduced). Measured on a 4096 texture
  // shown at 1086: reading back level 0 takes about 220 ms, level 1 (2048) about 55.
  // The Alpha Off view shows the colour under transparency, which smaller levels need not keep: level 0.
  const QSize shown = m_preview->displaySizeFor(QSize((int)c.info.width, (int)c.info.height));
  const int side = std::max(shown.width(), shown.height());
  const bool offView = m_preview->alphaView() == TexturePreview::AlphaView::Off;
  const int minSide = !offView && side * 2 <= (int)std::max(c.info.width, c.info.height) ? side : 0;

  // A slow one (measured: a 4096 texture 150-550 ms; up to 2048 under 100): a note in the corner, painted
  // now, over the picture before.
  const bool slow = (qint64)c.info.width * c.info.height > 2048 * 2048;
  std::unique_ptr<wxBusyCursor> busy;
  if (slow)
  {
    busy.reset(new wxBusyCursor);
    if (m_preview->hasImage())
    {
      m_preview->setNote(wxString::Format(_("Decoding %s"), shownName) + Ellipsis);
      m_preview->Update();
    }
  }
  const TextureDecode::Pixels px = TextureDecode::decode(source.file, c.info, minSide);
  if (!px.ok)
  {
    c.error = px.error;
    c.valid = true;
    m_current = c;
    m_preview->setMessage(_("Could not decode this texture"), wx(px.error),
                          isBlp(c) ? _("Export BLP still saves the original file as it is.")
                                   : _("Export original file still saves the file as it is."));
    showInfo();
    return;
  }
  m_timing.decodeMs = px.ms;
  m_timing.loadMs = px.loadMs;
  stage.restart();
  c.decoded = true;
  c.image = px.image;
  c.imageLevel = px.level;
  c.width = px.width;
  c.height = px.height;
  c.levels = px.levels;
  c.internalFormat = px.internalFormat;
  // The alpha facts are those of level 0: from its pixels, or -- previewed from a smaller level, whose
  // filtering would turn cut-out edges into partial alpha -- from level 0's own bytes.
  if (px.level == 0 || !TextureDecode::alphaOfLevel0(source.bytes, c.info, c.internalFormat, c.alpha))
  {
    if (px.level != 0)
    {
      const TextureDecode::Pixels full = TextureDecode::decode(source.file, c.info, 0);
      if (full.ok)
      {
        c.image = full.image;
        c.imageLevel = 0;
      }
    }
    c.alpha = TextureDecode::alphaOf(c.image);
  }
  m_current = c;
  LOG_INFO << "[texview] decoded" << (c.entry.unnamed ? QString::number(c.entry.fileDataId) : c.entry.path) << px.width
           << "x" << px.height << TextureDecode::decodedFormatName(px.internalFormat, c.info) << px.levels << "levels, level"
           << c.imageLevel << "shown, in" << QString::number(px.ms, 'f', 1) << "ms" << c.info.mipDamage;
  m_preview->setImage(c.image, QSize(c.width, c.height));
  m_timing.imageMs = stage.nsecsElapsed() / 1e6;
  m_timing.level = c.imageLevel;
  cachePut(key, c);
  showInfo();
}

void TextureView::sharpen(int side)
{
  Current & c = m_current;
  if (m_shutdown || !m_active || !c.decoded || c.imageLevel == 0 || !clientReady() || !decoderReady())
    return;
  const TextureDecode::Source source = c.isLookup || c.entry.unnamed ? TextureDecode::readId(c.entry.fileDataId)
                                                                     : TextureDecode::read(c.entry);
  if (!source.ok || !source.info.supported)
    return;
  const bool offView = m_preview->alphaView() == TexturePreview::AlphaView::Off;
  const int minSide = side > 0 && !offView && side * 2 <= (int)std::max(source.info.width, source.info.height) ? side : 0;
  wxBusyCursor busy;
  const TextureDecode::Pixels px = TextureDecode::decode(source.file, source.info, minSide);
  if (!px.ok || px.level >= c.imageLevel)
    return;
  c.image = px.image;
  c.imageLevel = px.level;
  // Before the pick's first picture (a cached smaller level seen with Alpha Off): the pick's level is
  // this one, read and decoded.
  if (m_timing.shown == 0)
  {
    m_timing.level = c.imageLevel;
    m_timing.cached = false;
  }
  m_preview->setImage(c.image, QSize(c.width, c.height));
  cachePut(cacheKey(c.entry, c.isLookup), c);
}

void TextureView::emptyState()
{
  if (UnityAssetAccess::isClientLoading())
    m_preview->setMessage(_("Loading the game client") + Ellipsis);
  else if (!UnityAssetAccess::hasActiveClient())
    m_preview->setMessage(_("No game client loaded"), wxEmptyString,
                          _("File > Load World of Warcraft, or click Textures, loads one."));
  else
    m_preview->setMessage(_("Select a texture in Browse"));
}

void TextureView::entered()
{
  m_active = true;
  if (selection() == Selection::None)
    emptyState();
  else if (m_decodePending)
    schedule();
  showInfo();
}

void TextureView::left()
{
  // A decode on its way waits for the next entry; what the preview derives from the texture is freed
  // (the texture itself stays, so coming back shows it at once).
  m_active = false;
  m_keysRepeating = false;
  m_preview->setNote(wxEmptyString);
  m_preview->releaseCaches();
}

void TextureView::clientLoadStarting()
{
  m_decodePending = false;
  m_current = Current();
  m_pending = TextureEntry();
  cacheClear();
  m_preview->setMessage(_("Loading the game client") + Ellipsis);
  showInfo();
}

void TextureView::clientLoaded()
{
  if (selection() == Selection::None)
    emptyState();
  showInfo();
}

void TextureView::selectionChanged()
{
  if (m_viewer)
    m_viewer->TextureSelectionChanged();
}

// ------------------------------------------------------------------------------------------ cache

// What the cache may hold, by the decoded pictures' bytes (4 per pixel). Measured with a client loaded:
// the viewer's private memory runs at 2.3-2.9 GB, so 128 MB is about 5% of it, and a full cache grew the
// process by 141 MB. It holds 128 pictures of 512 x 512, 32 of 1024 x 1024, or 8 of the 4096 textures
// at the level the preview shows them; a texture seen again within that comes back in 5-34 ms instead
// of being read and decoded anew (20-225 ms).
const qint64 TextureView::CacheBudget = 128LL * 1024 * 1024;

QString TextureView::cacheKey(const TextureEntry & entry, bool lookup)
{
  // By FileDataID where there is one: a look-up row and the named file it finds are one file.
  if (entry.fileDataId > 0)
    return QString("id:%1").arg(entry.fileDataId);
  return QString("path:") + entry.path.toLower() + (lookup ? QString(":lookup") : QString());
}

const TextureView::Current * TextureView::cacheFind(const QString & key)
{
  for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
    if (it->key == key)
    {
      m_cache.splice(m_cache.begin(), m_cache, it);   // most recently used first
      return &m_cache.front().value;
    }
  return nullptr;
}

void TextureView::cachePut(const QString & key, const Current & c)
{
  for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
    if (it->key == key)
    {
      m_cacheBytes -= it->bytes;
      m_cache.erase(it);
      break;
    }
  Cached item;
  item.key = key;
  item.value = c;
  item.bytes = c.image.isNull() ? 0 : (qint64)c.image.sizeInBytes();
  // One larger than the whole budget is not kept (it would empty the cache for itself).
  if (item.bytes > CacheBudget)
    return;
  m_cacheBytes += item.bytes;
  m_cache.push_front(item);
  while (m_cacheBytes > CacheBudget && m_cache.size() > 1)
  {
    m_cacheBytes -= m_cache.back().bytes;
    m_cache.pop_back();
  }
}

void TextureView::cacheClear()
{
  m_cache.clear();
  m_cacheBytes = 0;
}

// ------------------------------------------------------------------------------------------- facts

wxString TextureView::displayName() const
{
  const TextureEntry & e = m_current.entry;
  if (e.unnamed || e.path.isEmpty())
    return e.fileDataId > 0 ? wxString::Format(_("FileDataID %d"), e.fileDataId) : wxString();
  return wx(e.name());
}

wxString TextureView::displayPath() const
{
  const TextureEntry & e = m_current.entry;
  return e.unnamed ? wxString() : wx(e.path);
}

wxString TextureView::statusFacts() const
{
  const Current & c = m_current;
  if (!c.decoded)
    return wxString();
  wxString text = wxString::Format(wxT("%d"), c.width) + Times + wxString::Format(wxT("%d"), c.height) +
                  Dot + wx(TextureDecode::decodedFormatName(c.internalFormat, c.info));
  if (!c.alpha.present)
    text += Dot + _("Alpha: No");
  else
    text += Dot + (c.alpha.partial > 0 ? _("Alpha: Yes (partial)") : _("Alpha: Yes (cut-out)"));
  return text;
}

wxString TextureView::factsLine() const
{
  const Current & c = m_current;
  if (m_decodePending)
    return _("Reading") + Ellipsis;
  if (!c.valid)
    return c.error.isEmpty() ? wxString() : _("Not read");
  wxArrayString parts;
  if (c.decoded)
  {
    parts.Add(wxString::Format(wxT("%d"), c.width) + Times + wxString::Format(wxT("%d"), c.height));
    parts.Add(wx(TextureDecode::decodedFormatName(c.internalFormat, c.info)));
    wxString levels = c.levels == 1 ? _("1 mip level") : wxString::Format(_("%d mip levels"), c.levels);
    // The facts are level 0's (the picture may be a smaller level: see decodeNow); a damaged smaller
    // level is said, not hidden.
    if (!c.info.mipDamage.isEmpty())
      levels += wxT(" ") + wxString::Format(_("(level %d damaged in the file)"), c.info.intactLevels);
    parts.Add(levels);
    if (!c.alpha.present)
      parts.Add(_("Alpha: No"));
    else
      parts.Add(c.alpha.partial > 0 ? _("Alpha: Yes (partial)") : _("Alpha: Yes (cut-out)"));
  }
  else if (c.info.headerRead && c.info.magic == "BLP2")
  {
    parts.Add(wxString::Format(wxT("%u"), c.info.width) + Times + wxString::Format(wxT("%u"), c.info.height) + wxT(" ") +
              _("(from the file header; not decoded)"));
    parts.Add(wx(c.info.describeFormat()));
    parts.Add(_("Alpha: unknown"));
  }
  parts.Add(sizeText(c.bytes));
  wxString text;
  for (size_t i = 0; i < parts.size(); i++)
    text += (i ? Dot : wxString()) + parts[i];
  return text;
}

void TextureView::showInfo()
{
  const Current & c = m_current;
  const bool any = c.valid || !c.error.isEmpty() || c.entry.fileDataId > 0 || !c.entry.path.isEmpty();
  if (!any)
  {
    m_name->SetLabel(wxEmptyString);
    m_path->SetLabel(wxEmptyString);
    m_facts->SetLabel(wxEmptyString);
  }
  else
  {
    if (c.entry.unnamed)
    {
      m_name->SetLabel(wxString::Format(_("FileDataID %d"), c.entry.fileDataId));
      m_path->SetLabel(c.isLookup && !c.valid ? _("Not in the texture list") : _("No file name in the file list"));
    }
    else
    {
      m_name->SetLabel(wx(c.entry.name()));
      m_path->SetLabel(wx(c.entry.folder()));
    }
    wxString facts = factsLine();
    if (c.entry.fileDataId > 0)
      facts = wxString::Format(_("FileDataID %d"), c.entry.fileDataId) + (facts.IsEmpty() ? wxString() : Dot + facts);
    m_facts->SetLabel(facts);
    m_facts->SetToolTip(facts);
    m_path->SetToolTip(m_path->GetLabel());
  }
  m_copyPath->Enable(!c.entry.unnamed && !c.entry.path.isEmpty());
  m_copyId->Enable(c.entry.fileDataId > 0);
  // PNG only from decoded pixels; the original whenever the file was read (a file that is not a BLP,
  // opened by its FileDataID, is saved as what it is).
  // (Why there is no PNG is said in the preview: a disabled button shows no tooltip.)
  m_exportPng->Enable(c.decoded);
  const wxString original = currentIsBlp() || !c.valid ? _("Export BLP") : _("Export original file");
  if (m_exportBlp->GetLabel() != original)
    m_exportBlp->SetLabel(original);
  m_exportBlp->Enable(c.valid);
  Layout();
}

void TextureView::setAlphaView(TexturePreview::AlphaView view)
{
  m_preview->setAlphaView(view);
  // The colour under transparency is level 0's: a texture shown from a smaller level is decoded at level 0.
  if (view == TexturePreview::AlphaView::Off && m_current.decoded && m_current.imageLevel != 0 && !m_loadingSettings)
    sharpen(0);
  // A radio tool is checked by toggling it, which releases the others of its group (wxAuiToolBar
  // checks a radio tool whatever the flag says, so only the active one is touched).
  m_tools->ToggleTool(view == TexturePreview::AlphaView::On    ? ID_TV_ALPHA_ON
                      : view == TexturePreview::AlphaView::Off ? ID_TV_ALPHA_OFF
                                                               : ID_TV_ALPHA_ONLY, true);
  m_tools->Refresh();
  saveSettings();
}

// --------------------------------------------------------------------------------- copy and export

void TextureView::copyPath()
{
  if (!m_current.entry.unnamed && !m_current.entry.path.isEmpty() && wxTheClipboard->Open())
  {
    wxTheClipboard->SetData(new wxTextDataObject(wx(m_current.entry.path)));
    wxTheClipboard->Close();
    status(_("Path copied"));
  }
}

void TextureView::copyFileDataId()
{
  if (m_current.entry.fileDataId > 0 && wxTheClipboard->Open())
  {
    wxTheClipboard->SetData(new wxTextDataObject(wxString::Format(wxT("%d"), m_current.entry.fileDataId)));
    wxTheClipboard->Close();
    status(_("FileDataID copied"));
  }
}

bool TextureView::isBlp(const Current & c)
{
  // Its header says; a file too short for one is a BLP when it is a named .blp (a texture of the list,
  // damaged), not when it is a file opened by its FileDataID that has no such name.
  if (c.info.headerRead)
    return c.info.magic.startsWith("BLP");
  return !c.entry.unnamed && c.entry.path.endsWith(".blp", Qt::CaseInsensitive);
}

bool TextureView::currentIsBlp() const
{
  return isBlp(m_current);
}

wxString TextureView::exportFolder() const
{
  QSettings config(qs(cfgPath), QSettings::IniFormat);
  const QString saved = config.value("TextureViewer/ExportFolder").toString();
  if (!saved.isEmpty() && QFileInfo(saved).isDir())
    return wx(saved);
  return wxStandardPaths::Get().GetDocumentsDir();
}

wxString TextureView::exportFileName(const TextureEntry & entry, const wxString & extension, bool blpContent)
{
  const bool original = extension == wxT("blp");
  if (entry.unnamed || entry.path.isEmpty())
    return wxString::Format(wxT("%d."), entry.fileDataId) + (original && !blpContent ? wxString(wxT("unk")) : extension);
  QString base = entry.name();
  if (original && !base.endsWith(".blp", Qt::CaseInsensitive))
    return wx(base);
  if (base.endsWith(".blp", Qt::CaseInsensitive))
    base.chop(4);
  return wx(base) + wxT(".") + extension;
}

void TextureView::exportPngInteractive()
{
  if (!m_current.decoded)
  {
    status(m_current.valid ? _("This texture has no decoded image to save as PNG.") : _("Select a texture to export."));
    return;
  }
  wxFileDialog dialog(this, _("Export PNG"), exportFolder(), exportFileName(m_current.entry, wxT("png")),
                      _("PNG image (*.png)|*.png"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
  // The dialog is modal: the selection cannot change under it.
  if (dialog.ShowModal() != wxID_OK)
    return;
  wxString message;
  exportPng(dialog.GetPath(), message);
  status(message);
}

void TextureView::exportBlpInteractive()
{
  if (!m_current.valid)
  {
    status(_("Select a texture to export."));
    return;
  }
  const bool blp = currentIsBlp();
  wxFileDialog dialog(this, blp ? _("Export original BLP") : _("Export original file"), exportFolder(),
                      exportFileName(m_current.entry, wxT("blp"), blp),
                      blp ? _("BLP texture (*.blp)|*.blp") : _("All files (*.*)|*.*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
  if (dialog.ShowModal() != wxID_OK)
    return;
  wxString message;
  exportBlp(dialog.GetPath(), message);
  status(message);
}

bool TextureView::exportPng(const wxString & target, wxString & message)
{
  if (!m_current.decoded || m_current.image.isNull())
  {
    message = _("This texture has no decoded image to save as PNG.");
    return false;
  }
  // Level 0, full resolution, with its alpha; never the scaled display. A texture previewed from a smaller
  // level is decoded at level 0 for this.
  QImage full = m_current.image;
  if (m_current.imageLevel != 0)
  {
    if (!clientReady() || !decoderReady())
    {
      message = _("The game client or the texture decoder is not ready; try again in a moment.");
      return false;
    }
    wxBusyCursor busy;
    const TextureDecode::Source source = m_current.isLookup || m_current.entry.unnamed
                                           ? TextureDecode::readId(m_current.entry.fileDataId)
                                           : TextureDecode::read(m_current.entry);
    const TextureDecode::Pixels px = source.ok ? TextureDecode::decode(source.file, source.info, 0) : TextureDecode::Pixels();
    if (!px.ok || px.level != 0)
    {
      message = wxString::Format(_("Could not decode the texture at full resolution: %s"),
                                 wx(source.ok ? px.error : source.error));
      return false;
    }
    full = px.image;
  }
  QString error;
  if (!TextureDecode::savePng(qs(target), full, error))
  {
    message = wxString::Format(_("Could not save %s: %s"), target, wx(error));
    LOG_ERROR << "[texview] PNG export failed:" << qs(target) << error;
    return false;
  }
  message = wxString::Format(_("Saved %s (%d x %d PNG)"), target, full.width(), full.height());
  LOG_INFO << "[texview] exported PNG" << qs(target) << full.width() << "x" << full.height();
  QSettings config(qs(cfgPath), QSettings::IniFormat);
  config.setValue("TextureViewer/ExportFolder", QFileInfo(qs(target)).absolutePath());
  config.sync();
  return true;
}

bool TextureView::exportBlp(const wxString & target, wxString & message)
{
  if (!m_current.valid || !clientReady())
  {
    message = _("Select a texture to export.");
    return false;
  }
  // The file's bytes, read again as they are in the game client: nothing decoded or re-encoded.
  const TextureDecode::Source source = m_current.isLookup || m_current.entry.unnamed
                                         ? TextureDecode::readId(m_current.entry.fileDataId)
                                         : TextureDecode::read(m_current.entry);
  if (!source.ok)
  {
    message = wxString::Format(_("Could not read the file: %s"), wx(source.error));
    return false;
  }
  QString error;
  if (!TextureDecode::saveBlp(qs(target), source.bytes, error))
  {
    message = wxString::Format(_("Could not save %s: %s"), target, wx(error));
    LOG_ERROR << "[texview] BLP export failed:" << qs(target) << error;
    return false;
  }
  message = wxString::Format(_("Saved %s (the original file, %s bytes)"), target,
                             thousands((long long)source.bytes.size()));
  LOG_INFO << "[texview] exported BLP" << qs(target) << (qulonglong)source.bytes.size() << "bytes";
  QSettings config(qs(cfgPath), QSettings::IniFormat);
  config.setValue("TextureViewer/ExportFolder", QFileInfo(qs(target)).absolutePath());
  config.sync();
  return true;
}

// ------------------------------------------------------------------------------------------- keys

// Keys while the keyboard is in the texture view (the preview, its controls). Ctrl+S and Ctrl+Shift+S
// also work from Browse: the main window takes them there (ModelViewer::OnCharHook).
void TextureView::OnCharHook(wxKeyEvent & event)
{
  if (!IsShownOnScreen())
  {
    event.Skip();
    return;
  }
  const int key = event.GetKeyCode();
  const bool ctrl = event.ControlDown(), shift = event.ShiftDown(), alt = event.AltDown();
  if (ctrl && !alt && key == 'S')
  {
    if (shift)
      exportBlpInteractive();
    else
      exportPngInteractive();
    return;
  }
  const wxWindow * focus = wxWindow::FindFocus();
  // The arrow keys stay in the preview: there is nothing to pan, and the panel would move the keyboard
  // on to the next control, taking the alpha keys with it.
  if (!ctrl && !alt && focus == m_preview &&
      (key == WXK_LEFT || key == WXK_RIGHT || key == WXK_UP || key == WXK_DOWN || key == WXK_NUMPAD_LEFT ||
       key == WXK_NUMPAD_RIGHT || key == WXK_NUMPAD_UP || key == WXK_NUMPAD_DOWN))
    return;
  // The alpha keys only where they cannot be typing: the preview and the toolbar's buttons.
  if (!ctrl && !alt && (focus == m_preview || focus == m_tools || focus == this))
  {
    if (key == 'A')
    {
      const TexturePreview::AlphaView v = m_preview->alphaView();
      if (shift)
        setAlphaView(v == TexturePreview::AlphaView::Only ? TexturePreview::AlphaView::On : TexturePreview::AlphaView::Only);
      else
        setAlphaView(v == TexturePreview::AlphaView::On ? TexturePreview::AlphaView::Off : TexturePreview::AlphaView::On);
      return;
    }
  }
  event.Skip();
}
