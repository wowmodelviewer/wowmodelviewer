/*
 * WoWFolder.cpp
 *
 *  Created on: 7 Aug. 2017
 *      Author: Jeromnimo
 */

#include "WoWFolder.h"

#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>

#include "CASCFile.h"
#include "CascFileProvider.h"
#include "Game.h"
#include "HardDriveFile.h"
#include "Listfile.h"
#include "LoadTimeline.h"
#include "MpqFile.h"
#include "MpqFileProvider.h"
#include "WotlkDbc.h"

#include "logger/Logger.h"

wow::WoWFolder::WoWFolder(const QString & path)
  : GameFolder(path)
{
}

void wow::WoWFolder::init()
{
  m_CASCFolder.init(path());
}


void wow::WoWFolder::initFromListfile(const QString & filename)
{
  // The listfile is read once per session (Listfile): it names the files of every client alike. Which of them this
  // client has is decided here, from its own storage.
  bool reused = false;
  std::shared_ptr<const Listfile> list =
    Listfile::get(core::Game::instance().configFolder() + filename, [this](float fraction) {
      if (m_loadProgressCb)
        m_loadProgressCb(LoadPhase::ReadingListfile, fraction);
    }, reused);
  if (!list)
  {
    LOG_ERROR << "Failed to open" << filename;
    return;
  }
  core::LoadTimeline::instance().cache("listfile index", reused ? "HIT" : "MISS",
                                       reused ? QString("read earlier in this session")
                                              : QString("%1 entries read").arg(list->size()));

  // Loading a client again runs this on the folder the previous load filled. The files that load listed under the
  // same FileDataID and path are kept instead of being made again (every reload added ~1.9 million more before), and
  // whatever this pass does not list is dropped, so the folder ends up listing what a first load would.
  const bool reloading = nbChildren() > 0;
  freeDetachedFiles(); // a load that did not get as far as freeing its own

  LOG_INFO << "WoWFolder - Starting to build object hierarchy";
  core::LoadTimeline::instance().begin("file index build");
  m_remoteViewerFiles = 0;
  static const char * VIEWER_EXT[] = {".m2", ".skin", ".anim", ".skel", ".blp", ".wmo", ".db2"};
  const size_t n = list->size();
  // The previous pass's file of each line, when it read this same listfile: found again without a search.
  const bool sameList = reloading && m_listfile == list && m_entryFiles.size() == n;
  core::LoadTimeline::instance().begin("this client's files");
  std::vector<GameFile *> entryFile(n, nullptr);
  std::vector<GameFile *> models;
  std::vector<GameFile *> files;
  files.reserve(reloading ? nbChildren() : n);
  size_t kept = 0, created = 0;
  const size_t reportEvery = std::max<size_t>(1, n / 100);
  for (size_t i = 0; i < n; i++)
  {
    if (m_loadProgressCb && i % reportEvery == 0)
      m_loadProgressCb(LoadPhase::IndexingFiles, (float)i / (float)n);
    const int id = list->id(i);
    const QString & name = list->path(i);
    if (m_CASCFolder.isRemote(id))
      for (const char * ext : VIEWER_EXT)
        if (name.endsWith(QLatin1String(ext)))
        {
          m_remoteViewerFiles++;
          break;
        }
    if (!m_CASCFolder.fileExists(id))
      continue;
    GameFile * file = nullptr;
    if (reloading)
    {
      GameFile * previous = nullptr;
      if (sameList)
        previous = m_entryFiles[i];
      else
      {
        auto known = m_idMap.find(id);
        if (known != m_idMap.end())
          previous = known->second;
      }
      // Still one of this folder's files (checked before it is touched: a file of the last pass can have been freed
      // since), and the same file: listed under this id and path.
      if (previous && hasChild(previous) && previous->fileDataId() == id && dynamic_cast<CASCFile *>(previous) &&
          (previous->fullname().constData() == name.constData() || previous->fullname() == name))
        file = previous;
    }
    if (file)
      kept++;
    else
    {
      CASCFile * made = new CASCFile(name, id);
      made->setName(name.mid(name.lastIndexOf('/') + 1));
      file = made;
      created++;
    }
    entryFile[i] = file;
    files.push_back(file);
    if (name.endsWith(QLatin1String(".m2"), Qt::CaseInsensitive))
      models.push_back(file);
  }

  core::LoadTimeline::instance().end("this client's files");
  // The folder's files and indexes, each built at once. What the previous load listed and this one does not -- files
  // the new client does not have or the listfile no longer names, files whose entry now names another path, files
  // opened by id alone, and the custom-folder files, which addCustomFiles adds again -- is let go of below.
  core::LoadTimeline::instance().begin("children");
  std::vector<GameFile *> stale = replaceChildren(files);
  replaceModelFiles(models);
  core::LoadTimeline::instance().end("children");
  core::LoadTimeline::instance().begin("index by id");
  {
    // By FileDataID, in line order (the last line of an id wins, as it did when each file was added on its own). The
    // community listfile is sorted by id, so every insertion is at the end.
    std::map<int, GameFile *> byId;
    for (GameFile * f : files)
    {
      const int id = f->fileDataId();
      if (byId.empty() || byId.rbegin()->first < id)
        byId.emplace_hint(byId.end(), id, f);
      else
        byId[id] = f;
    }
    m_idMap.swap(byId);
  }
  core::LoadTimeline::instance().end("index by id");
  core::LoadTimeline::instance().begin("index by path");
  {
    // By path, in path order: every insertion at the end. (The same path twice: the later line wins, as before.)
    std::map<QString, GameFile *> byPath;
    const QString * last = nullptr;
    for (quint32 e : list->pathOrder())
    {
      GameFile * f = entryFile[e];
      if (!f)
        continue;
      const QString & name = list->path(e);
      if (last && *last == name)
        byPath.rbegin()->second = f;
      else
        byPath.emplace_hint(byPath.end(), name, f);
      last = &name;
    }
    replaceNameIndex(std::move(byPath));
  }
  core::LoadTimeline::instance().end("index by path");
  // Those let go of are detached now and freed after the load (freeDetachedFiles, while the viewer is idle), once
  // Browse and the character controls are rebuilt: until then a Browse row of the previous load still points at one. The count:
  // addChild takes no reference, so a child is at 0; m_detached holds one of its own, and freeing drops it.
  for (GameFile * f : stale)
  {
    f->ref();
    m_detached.push_back(f);
  }
  m_listfile = list;
  m_entryFiles.swap(entryFile);
  core::LoadTimeline::instance().end("file index build", QString("%1 files: %2 kept, %3 new, %4 let go%5")
                                                           .arg(files.size()).arg(kept).arg(created).arg(stale.size())
                                                           .arg(sameList ? "" : (reloading ? " (searched by id)" : "")));
  if (reloading)
    LOG_INFO << "WoWFolder - Reload dropped" << (unsigned int)stale.size() << "files the previous load listed";
  LOG_INFO << "WoWFolder - Hierarchy creation done";
}

