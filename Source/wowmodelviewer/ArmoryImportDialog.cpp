/*
 * ArmoryImportDialog.cpp
 *
 * See ArmoryImportDialog.h.
 */

#include "ArmoryImportDialog.h"

#include <algorithm>
#include <memory>

#include <wx/combobox.h>
#include <wx/statline.h>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QVariantMap>

#include "CharInfos.h"
#include "GlobalSettings.h"
#include "modelviewer.h"
#include "UiStyle.h"
#include "util.h"

#include "logger/Logger.h"

namespace
{
  // How many characters the Recent list keeps.
  const int RecentMax = 8;

  // A cached realm list is used as it is for a week, then asked for again (and still used if
  // asking fails: realms are added and merged rarely).
  const qint64 RealmListFreshSeconds = 7 * 24 * 3600;

  // Regions whose proxy answered "no realm list here" during this run. Not kept on disk, so a
  // proxy that gains the list is asked again the next time the viewer starts.
  QStringList s_noRealmList;

  // ... and those whose realm-list request failed (timeout, server error), with when: asked
  // again after a few minutes rather than on every opening and region change.
  QHash<QString, qint64> s_realmListFailedAt;
  const qint64 RealmListRetrySeconds = 10 * 60;

  // The width of the fields column, in DIPs.
  const int ContentWidth = 340;

  // The inline notice colour WMV already uses (ModelInspector's geoset notice).
  const wxColour NoticeColour(170, 90, 0);

  // Typographic characters, spelled as escapes: the sources are compiled as ANSI.
  const wxString Ellipsis(L"\u2026");
  const wxString Dash(L"\u2014");
  const wxString Dot(L" \u2022 ");

  QString toQt(const wxString & s) { return QString::fromWCharArray(s.wc_str()); }
  wxString toWx(const QString & s) { return wxString(s.toStdWString()); }

  QString configPath() { return toQt(cfgPath); }

  QString realmCachePath()
  {
    return QFileInfo(configPath()).absolutePath() + "/ArmoryRealms.json";
  }

  QJsonObject readRealmCache()
  {
    QFile file(realmCachePath());
    if (!file.open(QIODevice::ReadOnly))
      return QJsonObject();
    return QJsonDocument::fromJson(file.readAll()).object();
  }

