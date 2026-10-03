/*
 * TextureCatalog.h
 *
 * The Texture Viewer's list of the active client's BLP textures: a view of the app's own file
 * index (GameFolder::filesByPath, the map every by-name lookup uses), kept as path strings shared
 * with that index and FileDataIDs, in path order. It is not a second index: it holds no files and
 * reads nothing, it is rebuilt from the index whenever a client is loaded, and rows are resolved
 * back to the index's file objects only at the moment they are used.
 *
 * Also the folder listing (sub-folders, then the folder's own textures, from ranges of the sorted
 * paths) and the search (plain lower-case substring matching on names and paths, plus an exact
 * FileDataID, over a contiguous UTF-8 copy of the paths made with the list). Files the index knows only by FileDataID -- named "File%08x.unk" by the index,
 * added when something opened them by id -- are listed apart: whether one is a texture is only
 * known once its bytes are read.
 *
 * GUI thread only, like the index it reads.
 */

#ifndef TEXTURECATALOG_H
#define TEXTURECATALOG_H

#include <QString>
#include <string>
#include <utility>
#include <vector>

struct TextureEntry
{
  QString path;          // full path as the index spells it (shared with it, not copied)
  int fileDataId = -1;   // -1 on clients without FileDataIDs
  int nameStart = 0;     // where the file name starts in path
  bool unnamed = false;  // known only by FileDataID ("File%08x.unk" in the index)

  QString name() const { return path.mid(nameStart); }
  QString folder() const { return path.left(nameStart); }
};

class TextureCatalog
{
public:
  struct Folder
  {
    QString path;   // with the trailing '/'
    QString name;   // last segment, without '/'
    int textures = 0;  // textures below it, at any depth
  };

  struct Listing
  {
    std::vector<Folder> folders;  // sub-folders, in path order
    std::vector<int> textures;    // indices into entries(), the folder's own textures
  };

  struct SearchResult
  {
    QString query;               // the normalised query this result is for
    std::vector<int> matches;    // indices into entries(): name matches first, then path-only ones
    int nameMatches = 0;         // how many of matches are name matches
    int exactId = 0;             // the query as a FileDataID when it is all digits, else 0
    int exactIndex = -1;         // entry with that FileDataID (named or unnamed list), or -1
    bool exactInUnnamed = false; // exactIndex is into unnamed()
  };

  // Rebuild from the active client's index. Returns false when no client is loaded.
  bool rebuild();
  void clear();
  bool empty() const { return m_entries.empty() && m_unnamed.empty(); }

  // Bumped on every rebuild and clear: anything kept from an earlier list (a thumbnail, a pending
  // request) is stale when its generation differs.
  int generation() const { return m_generation; }
  bool hasFileDataIds() const { return m_hasFileDataIds; }
  qint64 buildMs() const { return m_buildMs; }
  size_t indexFiles() const { return m_indexFiles; }

  const std::vector<TextureEntry> & entries() const { return m_entries; }
  const std::vector<TextureEntry> & unnamed() const { return m_unnamed; }

  // folder: "" for the root, else a path ending in '/'.
  Listing list(const QString & folder) const;

  // Lower-case, '\' to '/', trimmed: how queries and paths are compared.
  static QString normalise(const QString & text);

  // Every space-separated token must occur. A token with '/' is looked for in the full path, any
  // other first in the file name (a name match) and else in the path. A query of digits also pins
  // the entry with that FileDataID, if the list has one. 'previous', when it is the result for a
  // query this one extends, is narrowed instead of scanning everything again.
  SearchResult search(const QString & query, const SearchResult * previous = nullptr) const;

  // Index of the entry with this path (exact, normalised), or -1.
  int findPath(const QString & path) const;
  // Index of the entry with this FileDataID in entries(), or -1.
  int findId(int fileDataId) const;

  // A file the index has just found by FileDataID (a look-up from the search): list it with the
  // files known only by FileDataID, as a rebuild would. True when it was added.
  bool addUnnamed(int fileDataId, const QString & indexName);

private:
  std::vector<TextureEntry> m_entries;              // named .blp files, in path order
  std::vector<TextureEntry> m_unnamed;              // File%08x.unk, by FileDataID
  std::vector<std::pair<int, int>> m_byId;          // (FileDataID, index into m_entries), sorted
  // The search's copy of the paths: UTF-8, one after another, each ended by '\n'. One block is
  // scanned many times faster than 789,000 separately allocated strings are visited.
  std::string m_text;
  std::vector<int> m_textStart;                     // where entry i starts in m_text; then its size
  std::vector<int> m_textName;                      // where entry i's file name starts in m_text
  int m_generation = 0;
  bool m_hasFileDataIds = false;
  qint64 m_buildMs = 0;
  size_t m_indexFiles = 0;
};

#endif // TEXTURECATALOG_H