void wow::WoWFolder::freeDetachedFiles()
{
  if (m_detached.empty())
    return;
  LOG_INFO << "WoWFolder - Freeing" << (unsigned int)m_detached.size() << "files the last reload detached";
  freeDetachedFiles(m_detached.size());
}

size_t wow::WoWFolder::freeDetachedFiles(size_t limit)
{
  const size_t count = std::min(limit, m_detached.size());
  for (size_t i = 0; i < count; i++)
  {
    m_detached.back()->unref(); // the reference m_detached held: the last one, so the file is deleted
    m_detached.pop_back();
  }
  if (m_detached.empty())
    m_detached.shrink_to_fit();
  return m_detached.size();
}

bool wow::WoWFolder::isRemoteFile(const QString & name) const
{
  const int id = m_listfile ? m_listfile->idOf(name.toLower()) : -1;
  return id != -1 && m_CASCFolder.isRemote(id);
}

void wow::WoWFolder::addCustomFiles(const QString & path, bool bypassOriginalFiles)
{
  LOG_INFO << "Add customFiles from folder" << path;
  QDirIterator dirIt(path, QDirIterator::Subdirectories);

  while(dirIt.hasNext())
  {
    dirIt.next();
    QString filePath = dirIt.filePath().toLower();

    if(QFileInfo(filePath).isFile())
    {
      QString toRemove = path;
      toRemove += "\\";
      filePath.replace(0, toRemove.size(), "");

      GameFile * originalFile = GameFolder::getFile(filePath);
      bool addnewfile = true;
      int originalId = -1;
      if(originalFile)
      {
        if(bypassOriginalFiles)
        {
          originalId = originalFile->fileDataId();
          removeChild(originalFile);
          delete originalFile;
          originalFile = 0;
          addnewfile = true;
        }
        else
        {
          addnewfile = false;
        }
      }
      else
      {
        // Even though the file wasn't found in the game database, it's possible to assign it
        // a specific ID in the listfile (useful in some situations) :
        const int listed = m_listfile ? m_listfile->idOf(filePath) : -1;
        if (listed != -1)
          originalId = listed;
      }
      if(addnewfile)
      {
        LOG_INFO << "Add custom file" << filePath << "(ID:" << originalId << ")from hard drive location" << dirIt.filePath();
        HardDriveFile * file = new HardDriveFile(filePath, dirIt.filePath(), originalId);
        file->setName(filePath.mid(filePath.lastIndexOf("/")+1));
        addChild(file);
      }
    }
  }
}