  void writeRealmCache(const QJsonObject & cache)
  {
    QFile file(realmCachePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
      file.write(QJsonDocument(cache).toJson(QJsonDocument::Compact));
  }

  // A realm's slug from what was typed, for when there is no realm list to look it up in.
  // One slug-shaped word ("stormscale", or "argent-dawn" copied from a link) is the slug itself.
  // A realm's display name follows the rule its slug is made by: lower case, apostrophes,
  // brackets and hyphens dropped, spaces turned into hyphens -- "Mal'Ganis" -> malganis,
  // "Aggra (Portugues)" with its accent -> aggra-portugues with the accent kept, "Area 52" ->
  // area-52. That rule gives the real slug for every retail realm of all four regions (checked
  // against the complete lists); the one word it cannot tell apart is a hyphenated name typed
  // without spaces ("Azjol-Nerub", whose slug is azjolnerub), which the realm list covers.
  bool slugFromTyped(const QString & typed, QString & slug)
  {
    static const QRegularExpression spaces("\\s+", QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression slugShape("^[\\p{L}\\p{N}-]{1,64}$",
                                              QRegularExpression::UseUnicodePropertiesOption);
    const QString text = typed.trimmed().toLower();
    if (slugShape.match(text).hasMatch())
    {
      slug = text;
      return true;
    }

    slug = text;
    for (const QChar dropped : { QChar('\''), QChar(0x2019), QChar('('), QChar(')'), QChar('-') })
      slug.remove(dropped);
    slug.replace(spaces, "-");
    return slugShape.match(slug).hasMatch();
  }

  // Text wrapped to a width for a label. Lines break at spaces, as Wrap() does, but a word wider
  // than the label (a link in a message) is cut too -- after a '/' where one fits, else where it
  // must -- since the native label would only clip it.
  wxString wrapToWidth(const wxWindow * w, const wxString & text, int width)
  {
    wxString out;
    const wxArrayString paragraphs = wxSplit(text, wxT('\n'), wxT('\0'));
    for (size_t p = 0; p < paragraphs.size(); p++)
    {
      if (p)
        out += wxT('\n');
      wxString line;
      for (wxString word : wxSplit(paragraphs[p], wxT(' '), wxT('\0')))
      {
        while (w->GetTextExtent(word).x > width && word.length() > 1)
        {
          size_t fits = 1;
          while (fits < word.length() && w->GetTextExtent(word.Left(fits + 1)).x <= width)
            fits++;
          const size_t slash = word.Left(fits).find_last_of(wxT('/'));
          const size_t cut = (slash != wxString::npos && slash > 0) ? slash + 1 : fits;
          if (!line.empty())
          {
            out += line + wxT('\n');
            line.clear();
          }
          out += word.Left(cut) + wxT('\n');
          word = word.Mid(cut);
        }
        const wxString candidate = line.empty() ? word : line + wxT(' ') + word;
        if (!line.empty() && w->GetTextExtent(candidate).x > width)
        {
          out += line + wxT('\n');
          line = word;
        }
        else
        {
          line = candidate;
        }
      }
      out += line;
    }
    return out;
  }

  // The first paragraph of an importer message is its headline; the rest explains.
  void splitMessage(const wxString & message, wxString & title, wxString & detail)
  {
    const int gap = message.Find(wxT("\n\n"));
    if (gap == wxNOT_FOUND)
    {
      title = message;
      detail.clear();
      return;
    }
    title = message.Left(gap);
    detail = message.Mid(gap + 2);
  }
}

ArmoryImportDialog::ArmoryImportDialog(ModelViewer * viewer)
  : wxDialog(viewer, wxID_ANY, _("Import Armory Character"), wxDefaultPosition, wxDefaultSize,
             wxDEFAULT_DIALOG_STYLE, wxT("armoryImportDialog")),
    m_viewer(viewer)
{
  // Only the regions the importer and its proxy serve.
  m_regions.push_back(Region{ "eu", _("Europe") });
  m_regions.push_back(Region{ "us", _("Americas & Oceania") }); // US, Latin American, Brazilian and Oceanic realms
  m_regions.push_back(Region{ "kr", _("Korea") });
  m_regions.push_back(Region{ "tw", _("Taiwan") });

  buildLayout();
  loadSettings();
  fillRecent();
  loadRealms(false); // from the cache only; asking the proxy waits until the dialog is up
  refreshState();
  CentreOnParent(); // again, now that the Recent row is shown or hidden

  Bind(wxEVT_INIT_DIALOG, [this](wxInitDialogEvent & event)
  {
    event.Skip();
    // After the dialog is on screen: fetch the realm list if the cache has none (the dialog
    // shows that it is loading), then put the cursor in the name field -- every time it opens.
    CallAfter([this]()
    {
      loadRealms(true);
      // Queued behind the release of a realm-list fetch, so the field is enabled again by then.
      CallAfter([this]()
      {
        if (m_name->IsEnabled())
          m_name->SetFocus();
      });
    });
  });

  // While a request runs the dialog must not close under its own feet: the request's wait
  // still dispatches messages, so Escape, Alt+F4 or the title-bar X would reach it.
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent & event)
  {
    if (m_busy && event.CanVeto())
    {
      event.Veto();
      return;
    }
    event.Skip();
  });
  Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent & event)
  {
    if (m_busy && (event.GetKeyCode() == WXK_ESCAPE || event.GetKeyCode() == WXK_RETURN ||
                   event.GetKeyCode() == WXK_NUMPAD_ENTER))
      return; // swallowed
    event.Skip();
  });
}

