/*
 * WoWFolder.cpp
 *
 *  Created on: 7 Aug. 2017
 *      Author: Jeromnimo
 */

#include "WoWFolder.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>

#include "CASCFile.h"
#include "CascFileProvider.h"
#include "Game.h"
#include "HardDriveFile.h"
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
  QFile file(core::Game::instance().configFolder() + filename);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    LOG_ERROR << "Failed to open" << filename;
    return;
  }

  QTextStream in(&file);

  // For the optional load-progress callback: estimate completion from bytes consumed so the
  // loading UI advances during this (longest) startup step instead of sitting at one value.
  const qint64 totalBytes = file.size();
  qint64 bytesRead = 0;
  qint64 nextReport = 0;
  const qint64 reportEvery = (totalBytes > 0) ? (totalBytes / 60) : 1; // ~60 updates

  LOG_INFO << "WoWFolder - Starting to build object hierarchy";
  while (!in.atEnd())
  {
    QString line = in.readLine().toLower();

    bytesRead += (qint64)line.length() + 1; // approx (ASCII listfile + newline)
    if (m_loadProgressCb && bytesRead >= nextReport)
    {
      m_loadProgressCb((totalBytes > 0) ? (float)bytesRead / (float)totalBytes : 0.0f);
      nextReport = bytesRead + reportEvery;
    }

    QStringList lineData = line.split(';');
    if (lineData.size() < 2)
      continue;
    int id = lineData.at(0).toInt();
    QString fileName = lineData.at(1);
    // Add the file to the name-ID mappings even if it can't be found in CASC,
    // as it could be a custom file added by the user:
    m_idNameMap[id] = fileName;
    m_nameIdMap[fileName] = id;
    if (m_CASCFolder.fileExists(id))
    {
      CASCFile * File = new CASCFile(fileName, id);
      File->setName(line.mid(line.lastIndexOf('/') + 1));
      addChild(File);
    }
  }
  LOG_INFO << "WoWFolder - Hierarchy creation done";
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
        auto it = m_nameIdMap.find(filePath);
        if (it != m_nameIdMap.end())
          originalId = it->second;
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
    LOG_INFO << "File with id" << id << "not found in listfile. Trying to open" << filename;

    // Force-open-by-id probe for a file that isn't in the listfile. Route through the active
    // provider so storage stays abstracted (identical to the old direct call for modern CASC);
    // fall back to m_CASCFolder if the provider isn't set yet.
    HANDLE newfile;
    const bool opened = m_provider ? m_provider->openById(id, &newfile)
                                   : m_CASCFolder.openFile(id, &newfile);
    if(opened)
    {
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
  auto it = m_nameIdMap.find(QString::fromStdString(file));
  if (it == m_nameIdMap.end())
    return false;
  if (m_provider)
    return m_provider->openById(it->second, result);
  return m_CASCFolder.openFile(it->second, result);
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
  m_CASCFolder.setProgressCallback(m_loadProgressCb);
  const bool ok = m_CASCFolder.setConfig(config);

  // Derive the client profile from the detected config and select the matching storage
  // provider. Modern clients (CASC) resolve to a CascFileProvider that forwards to
  // m_CASCFolder -- identical behaviour to before. Old MoPaQ clients resolve to the
  // placeholder MpqFileProvider (not implemented yet). Done regardless of ok so the profile
  // reflects what was requested even on a failed open.
  m_clientProfile = core::ClientProfile::fromGameConfig(config);
  if (m_clientProfile.storage == core::StorageType::MPQ)
    m_provider.reset(new MpqFileProvider());
  else
    m_provider.reset(new CascFileProvider(&m_CASCFolder));

  LOG_INFO << "[clientprofile] active client ->" << m_clientProfile.describe();
  LOG_INFO << "[fileprovider] storage backend:" << m_provider->name()
           << "| ready:" << (m_provider->isReady() ? "yes" : "no")
           << "| lookup by id:" << (m_provider->supportsFileDataId() ? "yes" : "no")
           << "| lookup by name:" << (m_provider->supportsNameLookup() ? "yes" : "no");

  return ok;
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
  m_idMap.erase(child->fileDataId());
}

QString wow::WoWFolder::fileName(int id)
{
  auto it = m_idNameMap.find(id);
  if (it == m_idNameMap.end())
    return QString();
  return it->second;
}

int wow::WoWFolder::fileID(QString fileName)
{
  auto it = m_nameIdMap.find(fileName);
  if (it == m_nameIdMap.end())
    return -1;
  return it->second;
}
   