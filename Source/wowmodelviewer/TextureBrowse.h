/*
 * TextureBrowse.h
 *
 * Browse's "Textures (*.blp)" category: the client's textures in Browse's own tree -- FileControl's
 * wxTreeCtrl, its search box and its status line -- filled from the TextureCatalog instead of the
 * TreeStackItem hierarchy the other categories build.
 *
 * Why not that hierarchy: measured on client 12.1 (789,146 textures), building it took 2.9 s and 536 MB
 * on every entry into the category and every cleared search (it is never freed); a search for "_hd"
 * took 6.5 s; and expanding textures/bakednpctextures appended 83,178 rows in 17 s. Here:
 *   - folders are filled in when they are expanded, from the catalogue's sorted paths (TextureCatalog::
 *     list), nothing is built up front, and the catalogue is built once per client;
 *   - a folder with more than RangeSize textures shows them in ranges of RangeSize, labelled like a
 *     dictionary's guide words ("ability_d - ability_h"), so no expand adds more than about RangeSize rows;
 *   - a search runs the catalogue's search (name, path, FileDataID) and lists the first SearchShown
 *     matches in their folders, expanded, as Browse lists its search results;
 *   - files the index knows only by FileDataID sit in one group at the end, and a FileDataID typed in the
 *     search that the list does not have is offered as a look-up row;
 *   - the folders open in the tree (and its scroll position) are kept while a search or another
 *     category replaces it, and opened again when it comes back.
 *
 * Rows carry a TextureRowData (a FileTreeData with no GameFile, so Browse's handlers never take one
 * for a file of theirs). GUI thread only, like the index.
 */

#ifndef TEXTUREBROWSE_H
#define TEXTUREBROWSE_H

#include <functional>
#include <set>
#include <vector>

#include <QString>

#include <wx/treectrl.h>

// filecontrol.h relies on what its includer brought in.
#include <map>
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include "GameFile.h"
#include "filecontrol.h"
#include "TextureCatalog.h"

class TextureRowData : public FileTreeData
{
public:
  enum class Kind { Folder, Range, Texture, UnnamedGroup, Lookup };

  explicit TextureRowData(Kind k) : FileTreeData(nullptr, nullptr), kind(k) {}

  Kind kind;
  QString folder;            // Folder: its path, with the trailing '/'
  std::vector<int> textures; // Range: indices into entries()
  int entry = -1;            // Texture: index into entries(), or -1 for one known only by FileDataID
  int fileDataId = 0;        // Texture known only by FileDataID, and Lookup
  bool loaded = false;       // children added
};

class TextureBrowse
{
public:
  static const size_t RangeSize = 1000;
  static const size_t SearchShown = 500;

  explicit TextureBrowse(wxTreeCtrl * tree);

  // The catalogue of the active client, built on first use after each client load. False while no
  // client is ready (none loaded, or one loading).
  bool ensureCatalog();
  void clientLoadStarting();
  const TextureCatalog & catalog() const { return m_catalog; }

  // Fill the tree: the top folders (no search), or the search's matches in their folders. Returns the
  // line for Browse's status label (empty with no search).
  wxString populate(const QString & search);
  // Empty the tree (no client to list).
  void clear();
  // Fill a folder, range or group as it is expanded.
  void expanding(wxTreeItemId item);
  // The texture a row stands for. False for a folder, a range and a group.
  bool textureOf(wxTreeItemId item, TextureEntry & entry, bool & lookup) const;
  // Open the folders down to a texture's row and scroll to it; select it too when asked.
  bool reveal(const TextureEntry & entry, bool select);
  // A look-up from the search was read: list a file known only by FileDataID where it belongs, and
  // give the look-up row its name.
  void lookupResolved(int fileDataId, const QString & indexName);
  // True while the tree is being rebuilt or revealed: selection changes then are not picks.
  bool busy() const { return m_busy > 0; }
  // Browse is about to show another category in the tree: the folders open now (and the row at the
  // top) are kept, for the next populate without a search.
  void rememberOpenFolders();
  // The tree holds this category's search results.
  bool showsSearch() const { return m_treeIsOurs && m_treeIsSearch; }

private:
  wxTreeItemId addFolder(wxTreeItemId parent, const QString & path, const QString & name, int depth);
  wxTreeItemId addTexture(wxTreeItemId parent, int entry, int depth);
  wxTreeItemId addUnnamed(wxTreeItemId parent, int fileDataId);
  void addTextures(wxTreeItemId parent, const std::vector<int> & textures, int depth);
  void addUnnamedGroup(wxTreeItemId parent);
  QString label(const QString & segment, int depth) const;
  QString textureLabel(const TextureEntry & e, int depth) const;
  wxTreeItemId findChild(wxTreeItemId parent, const std::function<bool(const TextureRowData &)> & match) const;
  // Back to the tree's left edge: showing a deep row scrolls the native tree sideways to fit its whole
  // label, which hides the rows above it.
  void scrollLeft();
  // A row's identity across rebuilds ("" for one that has none).
  QString rowKey(const TextureRowData & d) const;
  void collectOpen(wxTreeItemId parent);
  void reopen(wxTreeItemId parent, wxTreeItemId & top, wxTreeItemId & picked);

  struct Busy
  {
    explicit Busy(TextureBrowse & b) : browse(b) { browse.m_busy++; }
    ~Busy() { browse.m_busy--; }
    TextureBrowse & browse;
  };

  wxTreeCtrl * m_tree;
  TextureCatalog m_catalog;
  bool m_catalogCurrent = false;
  TextureCatalog::SearchResult m_lastResult;   // narrowed when the next search extends it
  int m_busy = 0;
  bool m_treeIsOurs = false;       // the tree holds this category's rows
  bool m_treeIsSearch = false;     // ... and they are a search's results
  std::set<QString> m_openRows;    // rowKey of the rows open when the browsing tree was last replaced
  QString m_topRow;                // rowKey of its first visible row
  QString m_pickedRow;             // rowKey of its selected row
};

#endif // TEXTUREBROWSE_H