void ArmoryImportDialog::buildLayout()
{
  const int xs = FromDIP(UiStyle::XS);
  const int sp = FromDIP(UiStyle::S);
  const int md = FromDIP(UiStyle::M);
  const int lg = FromDIP(UiStyle::L);
  const int controlHeight = FromDIP(UiStyle::ControlHeight);
  m_contentWidth = FromDIP(ContentWidth);

  wxBoxSizer * column = new wxBoxSizer(wxVERTICAL);

  // --- Character ------------------------------------------------------------------------
  column->Add(UiStyle::sectionHeader(this, _("Character")), 0, wxEXPAND);

  // Recent characters (shown only once there are any). Created first, so it is first in the
  // tab order when it is there.
  m_recentLabel = new wxStaticText(this, wxID_ANY, _("Recent characters"));
  m_recentChoice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(m_contentWidth, controlHeight));
  m_recentChoice->SetName(wxT("armoryRecent"));
  column->Add(m_recentLabel, 0, wxTOP, sp);
  column->Add(m_recentChoice, 0, wxEXPAND | wxTOP, xs);

  // Region and realm side by side, the realm wider.
  wxFlexGridSizer * place = new wxFlexGridSizer(2, 2, xs, sp);
  place->AddGrowableCol(1, 1);
  place->Add(new wxStaticText(this, wxID_ANY, _("Region")));
  place->Add(new wxStaticText(this, wxID_ANY, _("Realm")));

  m_region = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(140), controlHeight));
  m_region->SetName(wxT("armoryRegion"));
  for (const Region & region : m_regions)
    m_region->Append(region.label);
  place->Add(m_region, 0, wxEXPAND);

  m_realm = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, controlHeight),
                           0, nullptr, wxCB_DROPDOWN);
  m_realm->SetName(wxT("armoryRealm"));
  place->Add(m_realm, 1, wxEXPAND);
  column->Add(place, 0, wxEXPAND | wxTOP, sp);

  column->Add(new wxStaticText(this, wxID_ANY, _("Character name")), 0, wxTOP, sp);
  m_name = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, controlHeight));
  m_name->SetName(wxT("armoryName"));
  column->Add(m_name, 0, wxEXPAND | wxTOP, xs);

  // One line under the fields, always there so nothing jumps: why Import is unavailable, or
  // what a typed realm will be sent as.
  m_hint = new wxStaticText(this, wxID_ANY, wxT(" "));
  m_hint->SetName(wxT("armoryHint"));
  m_hint->SetMinSize(wxSize(m_contentWidth, m_hint->GetCharHeight()));
  column->Add(m_hint, 0, wxEXPAND | wxTOP, xs);

  m_import = new wxButton(this, wxID_ANY, _("Import character"));
  m_import->SetName(wxT("armoryImport"));
  m_import->SetFont(m_import->GetFont().Bold());
  m_import->SetMinSize(wxSize(-1, controlHeight));
  m_import->SetDefault();
  column->Add(m_import, 0, wxALIGN_RIGHT | wxTOP, xs);

  // Status: looking up / imported / why not. Room for a headline and a few lines is kept, so
  // the dialog does not change size as the states come and go.
  m_statusTitle = new wxStaticText(this, wxID_ANY, wxEmptyString);
  m_statusTitle->SetName(wxT("armoryStatusTitle"));
  m_statusTitle->SetFont(m_statusTitle->GetFont().Bold());
  m_statusDetail = new wxStaticText(this, wxID_ANY, wxEmptyString);
  m_statusDetail->SetName(wxT("armoryStatusDetail"));
  wxBoxSizer * status = new wxBoxSizer(wxVERTICAL);
  status->Add(m_statusTitle, 0, wxEXPAND);
  status->Add(m_statusDetail, 0, wxEXPAND | wxTOP, xs);
  status->SetMinSize(wxSize(m_contentWidth, m_statusTitle->GetCharHeight() * 4 + xs));
  column->Add(status, 0, wxEXPAND | wxTOP, sp);

  // --- Armory link ----------------------------------------------------------------------
  column->Add(UiStyle::sectionHeader(this, _("Armory link")), 0, wxEXPAND | wxTOP, lg);
  wxBoxSizer * linkRow = new wxBoxSizer(wxHORIZONTAL);
  m_link = new wxTextCtrl(this, wxID_ANY, armoryPath, wxDefaultPosition, wxSize(-1, controlHeight),
                          wxTE_PROCESS_ENTER);
  m_link->SetName(wxT("armoryLink"));
  m_link->SetHint(_("Paste a character's Armory link"));
  linkRow->Add(m_link, 1, wxALIGN_CENTER_VERTICAL);
  m_importLink = new wxButton(this, wxID_ANY, _("Import link"));
  m_importLink->SetName(wxT("armoryImportLink"));
  m_importLink->SetMinSize(wxSize(-1, controlHeight));
  linkRow->Add(m_importLink, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, sp);
  column->Add(linkRow, 0, wxEXPAND | wxTOP, sp);
  column->Add(UiStyle::secondaryLabel(this, _("The address of a character's Armory page, with /character/ in it.")),
              0, wxTOP, xs);

  wxBoxSizer * outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(column, 1, wxEXPAND | wxALL, md);
  wxStdDialogButtonSizer * buttons = CreateStdDialogButtonSizer(wxCLOSE);
  outer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, md);
  m_close = wxDynamicCast(FindWindow(wxID_CLOSE), wxButton);
  if (m_close)
    m_close->SetName(wxT("armoryClose"));

  SetSizerAndFit(outer);
  SetEscapeId(wxID_CLOSE);
  CentreOnParent();

  // --- behaviour ------------------------------------------------------------------------
  m_import->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { onImportCharacter("button"); });
  m_importLink->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { onImportLink(); });
  m_link->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { onImportLink(); });
  m_recentChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { onRecentChosen(); });
  m_region->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { onRegionChosen(); });
  m_realm->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { refreshState(); });
  m_realm->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &) { refreshState(); });
  m_name->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { refreshState(); });
  m_link->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { refreshState(); });
}

