/*
 * TextureCatalog.cpp -- see TextureCatalog.h.
 */

#include "TextureCatalog.h"

#include <algorithm>

#include <QElapsedTimer>

#include "Game.h"
#include "GameFile.h"
#include "GameFolder.h"
#include "UnityAssetAccess.h"
#include "logger/Logger.h"

namespace
{
  bool byPath(const TextureEntry & e, const QString & path) { return e.path < path; }

  // Whether 'what' (not empty) occurs inside text[from, to).
  bool occurs(const std::string & text, size_t from, size_t to, const std::string & what)
  {
    if (to - from < what.size())
      return false;
    const char * end = text.data() + to;
    return std::search(text.data() + from, end, what.begin(), what.end()) != end;
  }
}

void TextureCatalog::clear()
{
  m_entries.clear();
  m_entries.shrink_to_fit();
  m_unnamed.clear();
  m_byId.clear();
  m_byId.shrink_to_fit();
  m_text.clear();
  m_text.shrink_to_fit();
  m_textStart.clear();
  m_textStart.shrink_to_fit();
  m_textName.clear();
  m_textName.shrink_to_fit();
  m_indexFiles = 0;
  m_buildMs = 0;
  m_generation++;
}

bool TextureCatalog::rebuild()
{
  clear();
  if (!UnityAssetAccess::hasActiveClient())
    return false;

  QElapsedTimer timer;
  timer.start();

  core::GameFolder & dir = GAMEDIRECTORY;
  m_hasFileDataIds = dir.clientProfile().hasFileDataId;
  const std::map<QString, GameFile *> & files = dir.filesByPath();
  m_indexFiles = files.size();

  static const QString blp(".blp"), unkPrefix("File"), unkSuffix(".unk");
  for (const auto & item : files)
  {
    const QString & path = item.first;
    GameFile * file = item.second;
    if (!file)
      continue;
    if (path.endsWith(blp))
    {
      TextureEntry e;
      e.path = path;
      e.fileDataId = file->fileDataId();
      e.nameStart = path.lastIndexOf('/') + 1;
      m_entries.push_back(e);
    }
    // What the index calls a file it opened by FileDataID with no name for it (WoWFolder::getFile).
    else if (path.startsWith(unkPrefix) && path.endsWith(unkSuffix) && file->fileDataId() > 0)
    {
      TextureEntry e;
      e.path = path;
      e.fileDataId = file->fileDataId();
      e.unnamed = true;
      m_unnamed.push_back(e);
    }
  }

  std::sort(m_unnamed.begin(), m_unnamed.end(),
            [](const TextureEntry & a, const TextureEntry & b) { return a.fileDataId < b.fileDataId; });

  if (m_hasFileDataIds)
  {
    m_byId.reserve(m_entries.size());
    for (size_t i = 0; i < m_entries.size(); i++)
      if (m_entries[i].fileDataId > 0)
        m_byId.emplace_back(m_entries[i].fileDataId, (int)i);
    std::sort(m_byId.begin(), m_byId.end());
  }

  // The search's text. A path in plain ASCII (nearly all) is its own UTF-8, copied a character at a
  // time; '/' is one byte in UTF-8, so the file name starts after the last one either way.
  m_text.reserve(m_entries.size() * 64);
  m_textStart.reserve(m_entries.size() + 1);
  m_textName.reserve(m_entries.size());
  for (const TextureEntry & e : m_entries)
  {
    const int start = (int)m_text.size();
    m_textStart.push_back(start);
    const QChar * c = e.path.constData();
    const int n = e.path.size();
    int i = 0;
    while (i < n && c[i].unicode() < 0x80)
      i++;
    if (i == n)
    {
      for (i = 0; i < n; i++)
        m_text.push_back((char)c[i].unicode());
      m_textName.push_back(start + e.nameStart);
    }
    else
    {
      const QByteArray utf8 = e.path.toUtf8();
      m_text.append(utf8.constData(), (size_t)utf8.size());
      m_textName.push_back(start + utf8.lastIndexOf('/') + 1);
    }
    m_text.push_back('\n');
  }
  m_textStart.push_back((int)m_text.size());

  m_buildMs = timer.elapsed();
  LOG_INFO << "[texview] listed" << (qulonglong)m_entries.size() << "textures and" << (qulonglong)m_unnamed.size()
           << "files known only by FileDataID, from" << (qulonglong)m_indexFiles << "index entries, in"
           << m_buildMs << "ms";
  return true;
}

TextureCatalog::Listing TextureCatalog::list(const QString & folder) const
{
  Listing out;
  const size_t n = m_entries.size();
  size_t i = std::lower_bound(m_entries.begin(), m_entries.end(), folder, byPath) - m_entries.begin();
  while (i < n && m_entries[i].path.startsWith(folder))
  {
    const QString & path = m_entries[i].path;
    const int slash = path.indexOf('/', folder.size());
    if (slash < 0)
    {
      out.textures.push_back((int)i);
      i++;
      continue;
    }
    // Everything under "<folder><sub>/" sorts below "<folder><sub>0" ('0' follows '/'): skip there.
    const QString stop = path.left(slash) + QChar(ushort('/' + 1));
    const size_t end = std::lower_bound(m_entries.begin() + i, m_entries.end(), stop, byPath) - m_entries.begin();
    Folder f;
    f.path = path.left(slash + 1);
    f.name = path.mid(folder.size(), slash - folder.size());
    f.textures = (int)(end - i);
    out.folders.push_back(f);
    i = end;
  }
  return out;
}