GameFile * wow::WoWFolder::getFile(int id)
{
  GameFile * result = 0;

  if (id <= 0) // bad id given
    return result;

  auto it = m_idMap.find(id);
  if (it != m_idMap.end())
    result = it->second;

  if (!result) // if not found, try to force open by id
  {
    // Build File########.unk filename needed for CASC lib to open file based on id
    QString filename = QString("File%1.unk").arg(id, 8, 16, QLatin1Char('0'));
    // A file of this build that is not on this computer -- one the client never downloads, or one it has not yet (a
    // Battle.net update under way) -- is left out of the folder even when the listfile names it, so it lands here too.
    // The probe below still "opens" it (the build knows the file), but its data cannot be read: say so, rather than
    // blame the listfile.
    const bool remote = m_CASCFolder.isRemote(id);
    if (remote)
    {
      const char * notHere = "is in this build but not on this computer (not downloaded): it cannot be read";
      const QString knownName = fileName(id);
      if (knownName.isEmpty())
        LOG_WARNING << "File" << id << "(not in listfile)" << notHere;
      else
        LOG_WARNING << "File" << id << knownName << notHere;
    }
    else
      LOG_INFO << "File with id" << id << "not found in listfile. Trying to open" << filename;

    // Force-open-by-id probe for a file that isn't in the listfile. Route through the active
    // provider so storage stays abstracted (identical to the old direct call for modern CASC);
    // fall back to m_CASCFolder if the provider isn't set yet.
    HANDLE newfile;
    const bool opened = m_provider ? m_provider->openById(id, &newfile)
                                   : m_CASCFolder.openFile(id, &newfile);
    if(opened)
    {
      if (!remote)
        LOG_INFO << "Succesfully opened";
      if (m_provider)
        m_provider->closeFile(newfile);
      else
        m_CASCFolder.closeFile(newfile);
      CASCFile * file = new CASCFile(filename, id);
      file->setName(filename);
      addChild(file);
      result = file;
    }
  }

  return result;
}