const ArmoryImportDialog::Region * ArmoryImportDialog::currentRegion() const
{
  const int index = m_region->GetSelection();
  return (index >= 0 && index < m_regions.size()) ? &m_regions[index] : nullptr;
}

wxString ArmoryImportDialog::regionLabel(const QString & code) const
{
  for (const Region & region : m_regions)
    if (region.code.compare(code, Qt::CaseInsensitive) == 0)
      return region.label;
  return toWx(code.toUpper());
}

void ArmoryImportDialog::loadSettings()
{
  QSettings config(configPath(), QSettings::IniFormat);
  config.beginGroup("Armory");
  const QString lastRegion = config.value("LastRegion").toString();
  const QString lastRealmName = config.value("LastRealmName").toString();
  const QString lastRealm = config.value("LastRealm").toString();

  m_recent.clear();
  const int count = config.beginReadArray("Recent");
  for (int i = 0; i < count && m_recent.size() < RecentMax; i++)
  {
    config.setArrayIndex(i);
    Recent entry;
    entry.name = config.value("name").toString();
    entry.realmSlug = config.value("realm").toString();
    entry.realmName = config.value("realmName").toString();
    entry.region = config.value("region").toString();
    if (!entry.name.isEmpty() && !entry.realmSlug.isEmpty() && !entry.region.isEmpty())
      m_recent.push_back(entry);
  }
  config.endArray();
  config.endGroup();

  int regionIndex = 0;
  for (int i = 0; i < m_regions.size(); i++)
    if (m_regions[i].code == lastRegion)
      regionIndex = i;
  m_region->SetSelection(regionIndex);

  if (!lastRealmName.isEmpty())
    m_realm->ChangeValue(toWx(lastRealmName));
  else if (!lastRealm.isEmpty())
    m_realm->ChangeValue(toWx(lastRealm));
}

// After a successful import, straight away (not at exit): the region and realm to start from
// next time, and the character at the top of Recent. The character itself is not prefilled.
void ArmoryImportDialog::rememberSuccess(const Recent & entry)
{
  for (int i = m_recent.size() - 1; i >= 0; i--)
    if (m_recent[i].region == entry.region && m_recent[i].realmSlug == entry.realmSlug &&
        m_recent[i].name.compare(entry.name, Qt::CaseInsensitive) == 0)
      m_recent.remove(i);
  m_recent.push_front(entry);
  while (m_recent.size() > RecentMax)
    m_recent.pop_back();

  QSettings config(configPath(), QSettings::IniFormat);
  config.beginGroup("Armory");
  config.setValue("LastRegion", entry.region);
  config.setValue("LastRealm", entry.realmSlug);
  config.setValue("LastRealmName", entry.realmName);
  config.remove("Recent");
  config.beginWriteArray("Recent", m_recent.size());
  for (int i = 0; i < m_recent.size(); i++)
  {
    config.setArrayIndex(i);
    config.setValue("name", m_recent[i].name);
    config.setValue("realm", m_recent[i].realmSlug);
    config.setValue("realmName", m_recent[i].realmName);
    config.setValue("region", m_recent[i].region);
  }
  config.endArray();
  config.endGroup();
  config.sync();
}

void ArmoryImportDialog::fillRecent()
{
  m_recentChoice->Clear();
  for (const Recent & entry : m_recent)
    m_recentChoice->Append(wxString::Format(wxT("%s %s %s (%s)"), toWx(entry.name), Dash,
                                            toWx(entry.realmName.isEmpty() ? entry.realmSlug : entry.realmName),
                                            toWx(entry.region.toUpper())));
  m_recentChoice->SetSelection(wxNOT_FOUND);

  const bool any = !m_recent.isEmpty();
  if (m_recentLabel->IsShown() != any || m_recentChoice->IsShown() != any)
  {
    m_recentLabel->Show(any);
    m_recentChoice->Show(any);
    Layout();
    Fit();
  }
}

