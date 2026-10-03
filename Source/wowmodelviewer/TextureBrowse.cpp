/*
 * TextureBrowse.cpp -- see TextureBrowse.h.
 */

#include "TextureBrowse.h"

#include <algorithm>
#include <map>

#include <QElapsedTimer>

#include <wx/numformatter.h>
#ifdef __WXMSW__
  #include <wx/msw/wrapwin.h>
#endif

#include "UnityAssetAccess.h"
#include "logger/Logger.h"

namespace
{
  wxString wx(const QString & s) { return wxString(s.toStdWString()); }
  wxString thousands(size_t n) { return wxNumberFormatter::ToString((long)n); }
  const wxString GuideDash(L" \u2013 ");

  QString withoutBlp(QString s)
  {
    if (s.endsWith(".blp"))
      s.chop(4);
    return s;
  }

  // A range's guide word: the shortest start of 'name' that tells it apart from 'neighbour' (the name
  // just outside the range), or the whole name, without ".blp", when that is what it takes.
  QString guideWord(const QString & name, const QString & neighbour)
  {
    int n = 0;
    while (n < name.size() && n < neighbour.size() && name[n] == neighbour[n])
      n++;
    const QString base = withoutBlp(name);
    return n + 1 >= base.size() ? base : name.left(n + 1);
  }

  // A search's matches as a small folder tree, to be listed folders first, as browsing lists them.
  struct ResultFolder
  {
    std::map<QString, ResultFolder> folders;   // by name
    std::vector<int> textures;                 // indices into entries(), in path order
  };
}

TextureBrowse::TextureBrowse(wxTreeCtrl * tree) : m_tree(tree)
{
}

bool TextureBrowse::ensureCatalog()
{
  if (UnityAssetAccess::isClientLoading() || !UnityAssetAccess::hasActiveClient())
    return false;
  if (m_catalogCurrent)
    return true;
  wxBusyCursor busy;
  m_catalogCurrent = m_catalog.rebuild();
  m_lastResult = TextureCatalog::SearchResult();
  return m_catalogCurrent;
}

void TextureBrowse::clientLoadStarting()
{
  m_catalog.clear();
  m_catalogCurrent = false;
  m_lastResult = TextureCatalog::SearchResult();
  m_openRows.clear();
  m_topRow.clear();
  m_pickedRow.clear();
  m_treeIsOurs = false;
}

// ------------------------------------------------------------------------------------------ rows

// Browse's own labels (beautifyFileName in filecontrol.cpp): the first two levels start with a
// capital, everything below is as the file list spells it.
QString TextureBrowse::label(const QString & segment, int depth) const
{
  QString s = segment;
  if (depth <= 1 && !s.isEmpty())
    s[0] = s[0].toUpper();
  return s;
}

QString TextureBrowse::textureLabel(const TextureEntry & e, int depth) const
{
  QString s = depth < 0 ? e.path : label(e.name(), depth);
  if (e.fileDataId > 0 && m_catalog.hasFileDataIds())
    s += QString(" [%1]").arg(e.fileDataId);
  return s;
}

wxTreeItemId TextureBrowse::addFolder(wxTreeItemId parent, const QString & path, const QString & name, int depth)
{
  TextureRowData * data = new TextureRowData(TextureRowData::Kind::Folder);
  data->folder = path;
  const wxTreeItemId item = m_tree->AppendItem(parent, wx(label(name, depth)), -1, -1, data);
  m_tree->SetItemHasChildren(item, true);
  return item;
}

wxTreeItemId TextureBrowse::addTexture(wxTreeItemId parent, int entry, int depth)
{
  TextureRowData * data = new TextureRowData(TextureRowData::Kind::Texture);
  data->entry = entry;
  data->fileDataId = m_catalog.entries()[entry].fileDataId;
  return m_tree->AppendItem(parent, wx(textureLabel(m_catalog.entries()[entry], depth)), -1, -1, data);
}

wxTreeItemId TextureBrowse::addUnnamed(wxTreeItemId parent, int fileDataId)
{
  TextureRowData * data = new TextureRowData(TextureRowData::Kind::Texture);
  data->fileDataId = fileDataId;
  return m_tree->AppendItem(parent, wxString::Format(_("FileDataID %d"), fileDataId), -1, -1, data);
}