GameFile * wow::WoWFolder::getFile(QString filename)
{
  // First the normal name-map lookup (CASC listfile entries, custom files, and MPQ files that
  // were already created on demand).
  GameFile * result = GameFolder::getFile(filename);
  if (result)
    return result;

  // Name-addressed storage (MPQ): create the file on demand if the archive chain has it. This
  // is the legacy equivalent of the CASC getFile(int) force-open probe above.
  if (m_provider && m_provider->supportsNameLookup())
  {
    const QString norm = filename.toLower().replace('\\', '/');
    if (m_provider->hasFile(norm.toStdString()))
    {
      MpqFile * file = new MpqFile(norm);
      file->setName(norm.mid(norm.lastIndexOf('/') + 1));
      addChild(file); // registers it in the name map for next time
      result = file;
    }
  }

  return result;
}

bool wow::WoWFolder::openFile(int id, HANDLE * result)
{
  // Route through the active storage provider. For the modern (CASC) client the provider
  // forwards straight to m_CASCFolder, so behaviour is unchanged. Fall back to m_CASCFolder
  // if the provider has not been created yet (openFile before setConfig()).
  if (m_provider)
    return m_provider->openById(id, result);
  return m_CASCFolder.openFile(id, result);
}

bool wow::WoWFolder::openFile(std::string file, HANDLE * result)
{
  // Name-addressed storage (MPQ): open directly by name through the provider -- there is no
  // FileDataID and no listfile to resolve against.
  if (m_provider && m_provider->supportsNameLookup())
    return m_provider->openByName(file, result);

  // CASC: resolve the name to a FileDataID via the listfile (as before), then open by id
  // through the provider.
  const int id = m_listfile ? m_listfile->idOf(QString::fromStdString(file)) : -1;
  if (id == -1)
    return false;
  if (m_provider)
    return m_provider->openById(id, result);
  return m_CASCFolder.openFile(id, result);
}

QString wow::WoWFolder::version()
{
  if (m_clientProfile.storage == core::StorageType::MPQ)
    return m_clientProfile.versionString;
  return m_CASCFolder.version();
}

int wow::WoWFolder::majorVersion()
{
  if (m_clientProfile.storage == core::StorageType::MPQ)
    return m_clientProfile.major;
  auto v = m_CASCFolder.version().split(QLatin1Char('.'));
  return v[0].toInt();
}

QString wow::WoWFolder::locale()
{
  if (m_clientProfile.storage == core::StorageType::MPQ)
    return m_mpqLocale;
  return m_CASCFolder.locale();
}

bool wow::WoWFolder::setConfig(core::GameConfig config)
{
  // Forward the load-progress callback so the (long) present-file enumeration can report.
  // Through the folder's current callback, not a copy of it: the loading page that set it is gone after the load.
  m_CASCFolder.setProgressCallback([this](float fraction) {
    if (m_loadProgressCb)
      m_loadProgressCb(LoadPhase::OpeningStorage, fraction);
  });
  const bool ok = m_CASCFolder.setConfig(config);

  // A client that did not open changes nothing: the one already loaded (if any) stays as it was, profile and
  // provider included (CASCFolder::setConfig keeps its storage and config too).
  if (!ok)
  {
    LOG_ERROR << "[clientprofile] not opened:" << core::ClientProfile::fromGameConfig(config).describe();
    return false;
  }

  // The client profile for the opened config, and its storage provider: every Battle.net product is CASC
  // (ClientProfile::fromGameConfig), so a CascFileProvider forwarding to m_CASCFolder. The legacy MPQ client
  // installs its own provider (initMpq).
  m_clientProfile = core::ClientProfile::fromGameConfig(config);
  m_provider.reset(new CascFileProvider(&m_CASCFolder));

  LOG_INFO << "[clientprofile] active client ->" << m_clientProfile.describe();
  LOG_INFO << "[fileprovider] storage backend:" << m_provider->name()
           << "| ready:" << (m_provider->isReady() ? "yes" : "no")
           << "| lookup by id:" << (m_provider->supportsFileDataId() ? "yes" : "no")
           << "| lookup by name:" << (m_provider->supportsNameLookup() ? "yes" : "no");

  return ok;
}