// The selected region's realm list: from ArmoryRealms.json while it is fresh, otherwise -- when
// allowed -- from the proxy, through the importer plugin. A proxy without the list leaves the
// field for typing (with the realms that imported before as suggestions).
void ArmoryImportDialog::loadRealms(bool allowFetch)
{
  const Region * region = currentRegion();
  if (!region)
    return;

  m_realms.clear();
  m_realmListLoaded = false;

  QJsonObject cache = readRealmCache();
  QJsonObject regions = cache.value("regions").toObject();
  QJsonObject entry = regions.value(region->code).toObject();
  const qint64 now = QDateTime::currentSecsSinceEpoch();

  auto take = [this](const QJsonArray & list)
  {
    m_realms.clear();
    for (const auto & value : list)
    {
      const QJsonObject realm = value.toObject();
      Realm r{ realm.value("slug").toString(), realm.value("name").toString() };
      if (r.slug.isEmpty())
        continue;
      if (r.name.isEmpty())
        r.name = r.slug;
      m_realms.push_back(r);
    }
    m_realmListLoaded = !m_realms.isEmpty();
  };

  if (entry.value("realms").isArray())
    take(entry.value("realms").toArray());

  const bool fresh = m_realmListLoaded &&
                     now - static_cast<qint64>(entry.value("fetched").toDouble()) < RealmListFreshSeconds;
  // The proxy override is part of the key: a different proxy may well have the list.
  const QString noListKey = region->code + "|" + QString::fromStdString(GLOBALSETTINGS.armoryProxyURL());
  const bool refusedThisRun = s_noRealmList.contains(noListKey);
  const bool failedLately = s_realmListFailedAt.contains(noListKey) &&
                            now - s_realmListFailedAt.value(noListKey) < RealmListRetrySeconds;

  if (allowFetch && !fresh && !refusedThisRun && !failedLately && !m_busy)
  {
    // Where the keyboard was, to give it back afterwards: the fetch disables every control,
    // including the one whose change started it.
    wxWindow * focused = wxWindow::FindFocus();
    setBusy(true);
    showStatus(Tone::Working, wxString::Format(_("Loading the %s realm list"), region->label) + Ellipsis, wxEmptyString);
    Update();

    const QVariantMap answer = m_viewer->ArmoryRealmList(region->code);
    if (answer.value("ok").toBool())
    {
      QJsonArray list;
      for (const QVariant & value : answer.value("realms").toList())
      {
        const QVariantMap realm = value.toMap();
        QJsonObject item;
        item["slug"] = realm.value("slug").toString();
        item["name"] = realm.value("name").toString();
        item["id"] = realm.value("id").toInt();
        list.append(item);
      }
      entry = QJsonObject();
      entry["fetched"] = static_cast<double>(now);
      entry["realms"] = list;
      take(list);
      s_realmListFailedAt.remove(noListKey);
    }
    else if (answer.value("unsupported").toBool())
    {
      // Keep any list cached earlier; just do not ask this proxy again during this run.
      s_noRealmList << noListKey;
    }
    else
    {
      // A failed request stores nothing on disk; it is tried again in a few minutes.
      s_realmListFailedAt[noListKey] = now;
    }

    regions[region->code] = entry;
    cache["regions"] = regions;
    cache["version"] = 1;
    writeRealmCache(cache);

    showStatus(Tone::Working, wxEmptyString, wxEmptyString);
    CallAfter([this, focused]()
    {
      setBusy(false);
      refreshState();
      if (focused && focused->IsEnabled() && focused->IsShown())
        focused->SetFocus();
    });
  }

  fillRealmChoices();
}

// The realms the field knows for the selected region: the whole list when there is one,
// otherwise the realms that imported before (the last one and those in Recent).
QVector<ArmoryImportDialog::Realm> ArmoryImportDialog::knownRealms() const
{
  if (m_realmListLoaded)
    return m_realms;

  QVector<Realm> realms;
  const Region * region = currentRegion();
  if (!region)
    return realms;

  auto add = [&realms](const QString & slug, const QString & name)
  {
    if (slug.isEmpty())
      return;
    for (const Realm & known : realms)
      if (known.slug == slug)
        return;
    realms.push_back(Realm{ slug, name.isEmpty() ? slug : name });
  };

  for (const Recent & entry : m_recent)
    if (entry.region == region->code)
      add(entry.realmSlug, entry.realmName);

  QSettings config(configPath(), QSettings::IniFormat);
  if (config.value("Armory/LastRegion").toString() == region->code)
    add(config.value("Armory/LastRealm").toString(), config.value("Armory/LastRealmName").toString());
  return realms;
}