void TextureBrowse::addTextures(wxTreeItemId parent, const std::vector<int> & textures, int depth)
{
  if (textures.size() <= RangeSize)
  {
    for (int index : textures)
      addTexture(parent, index, depth);
    return;
  }
  const std::vector<TextureEntry> & entries = m_catalog.entries();
  const size_t n = textures.size();
  for (size_t first = 0; first < n; first += RangeSize)
  {
    const size_t last = std::min(n, first + RangeSize) - 1;
    const QString firstName = entries[textures[first]].name(), lastName = entries[textures[last]].name();
    const QString before = first > 0 ? entries[textures[first - 1]].name() : lastName;
    const QString after = last + 1 < n ? entries[textures[last + 1]].name() : firstName;
    TextureRowData * data = new TextureRowData(TextureRowData::Kind::Range);
    data->textures.assign(textures.begin() + first, textures.begin() + last + 1);
    const wxTreeItemId item =
      m_tree->AppendItem(parent, wx(guideWord(firstName, before)) + GuideDash + wx(guideWord(lastName, after)), -1, -1, data);
    m_tree->SetItemHasChildren(item, true);
  }
}

void TextureBrowse::addUnnamedGroup(wxTreeItemId parent)
{
  TextureRowData * data = new TextureRowData(TextureRowData::Kind::UnnamedGroup);
  const wxTreeItemId item = m_tree->AppendItem(parent, _("Files known only by FileDataID"), -1, -1, data);
  m_tree->SetItemHasChildren(item, true);
}

void TextureBrowse::scrollLeft()
{
#ifdef __WXMSW__
  ::SendMessage((HWND)m_tree->GetHWND(), WM_HSCROLL, MAKEWPARAM(SB_LEFT, 0), 0);
#endif
}

wxTreeItemId TextureBrowse::findChild(wxTreeItemId parent, const std::function<bool(const TextureRowData &)> & match) const
{
  wxTreeItemIdValue cookie;
  for (wxTreeItemId child = m_tree->GetFirstChild(parent, cookie); child.IsOk(); child = m_tree->GetNextChild(parent, cookie))
  {
    const TextureRowData * data = dynamic_cast<const TextureRowData *>(m_tree->GetItemData(child));
    if (data && match(*data))
      return child;
  }
  return wxTreeItemId();
}

// ------------------------------------------------------------------------------------- filling in

void TextureBrowse::clear()
{
  rememberOpenFolders();
  Busy guard(*this);
  m_tree->DeleteAllItems();
}

// ------------------------------------------------------------------------- the browsing tree kept

QString TextureBrowse::rowKey(const TextureRowData & d) const
{
  switch (d.kind)
  {
    case TextureRowData::Kind::Folder: return QString("F") + d.folder;
    case TextureRowData::Kind::Range: return d.textures.empty() ? QString() : QString("R%1").arg(d.textures.front());
    case TextureRowData::Kind::UnnamedGroup: return QString("U");
    case TextureRowData::Kind::Texture: return d.entry >= 0 ? QString("T%1").arg(d.entry) : QString("N%1").arg(d.fileDataId);
    default: return QString();
  }
}

void TextureBrowse::collectOpen(wxTreeItemId parent)
{
  wxTreeItemIdValue cookie;
  for (wxTreeItemId child = m_tree->GetFirstChild(parent, cookie); child.IsOk(); child = m_tree->GetNextChild(parent, cookie))
  {
    const TextureRowData * data = dynamic_cast<const TextureRowData *>(m_tree->GetItemData(child));
    if (!data || !m_tree->IsExpanded(child))
      continue;
    m_openRows.insert(rowKey(*data));
    collectOpen(child);
  }
}

