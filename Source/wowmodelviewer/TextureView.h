/*
 * TextureView.h
 *
 * The texture viewer: what the centre of the main window shows in Textures mode (ModelViewer::
 * SetViewerMode). It is the viewport host's content window (UnityRendererHost::showContent), so it
 * takes the Unity player's place without a second window or pane; the player is only taken off screen
 * meanwhile (its frame parked), and whatever model is loaded stays loaded behind it.
 *
 * Top to bottom: the view controls (alpha on / off / only; checkerboard, dark or light background),
 * the texture (TexturePreview: centred, sized for it, never zoomed), what the file is -- name, folder,
 * FileDataID, size, format, mip levels, alpha, file size, with Copy path and Copy FileDataID beside
 * them -- and the two exports as buttons: Export PNG (full resolution, with alpha; not for a texture
 * that is not decoded) and Export BLP (the original file byte for byte).
 *
 * THE SELECTION. Textures mode exists without one: with nothing picked the view says "Select a texture
 * in Browse". A pick (show) is the selection at once -- its name, folder and FileDataID are shown, its
 * facts say it is being read, its exports wait -- while the picture of the texture before stays on screen,
 * untouched, until the new one is ready and replaces it in one paint. The read and decode run as soon
 * as the window has nothing else to do (its idle event): after the input already queued and after the
 * repaint of the row and the facts, so rows clicked quickly decode only the last. While an arrow key
 * repeats in Browse the rows go by without decoding and the row it stops on decodes when it is let go:
 * a repeat comes every 33 ms or so, about what a 512 x 512 texture takes from click to picture and less
 * than any larger one (1024: 58 ms, 2048: 110, 4096: 225), and a texture's size is not known before its
 * file is read. There is no other delay.
 *
 * Decoded textures are kept in a small cache bounded by memory (CacheBudget), so going back to one is
 * immediate. A texture the area shows at half its size or less is previewed, in the Alpha On and Only
 * views, from the smallest of its own mip levels that is still at least the size shown (it is only ever
 * reduced), which spares the read back of the full level -- most of the time a 4096 texture takes. The
 * Alpha Off view, which shows the colour under transparent pixels, always uses level 0: a file's smaller
 * levels need not keep that colour (measured on the client's 4096 textures: as the On and Only views show
 * them they match level 0 at display size, 46.2 dB at worst; the colour alone, down to 13.5 dB). The facts stay those
 * of level 0 (the alpha read from level 0's own bytes) and Export PNG decodes level 0.
 *
 * Reading, checking and decoding are the app's own (TextureDecode, BlpInfo): a texture the decoder
 * would get wrong is described and refused, never shown. GUI thread only.
 */

#ifndef TEXTUREVIEW_H
#define TEXTUREVIEW_H

#include <functional>
#include <list>

#include <QImage>
#include <QString>

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include "BlpInfo.h"
#include "TextureCatalog.h"
#include "TextureDecode.h"
#include "TexturePreview.h"

class ModelViewer;
class wxAuiToolBar;

class TextureView : public wxPanel
{
public:
  // The selected texture, as far as it has been read.
  struct Current
  {
    bool valid = false;        // a file was read (its original can be exported)
    bool decoded = false;      // image holds its pixels (PNG export possible)
    bool isLookup = false;     // opened by a FileDataID the texture list does not have
    TextureEntry entry;        // what Browse picked (a lookup's entry is completed once it is read)
    BlpInfo info;
    QImage image;              // level imageLevel of the texture (0: full resolution)
    int imageLevel = 0;
    int width = 0;             // of level 0, as decoded
    int height = 0;
    TextureDecode::Alpha alpha; // of level 0
    int levels = 0;
    int internalFormat = 0;
    qint64 bytes = 0;
    QString error;
    bool retry = false;        // refused for now only (the decoder not ready): picking it again tries again
  };
  enum class Selection { None, Loading, Shown, Refused, Failed };

  // How much the decoded-texture cache may hold (see the .cpp).
  static const qint64 CacheBudget;

  TextureView(wxWindow * parent, ModelViewer * viewer);
  ~TextureView();

  // Select this texture: shown as soon as it is read and decoded (at once from the cache). The texture
  // already selected, picked again, keeps what is on screen (one that could not be read is tried again).
  void show(const TextureEntry & entry, bool lookup);
  // The same, read and decoded now (an export from Browse's menu needs the pixels).
  void showNow(const TextureEntry & entry, bool lookup);
  bool isCurrent(const TextureEntry & entry) const;
  const Current & current() const { return m_current; }
  Selection selection() const;

  // Textures mode is entered (the view is shown: the selection, or the empty state) or left (a decode
  // on its way waits for the next entry).
  void entered();
  void left();
  // An arrow key repeating in Browse's tree (true), or let go (false): see THE SELECTION.
  void setKeysRepeating(bool repeating);