void ArmoryImportDialog::fillRealmChoices()
{
  const wxString typed = m_realm->GetValue();
  wxArrayString names;
  for (const Realm & realm : knownRealms())
    names.Add(toWx(realm.name));

  m_realm->Freeze();
  m_realm->Set(names);
  m_realm->ChangeValue(typed);
  m_realm->Thaw();
  // Typing filters: the system's suggestion list offers the realms that start with what was typed.
  m_realm->AutoComplete(names);
  m_realm->SetHint(m_realmListLoaded ? _("Start typing a realm name") : _("Realm name"));
}

bool ArmoryImportDialog::resolveRealm(QString & slug, QString & displayName, wxString & hint, bool & warn) const
{
  warn = false;
  hint.clear();
  const QString typed = toQt(m_realm->GetValue()).normalized(QString::NormalizationForm_C).simplified();
  if (typed.isEmpty())
    return false;

  // A realm the field knows, by display name or by slug.
  for (const Realm & realm : knownRealms())
  {
    if (realm.name.compare(typed, Qt::CaseInsensitive) == 0 || realm.slug.compare(typed, Qt::CaseInsensitive) == 0)
    {
      slug = realm.slug;
      displayName = realm.name;
      return true;
    }
  }

  const Region * region = currentRegion();
  if (m_realmListLoaded)
  {
    // The list is the region's whole list: anything else is not a realm there.
    warn = true;
    hint = wxString::Format(_("%s has no realm called \"%s\"."), region ? region->label : wxString(),
                            toWx(typed));
    return false;
  }

  // No list: take the realm as a character's Armory link spells it.
  QString candidate;
  if (!slugFromTyped(typed, candidate))
  {
    warn = true;
    hint = _("Type the realm as it appears in a character's Armory link, for example argent-dawn.");
    return false;
  }
  slug = candidate;
  displayName = typed;
  // Say so when the realm is sent as something other than what was typed (beyond letter case).
  if (candidate != typed.toLower())
    hint = wxString::Format(_("Sent as %s"), toWx(candidate));
  return true;
}

bool ArmoryImportDialog::resolveName(QString & name, wxString & hint) const
{
  hint.clear();
  // Trimmed, and in the composed Unicode form the profile API matches (e-acute as one character, not "e" plus a
  // combining accent, which it answers with "not found"). The case is kept as typed.
  name = toQt(m_name->GetValue()).trimmed().normalized(QString::NormalizationForm_C);
  if (name.isEmpty())
    return false;

  static const QRegularExpression forbidden("[\\s/?#]");
  if (name.contains(forbidden))
  {
    hint = _("A character name has no spaces and none of / ? #.");
    return false;
  }
  if (name.size() > 32)
  {
    hint = _("That is longer than a character name can be.");
    return false;
  }

  bool letter = false;
  for (const uint codePoint : name.toUcs4())
    letter = letter || QChar::isLetter(codePoint);
  if (!letter)
  {
    hint = _("A character name is made of letters.");
    return false;
  }
  return true;
}

// Enable Import only for something that can be sent, and say why not on the hint line.
void ArmoryImportDialog::refreshState()
{
  QString slug, realmName, name;
  wxString realmHint, nameHint;
  bool realmWarn = false;
  const bool realmOk = resolveRealm(slug, realmName, realmHint, realmWarn);
  const bool nameOk = resolveName(name, nameHint);

  wxString hint;
  bool warn = false;
  if (realmWarn)
  {
    hint = realmHint;
    warn = true;
  }
  else if (!nameHint.empty())
  {
    hint = nameHint;
    warn = true;
  }
  else
  {
    hint = realmHint; // "Sent as ...", or nothing
  }

  m_hint->SetForegroundColour(warn ? NoticeColour : UiStyle::secondaryText());
  m_hint->SetLabelText(hint.empty() ? wxString(wxT(" ")) : wrapToWidth(m_hint, hint, m_contentWidth));
  // The line is reserved at one line high; a hint that wraps needs the room for all of it.
  m_hint->InvalidateBestSize();
  m_hint->SetMinSize(wxSize(m_contentWidth, std::max(m_hint->GetCharHeight(), m_hint->GetBestSize().y)));
  growToFit();

  m_import->Enable(!m_busy && currentRegion() && realmOk && nameOk);
  m_importLink->Enable(!m_busy && !m_link->GetValue().Strip(wxString::both).empty());
}