void TextureBrowse::rememberOpenFolders()
{
  // Only the browsing tree: a search's results are opened by the search itself.
  if (m_treeIsOurs && !m_treeIsSearch && m_tree->GetRootItem().IsOk())
  {
    m_openRows.clear();
    collectOpen(m_tree->GetRootItem());
    const wxTreeItemId first = m_tree->GetFirstVisibleItem();
    const TextureRowData * data = first.IsOk() ? dynamic_cast<const TextureRowData *>(m_tree->GetItemData(first)) : nullptr;
    m_topRow = data ? rowKey(*data) : QString();
    const wxTreeItemId chosen = m_tree->GetSelection();
    const TextureRowData * picked = chosen.IsOk() ? dynamic_cast<const TextureRowData *>(m_tree->GetItemData(chosen)) : nullptr;
    m_pickedRow = picked ? rowKey(*picked) : QString();
  }
  m_treeIsOurs = false;
}

void TextureBrowse::reopen(wxTreeItemId parent, wxTreeItemId & top, wxTreeItemId & picked)
{
  wxTreeItemIdValue cookie;
  for (wxTreeItemId child = m_tree->GetFirstChild(parent, cookie); child.IsOk(); child = m_tree->GetNextChild(parent, cookie))
  {
    const TextureRowData * data = dynamic_cast<const TextureRowData *>(m_tree->GetItemData(child));
    if (!data)
      continue;
    const QString key = rowKey(*data);
    if (!m_topRow.isEmpty() && key == m_topRow)
      top = child;
    if (!m_pickedRow.isEmpty() && key == m_pickedRow)
      picked = child;
    if (m_openRows.count(key))
    {
      m_tree->Expand(child);   // filled by expanding()
      reopen(child, top, picked);
    }
  }
}

// ------------------------------------------------------------------------------------- filling in

wxString TextureBrowse::populate(const QString & search)
{
  // The browsing tree being replaced (by a search, or the same tree again) is kept for its return.
  if (m_treeIsOurs && !m_treeIsSearch)
    rememberOpenFolders();
  Busy guard(*this);
  QElapsedTimer timer;
  timer.start();
  m_tree->Freeze();
  m_tree->DeleteAllItems();
  const wxTreeItemId root = m_tree->AddRoot(wxT("Root"));
  const QString query = TextureCatalog::normalise(search);
  wxString status;
  m_treeIsOurs = true;
  m_treeIsSearch = !query.isEmpty();

  if (query.isEmpty())
  {
    m_lastResult = TextureCatalog::SearchResult();
    const TextureCatalog::Listing top = m_catalog.list(QString());
    for (const TextureCatalog::Folder & f : top.folders)
      addFolder(root, f.path, f.name, 0);
    addTextures(root, top.textures, 0);
    if (!m_catalog.unnamed().empty())
      addUnnamedGroup(root);
    // As it was left: the same folders open, the same row picked (busy: not picked again), the same row
    // at the top.
    wxTreeItemId topRow, picked;
    reopen(root, topRow, picked);
    if (picked.IsOk())
      m_tree->SelectItem(picked);
    if (topRow.IsOk())
    {
      m_tree->ScrollTo(topRow);
      scrollLeft();
    }
    m_tree->Thaw();
    LOG_INFO << "[texview] browse tree:" << (qulonglong)top.folders.size() << "folders at the top," << (qulonglong)m_openRows.size()
             << "opened again, in" << timer.elapsed() << "ms";
    return status;
  }

  // Typing more of the same query only narrows the last result.
  const bool narrows = !m_lastResult.query.isEmpty() && query.startsWith(m_lastResult.query);
  const TextureCatalog::SearchResult result = m_catalog.search(query, narrows ? &m_lastResult : nullptr);
  m_lastResult = result;
  const qint64 searchMs = timer.elapsed();

  // The FileDataID typed, first: the texture with it, or a row that looks it up in the client.
  size_t found = result.matches.size();
  if (result.exactId > 0)
  {
    if (result.exactIndex >= 0 && !result.exactInUnnamed)
    {
      addTexture(root, result.exactIndex, -1);
      found++;
    }
    else if (result.exactIndex >= 0)
    {
      addUnnamed(root, m_catalog.unnamed()[result.exactIndex].fileDataId);
      found++;
    }
    else if (m_catalog.hasFileDataIds())
    {
      TextureRowData * data = new TextureRowData(TextureRowData::Kind::Lookup);
      data->fileDataId = result.exactId;
      m_tree->AppendItem(root, wxString::Format(_("Look up FileDataID %d"), result.exactId), -1, -1, data);
    }
  }

  // The first matches -- name matches before path matches -- in their folders, folders first.
  std::vector<int> shown(result.matches.begin(), result.matches.begin() + std::min(result.matches.size(), SearchShown));
  std::sort(shown.begin(), shown.end());
  ResultFolder top;
  const std::vector<TextureEntry> & entries = m_catalog.entries();
  for (int index : shown)
  {
    ResultFolder * at = &top;
    const QString folder = entries[index].folder();
    for (const QString & segment : folder.split('/', QString::SkipEmptyParts))
      at = &at->folders[segment];
    at->textures.push_back(index);
  }
  std::function<void(const ResultFolder &, wxTreeItemId, const QString &, int)> addResults =
    [&](const ResultFolder & node, wxTreeItemId parent, const QString & prefix, int depth) {
      for (const auto & sub : node.folders)
      {
        const QString path = prefix + sub.first + "/";
        const wxTreeItemId item = addFolder(parent, path, sub.first, depth);
        static_cast<TextureRowData *>(m_tree->GetItemData(item))->loaded = true;   // the matches only
        addResults(sub.second, item, path, depth + 1);
      }
      for (int index : node.textures)
        addTexture(parent, index, depth);
    };
  addResults(top, root, QString(), 0);
  m_tree->ExpandAll();
  wxTreeItemIdValue cookie;
  const wxTreeItemId first = m_tree->GetFirstChild(root, cookie);
  if (first.IsOk())
    m_tree->ScrollTo(first);
  scrollLeft();
  m_tree->Thaw();

  if (found == 0)
    status = result.exactId > 0 && m_catalog.hasFileDataIds()
               ? wxString::Format(_("No texture has FileDataID %d in the file list: select the row to look it up."),
                                  result.exactId)
               : wxString(_("No textures match."));
  else if (result.matches.size() > SearchShown)
    status = wxString::Format(_("%s textures found; the first %s are listed. Type more to narrow the search."),
                              thousands(found), thousands(SearchShown));
  else
    status = found == 1 ? wxString(_("1 texture found")) : wxString::Format(_("%s textures found"), thousands(found));
  LOG_INFO << "[texview] search" << query << "->" << (qulonglong)found << "matches in" << searchMs << "ms, tree"
           << (timer.elapsed() - searchMs) << "ms";
  return status;
}