int wow::WoWFolder::countMpqArchives(const QString & dataFolder, const QString & locale)
{
  MpqFileProvider probe;
  return probe.init(dataFolder, locale);
}

int wow::WoWFolder::initMpq(const QString & dataFolder, const QString & locale, const QString & version)
{
  // Build the client profile for a legacy (pre-CASC) client. fromGameConfig maps the major
  // version to the era and, for anything before Warlords (6.x), to MPQ storage / Name lookup.
  core::GameConfig cfg;
  cfg.locale = locale;
  cfg.version = version;   // e.g. "3.3.5.12340"
  cfg.product = "mpq";
  m_clientProfile = core::ClientProfile::fromGameConfig(cfg);
  // We are explicitly in the legacy path -- force MPQ storage / Name lookup regardless of the
  // version string that was passed.
  m_clientProfile.storage = core::StorageType::MPQ;
  m_clientProfile.lookupMode = core::FileLookupMode::Name;
  m_clientProfile.hasFileDataId = false;

  MpqFileProvider * mpq = new MpqFileProvider();
  const int opened = mpq->init(dataFolder, locale);
  m_provider.reset(mpq);
  m_mpqLocale = mpq->detectedLocale();

  // Fresh legacy client -> drop any cached WotLK DBC tables so they are re-read from this install.
  WotlkDbc::instance().reset();

  LOG_INFO << "[clientprofile] active client ->" << m_clientProfile.describe();
  LOG_INFO << "[fileprovider] storage backend:" << m_provider->name()
           << "| ready:" << (m_provider->isReady() ? "yes" : "no")
           << "| lookup by name:" << (m_provider->supportsNameLookup() ? "yes" : "no")
           << "| archives:" << opened
           << "| locale:" << (m_mpqLocale.isEmpty() ? QString("(auto: none)") : m_mpqLocale);
  LOG_INFO << "[fileprovider] MPQ load order (base -> patches -> locale, highest priority last):"
           << mpq->archiveListString();

  // Populate the browsable file tree from the MPQ (listfile) so the GUI file browser works (the
  // CASC path builds its tree from the community listfile; MPQ clients carry their own). Files
  // are still opened lazily/by-name -- these entries just make them discoverable in the UI.
  if (opened > 0)
  {
    std::vector<QString> names;
    mpq->listAllFiles(names);
    for (const QString & n : names)
    {
      MpqFile * f = new MpqFile(n);
      f->setName(n.mid(n.lastIndexOf('/') + 1));
      addChild(f);
    }
    LOG_INFO << "[fileprovider] MPQ file tree populated with" << (int)names.size() << "files";
  }

  return opened;
}

std::vector<core::GameConfig> wow::WoWFolder::configsFound()
{
  return m_CASCFolder.configsFound();
}

int wow::WoWFolder::lastError()
{
  return m_CASCFolder.lastError();
}

void wow::WoWFolder::onChildAdded(GameFile * child)
{
  GameFolder::onChildAdded(child);
  m_idMap[child->fileDataId()] = child;
}

void wow::WoWFolder::onChildRemoved(GameFile * child)
{
  GameFolder::onChildRemoved(child);
  // Only when the id still leads to this file: a reload can have listed its replacement already.
  auto it = m_idMap.find(child->fileDataId());
  if (it != m_idMap.end() && it->second == child)
    m_idMap.erase(it);
}

QString wow::WoWFolder::fileName(int id)
{
  return m_listfile ? m_listfile->pathOf(id) : QString();
}

int wow::WoWFolder::fileID(QString fileName)
{
  return m_listfile ? m_listfile->idOf(fileName) : -1;
}
   