void ArmoryImportDialog::onImportCharacter(const char * trigger)
{
  if (m_busy)
  {
    LOG_INFO << "[armory-dialog] ignored: a request is already running";
    return;
  }

  const Region * region = currentRegion();
  QString slug, realmName, name;
  wxString hint;
  bool warn = false;
  if (!region || !resolveRealm(slug, realmName, hint, warn) || !resolveName(name, hint))
  {
    refreshState();
    return;
  }

  // The fields become the character link the importer already reads
  // (.../character/<region>/<realm>/<name>), so this goes through exactly the request path a
  // pasted link does.
  const QString link = QString("https://worldofwarcraft.blizzard.com/en-gb/character/%1/%2/%3")
    .arg(region->code)
    .arg(QString::fromUtf8(QUrl::toPercentEncoding(slug)))
    .arg(QString::fromUtf8(QUrl::toPercentEncoding(name)));

  LOG_INFO << "Armory dialog: importing" << name + "-" + realmName << "(" << region->code << ") [" << trigger << "]";
  runImport(toWx(link), wxString::Format(_("Looking up %s-%s"), toWx(name), toWx(realmName)) + Ellipsis, m_name, false);
}

void ArmoryImportDialog::onImportLink()
{
  if (m_busy)
  {
    LOG_INFO << "[armory-dialog] ignored: a request is already running";
    return;
  }

  const wxString link = m_link->GetValue().Strip(wxString::both);
  if (link.empty())
    return;

  armoryPath = link; // Settings/ArmoryPath, as before
  LOG_INFO << "Armory dialog: importing a link [link]";
  runImport(link, wxString(_("Looking up the character in the link")) + Ellipsis, m_link, true);
}

void ArmoryImportDialog::runImport(const wxString & link, const wxString & lookingUp, wxWindow * origin, bool fromLink)
{
  setBusy(true);
  showStatus(Tone::Working, lookingUp, wxEmptyString);
  Update(); // painted before the wait, which only repaints what was already invalidated

  wxString error;
  QVariantMap summary;
  std::unique_ptr<CharInfos> info(m_viewer->FetchArmoryCharacter(link, error, &summary));
  const bool ok = info && m_viewer->ApplyArmoryCharacter(*info, error);

  if (ok)
  {
    // The server's spelling where the importer could say, else what was typed.
    QString typedName, typedSlug, typedRealmName;
    wxString unused;
    bool unusedWarn = false;
    resolveName(typedName, unused);
    resolveRealm(typedSlug, typedRealmName, unused, unusedWarn);

    Recent entry;
    entry.name = summary.value("name").toString();
    entry.realmSlug = summary.value("realmSlug").toString();
    entry.realmName = summary.value("realmName").toString();
    entry.region = summary.value("region").toString().toLower();
    if (!fromLink)
    {
      const Region * region = currentRegion();
      if (entry.name.isEmpty()) entry.name = typedName;
      if (entry.realmSlug.isEmpty()) entry.realmSlug = typedSlug;
      if (entry.realmName.isEmpty()) entry.realmName = typedRealmName;
      if (entry.region.isEmpty() && region) entry.region = region->code;
    }

    const wxString who = wxString::Format(wxT("%s-%s"), toWx(entry.name.isEmpty() ? QString("?") : entry.name),
                                          toWx(entry.realmName.isEmpty() ? entry.realmSlug : entry.realmName));

    // What the answer already said: race and class, then realm and region.
    wxArrayString what;
    if (!summary.value("raceName").toString().isEmpty())
      what.Add(toWx(summary.value("raceName").toString()));
    if (!summary.value("className").toString().isEmpty())
      what.Add(toWx(summary.value("className").toString()));
    wxArrayString where;
    if (!entry.realmName.isEmpty())
      where.Add(toWx(entry.realmName));
    if (!entry.region.isEmpty())
      where.Add(regionLabel(entry.region));
    auto joined = [](const wxArrayString & parts)
    {
      wxString out;
      for (size_t i = 0; i < parts.size(); i++)
        out += (i ? Dot : wxString()) + parts[i];
      return out;
    };
    wxString detail = joined(what);
    const wxString place = joined(where);
    if (!place.empty())
      detail += (detail.empty() ? wxString() : wxString(wxT("\n"))) + place;

    // Only a region the dialog offers is remembered: a classic character imported from a link
    // (region "classic-eu") is shown as imported, but could not be selected again.
    bool offered = false;
    for (const Region & region : m_regions)
      offered = offered || region.code == entry.region;

    if (offered && !entry.name.isEmpty() && !entry.realmSlug.isEmpty())
    {
      rememberSuccess(entry);
      fillRecent();
      if (fromLink)
      {
        // Show where the linked character lives, so the fields match what was imported.
        for (int i = 0; i < m_regions.size(); i++)
          if (m_regions[i].code == entry.region && m_region->GetSelection() != i)
          {
            m_region->SetSelection(i);
            loadRealms(false);
          }
        m_realm->ChangeValue(toWx(entry.realmName.isEmpty() ? entry.realmSlug : entry.realmName));
      }
      else
      {
        // The realm as the server spells it ("stormscale" typed shows as "Stormscale").
        if (!entry.realmName.isEmpty())
          m_realm->ChangeValue(toWx(entry.realmName));
        if (!m_realmListLoaded)
          fillRealmChoices(); // the realm that just worked becomes a suggestion
      }
    }

    LOG_INFO << "Armory dialog: imported" << toQt(who);
    showStatus(Tone::Success, wxString::Format(_("Imported %s"), who), detail);
  }
  else
  {
    wxString title, detail;
    splitMessage(error, title, detail);
    LOG_INFO << "Armory dialog: import failed -" << toQt(title);
    showStatus(Tone::Error, title, detail);
  }

  // Released only after everything queued while the request ran has been handled, so a click
  // or Enter that arrived meanwhile meets disabled controls instead of starting another import.
  CallAfter([this, origin]()
  {
    setBusy(false);
    refreshState();
    if (origin && origin->IsEnabled())
    {
      origin->SetFocus();
      if (wxTextCtrl * text = wxDynamicCast(origin, wxTextCtrl))
        text->SelectAll();
    }
  });
}