void TextureBrowse::expanding(wxTreeItemId item)
{
  TextureRowData * data = item.IsOk() ? dynamic_cast<TextureRowData *>(m_tree->GetItemData(item)) : nullptr;
  if (!data || data->loaded || !m_catalogCurrent)
    return;
  data->loaded = true;
  Busy guard(*this);
  QElapsedTimer timer;
  timer.start();
  m_tree->Freeze();
  switch (data->kind)
  {
    case TextureRowData::Kind::Folder:
    {
      const TextureCatalog::Listing listing = m_catalog.list(data->folder);
      const int depth = data->folder.count('/');
      for (const TextureCatalog::Folder & f : listing.folders)
        addFolder(item, f.path, f.name, depth);
      addTextures(item, listing.textures, depth);
      break;
    }
    case TextureRowData::Kind::Range:
      for (int index : data->textures)
        addTexture(item, index, m_catalog.entries()[index].path.count('/'));
      break;
    case TextureRowData::Kind::UnnamedGroup:
      for (const TextureEntry & e : m_catalog.unnamed())
        addUnnamed(item, e.fileDataId);
      break;
    default:
      break;
  }
  m_tree->Thaw();
  LOG_INFO << "[texview] expanded" << (data->kind == TextureRowData::Kind::Folder ? data->folder : QString("a range or group"))
           << ":" << (qulonglong)m_tree->GetChildrenCount(item, false) << "rows in" << timer.elapsed() << "ms";
}

// ------------------------------------------------------------------------------------- picking rows