  // For the main window: the texture's name (the command bar) and its facts (the status bar).
  wxString displayName() const;
  wxString displayPath() const;
  wxString statusFacts() const;

  // The stages of the last texture picked, up to its first picture on screen, for the log and the tests:
  // times on a steady clock (ns), durations in ms. A sharper level fetched later is not part of them.
  struct Timing
  {
    qint64 selected = 0;      // the pick reached the view
    qint64 decodeStart = 0;   // reading began
    double readMs = 0;        // the file read from the client storage
    double checkMs = 0;       // its header and mip table checked
    double decodeMs = 0;      // the decoder (upload and read back)
    double loadMs = 0;        // of that: its own read of the file and the upload of every level
    double imageMs = 0;       // facts from the pixels, the preview given the image
    double paintMs = 0;       // the first paint of the new image
    qint64 shown = 0;         // that paint ended
    bool cached = false;      // from the cache: no read, no decode
    int level = 0;            // the mip level previewed
  };
  const Timing & timing() const { return m_timing; }
  static qint64 nowNs();

  // A FileDataID looked up from Browse's search turned out to be this file of the index.
  std::function<void(int fileDataId, const QString & indexName)> onLookupResolved;

  // A client load is starting: the old client's selection and cache go. It has ended: the empty state.
  void clientLoadStarting();
  void clientLoaded();
  // Before the app's GL context goes: nothing decodes after this.
  void shutdown();

  // Export and copy; the dialogs call the first two. Messages go to the main window's status bar.
  bool exportPng(const wxString & target, wxString & message);
  bool exportBlp(const wxString & target, wxString & message);
  void exportPngInteractive();
  void exportBlpInteractive();
  void copyPath();
  void copyFileDataId();
  // The suggested file name: the texture's own name with this extension, or "<FileDataID>.<ext>"
  // for a file without a name. For the original file ("blp") of something that is not a BLP: a
  // named file keeps its own name, an unnamed one gets the index's ".unk".
  static wxString exportFileName(const TextureEntry & entry, const wxString & extension, bool blpContent = true);
  // Whether a file read is a BLP: by its header, or (too short for one) by being a named .blp.
  static bool isBlp(const Current & c);

  // The cache, for the log and the tests.
  qint64 cacheBytes() const { return m_cacheBytes; }
  int cacheEntries() const { return (int)m_cache.size(); }

private:
  void buildLayout();
  void loadSettings();
  void saveSettings();
  bool clientReady() const;
  bool decoderReady() const;
  void schedule();
  void OnIdle(wxIdleEvent & event);
  void decodeNow();
  // A larger level of the texture shown, for an area that now shows it larger than its preview level
  // (side 0: level 0, for the Alpha Off view).
  void sharpen(int side);
  void emptyState();
  // The preview's message for a texture read and not shown (refused, or the decoder not ready).
  void showReason();
  // A look-up row's file, named in Browse again when it is picked without being read.
  void lookupFound();
  void showInfo();
  wxString factsLine() const;
  void setAlphaView(TexturePreview::AlphaView view);
  bool currentIsBlp() const;
  wxString exportFolder() const;
  void status(const wxString & text);
  void selectionChanged();
  void OnCharHook(wxKeyEvent & event);

  // The decoded-texture cache: most recently used first, bounded by the bytes of the images it holds.
  static QString cacheKey(const TextureEntry & entry, bool lookup);
  const Current * cacheFind(const QString & key);
  void cachePut(const QString & key, const Current & c);
  void cacheClear();
  struct Cached
  {
    QString key;
    Current value;
    qint64 bytes = 0;
  };
  std::list<Cached> m_cache;
  qint64 m_cacheBytes = 0;

  ModelViewer * m_viewer;
  TexturePreview * m_preview = nullptr;
  wxAuiToolBar * m_tools = nullptr;
  wxChoice * m_background = nullptr;
  wxStaticText * m_name = nullptr;
  wxStaticText * m_path = nullptr;
  wxStaticText * m_facts = nullptr;
  wxButton * m_copyPath = nullptr;
  wxButton * m_copyId = nullptr;
  wxButton * m_exportPng = nullptr;
  wxButton * m_exportBlp = nullptr;

  Current m_current;
  Timing m_timing;
  TextureEntry m_pending;          // picked, to be read and decoded
  bool m_pendingLookup = false;
  bool m_decodePending = false;   // decoded at the next idle event
  bool m_keysRepeating = false;
  bool m_active = false;           // Textures mode
  bool m_shutdown = false;
  bool m_loadingSettings = false;
};

#endif // TEXTUREVIEW_H