void ArmoryImportDialog::onRecentChosen()
{
  const int index = m_recentChoice->GetSelection();
  if (index < 0 || index >= m_recent.size() || m_busy)
    return;

  const Recent & entry = m_recent[index];
  for (int i = 0; i < m_regions.size(); i++)
    if (m_regions[i].code == entry.region && m_region->GetSelection() != i)
    {
      m_region->SetSelection(i);
      loadRealms(true);
    }
  m_realm->ChangeValue(toWx(entry.realmName.isEmpty() ? entry.realmSlug : entry.realmName));
  m_name->ChangeValue(toWx(entry.name));
  refreshState();
  // Queued behind the release of any realm-list fetch the region change started.
  CallAfter([this]()
  {
    if (m_name->IsEnabled())
    {
      m_name->SetFocus();
      m_name->SelectAll();
    }
  });
}

void ArmoryImportDialog::onRegionChosen()
{
  loadRealms(true);
  refreshState();
}

void ArmoryImportDialog::setBusy(bool busy)
{
  m_busy = busy;
  for (wxWindow * control : { static_cast<wxWindow *>(m_recentChoice), static_cast<wxWindow *>(m_region),
                              static_cast<wxWindow *>(m_realm), static_cast<wxWindow *>(m_name),
                              static_cast<wxWindow *>(m_link), static_cast<wxWindow *>(m_close) })
    if (control)
      control->Enable(!busy);
  if (busy)
  {
    m_import->Disable();
    m_importLink->Disable();
  }
  EnableCloseButton(!busy);
}

void ArmoryImportDialog::showStatus(Tone tone, const wxString & title, const wxString & detail)
{
  m_statusTitle->SetForegroundColour(tone == Tone::Error ? NoticeColour
                                                         : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
  m_statusDetail->SetForegroundColour(tone == Tone::Success ? wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT)
                                                            : UiStyle::secondaryText());
  m_statusTitle->SetLabelText(wrapToWidth(m_statusTitle, title, m_contentWidth));
  m_statusDetail->SetLabelText(wrapToWidth(m_statusDetail, detail, m_contentWidth));

  growToFit();
  Refresh();
}

// Grow for a long message, never shrink: the dialog should not jump between states.
void ArmoryImportDialog::growToFit()
{
  if (!GetSizer())
    return;
  Layout();
  const wxSize best = GetSizer()->GetMinSize();
  const wxSize client = GetClientSize();
  if (best.y > client.y)
    SetClientSize(wxSize(client.x, best.y));
}