bool TextureBrowse::textureOf(wxTreeItemId item, TextureEntry & entry, bool & lookup) const
{
  const TextureRowData * data = item.IsOk() ? dynamic_cast<const TextureRowData *>(m_tree->GetItemData(item)) : nullptr;
  if (!data || !m_catalogCurrent)
    return false;
  lookup = false;
  if (data->kind == TextureRowData::Kind::Lookup)
  {
    entry = TextureEntry();
    entry.fileDataId = data->fileDataId;
    entry.unnamed = true;
    lookup = true;
    return true;
  }
  if (data->kind != TextureRowData::Kind::Texture)
    return false;
  if (data->entry >= 0)
  {
    if ((size_t)data->entry >= m_catalog.entries().size())
      return false;
    entry = m_catalog.entries()[data->entry];
    return true;
  }
  entry = TextureEntry();
  entry.fileDataId = data->fileDataId;
  entry.unnamed = true;
  const std::vector<TextureEntry> & unnamed = m_catalog.unnamed();
  auto it = std::lower_bound(unnamed.begin(), unnamed.end(), data->fileDataId,
                             [](const TextureEntry & e, int id) { return e.fileDataId < id; });
  if (it != unnamed.end() && it->fileDataId == data->fileDataId)
    entry = *it;
  return true;
}

bool TextureBrowse::reveal(const TextureEntry & entry, bool select)
{
  if (!m_catalogCurrent)
    return false;
  Busy guard(*this);
  wxTreeItemId at = m_tree->GetRootItem();
  if (!at.IsOk())
    return false;
  wxTreeItemId row;
  if (entry.unnamed)
  {
    const wxTreeItemId group = findChild(at, [](const TextureRowData & d) { return d.kind == TextureRowData::Kind::UnnamedGroup; });
    if (!group.IsOk())
      return false;
    m_tree->Expand(group);
    const int id = entry.fileDataId;
    row = findChild(group, [id](const TextureRowData & d) { return d.kind == TextureRowData::Kind::Texture && d.fileDataId == id; });
  }
  else
  {
    const int index = m_catalog.findPath(entry.path);
    if (index < 0)
      return false;
    const QString folder = entry.folder();
    for (int slash = folder.indexOf('/'); slash >= 0; slash = folder.indexOf('/', slash + 1))
    {
      const QString prefix = folder.left(slash + 1);
      at = findChild(at, [&prefix](const TextureRowData & d) { return d.kind == TextureRowData::Kind::Folder && d.folder == prefix; });
      if (!at.IsOk())
        return false;
      m_tree->Expand(at);
    }
    auto isRow = [index](const TextureRowData & d) { return d.kind == TextureRowData::Kind::Texture && d.entry == index; };
    row = findChild(at, isRow);
    if (!row.IsOk())
    {
      const wxTreeItemId range = findChild(at, [index](const TextureRowData & d) {
        return d.kind == TextureRowData::Kind::Range && std::binary_search(d.textures.begin(), d.textures.end(), index);
      });
      if (range.IsOk())
      {
        m_tree->Expand(range);
        row = findChild(range, isRow);
      }
    }
  }
  if (!row.IsOk())
    return false;
  if (select)
    m_tree->SelectItem(row);
  // Its folder (or range) at the top with the row below it, and the tree at its left edge.
  m_tree->ScrollTo(m_tree->GetItemParent(row));
  m_tree->EnsureVisible(row);
  scrollLeft();
  return true;
}

void TextureBrowse::lookupResolved(int fileDataId, const QString & indexName)
{
  if (!m_catalogCurrent)
    return;
  const bool unnamed = indexName.startsWith("File") && indexName.endsWith(".unk");
  if (unnamed)
    m_catalog.addUnnamed(fileDataId, indexName);
  const wxTreeItemId root = m_tree->GetRootItem();
  if (!root.IsOk())
    return;
  const wxTreeItemId row = findChild(root, [fileDataId](const TextureRowData & d) {
    return d.kind == TextureRowData::Kind::Lookup && d.fileDataId == fileDataId;
  });
  if (!row.IsOk())
    return;
  if (unnamed)
  {
    // Now one of the files known only by FileDataID: picked again, it is shown, not looked up.
    static_cast<TextureRowData *>(m_tree->GetItemData(row))->kind = TextureRowData::Kind::Texture;
    m_tree->SetItemText(row, wxString::Format(_("FileDataID %d"), fileDataId));
  }
  else
    m_tree->SetItemText(row, wx(indexName) + wxString::Format(wxT(" [%d]"), fileDataId));
}