QString TextureCatalog::normalise(const QString & text)
{
  return text.trimmed().toLower().replace('\\', '/');
}

TextureCatalog::SearchResult TextureCatalog::search(const QString & query, const SearchResult * previous) const
{
  SearchResult r;
  r.query = normalise(query);
  const QStringList tokens = r.query.split(' ', QString::SkipEmptyParts);
  if (tokens.isEmpty())
    return r;

  bool digits = !r.query.isEmpty();
  for (const QChar c : r.query)
    digits = digits && c.isDigit();
  if (digits && m_hasFileDataIds)
  {
    bool ok = false;
    const int id = r.query.toInt(&ok);
    if (ok && id > 0)
    {
      r.exactId = id;
      r.exactIndex = findId(id);
      if (r.exactIndex < 0)
      {
        auto it = std::lower_bound(m_unnamed.begin(), m_unnamed.end(), id,
                                   [](const TextureEntry & e, int v) { return e.fileDataId < v; });
        if (it != m_unnamed.end() && it->fileDataId == id)
        {
          r.exactIndex = (int)(it - m_unnamed.begin());
          r.exactInUnnamed = true;
        }
      }
    }
  }

  // The tokens in the search text's encoding. UTF-8 is matched byte for byte: a valid sequence can
  // only match at a character boundary, so this finds what a QString search would.
  struct Token
  {
    std::string text;
    bool path;   // has a '/': looked for in the whole path only
  };
  std::vector<Token> want;
  for (const QString & token : tokens)
  {
    const QByteArray utf8 = token.toUtf8();
    want.push_back({ std::string(utf8.constData(), (size_t)utf8.size()), token.contains('/') });
  }

  std::vector<int> names, paths;
  auto consider = [&](int index)
  {
    const size_t start = (size_t)m_textStart[index];
    const size_t end = (size_t)m_textStart[index + 1] - 1;  // before its '\n'
    const size_t name = (size_t)m_textName[index];
    bool inName = true;
    for (const Token & token : want)
    {
      if (token.path)
      {
        if (!occurs(m_text, start, end, token.text))
          return;
        continue;
      }
      if (occurs(m_text, name, end, token.text))
        continue;
      if (!occurs(m_text, start, end, token.text))
        return;
      inName = false;
    }
    if (!r.exactInUnnamed && index == r.exactIndex)
      return; // pinned above the matches already
    (inName ? names : paths).push_back(index);
  };

  // Typing more of the same query can only narrow it.
  if (previous && !previous->query.isEmpty() && r.query.startsWith(previous->query))
  {
    for (int index : previous->matches)
      consider(index);
    if (!previous->exactInUnnamed && previous->exactIndex >= 0)
      consider(previous->exactIndex);
    // The narrowed set is in previous order (names, then paths); restore path order inside each group.
    std::sort(names.begin(), names.end());
    std::sort(paths.begin(), paths.end());
  }
  else
  {
    // One pass over the search text for the longest token, which every match contains; each entry
    // it occurs in is then checked against the whole query, once.
    const Token * scan = &want.front();
    for (const Token & token : want)
      if (token.text.size() > scan->text.size())
        scan = &token;
    const int count = (int)m_entries.size();
    int index = 0;
    size_t at = 0;
    while (index < count && (at = m_text.find(scan->text.data(), at, scan->text.size())) != std::string::npos)
    {
      while (m_textStart[index + 1] <= (int)at)
        index++;
      consider(index);
      at = (size_t)m_textStart[index + 1];
      index++;
    }
  }

  r.nameMatches = (int)names.size();
  r.matches.reserve(names.size() + paths.size());
  r.matches.insert(r.matches.end(), names.begin(), names.end());
  r.matches.insert(r.matches.end(), paths.begin(), paths.end());
  return r;
}

int TextureCatalog::findPath(const QString & path) const
{
  const QString key = normalise(path);
  auto it = std::lower_bound(m_entries.begin(), m_entries.end(), key, byPath);
  if (it != m_entries.end() && it->path == key)
    return (int)(it - m_entries.begin());
  return -1;
}

bool TextureCatalog::addUnnamed(int fileDataId, const QString & indexName)
{
  if (fileDataId <= 0 || findId(fileDataId) >= 0)
    return false;
  auto it = std::lower_bound(m_unnamed.begin(), m_unnamed.end(), fileDataId,
                             [](const TextureEntry & e, int v) { return e.fileDataId < v; });
  if (it != m_unnamed.end() && it->fileDataId == fileDataId)
    return false;
  TextureEntry e;
  e.path = indexName;
  e.fileDataId = fileDataId;
  e.unnamed = true;
  m_unnamed.insert(it, e);
  return true;
}

int TextureCatalog::findId(int fileDataId) const
{
  auto it = std::lower_bound(m_byId.begin(), m_byId.end(), std::make_pair(fileDataId, -1));
  if (it != m_byId.end() && it->first == fileDataId)
    return it->second;
  return -1;
}
