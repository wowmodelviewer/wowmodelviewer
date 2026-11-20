#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <wx/filename.h>
#include <wx/stdpaths.h>

namespace
{
  QString fromWx(const wxString & s) { return QString::fromWCharArray(s.wc_str()); }

  // Points 'link' at 'target' (a directory), replacing a link that points elsewhere (the app was moved or updated).
  bool linkDir(const QString & target, const QString & link)
  {
    const QFileInfo info(link);
    if (info.isSymLink())
    {
      if (QDir::cleanPath(info.symLinkTarget()) == QDir::cleanPath(target))
        return true;
      QFile::remove(link);
    }
    else if (info.exists())
      return true; // a real folder the user made: theirs, left alone
    return QFile::link(target, link);
  }
}

QString AppPaths::executableDir()
{
  return fromWx(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath());
}

QString AppPaths::resourceDir()
{
#if defined(__APPLE__)
  return QDir::cleanPath(executableDir() + "/../Resources");
#elif defined(_WIN32)
  return executableDir();
#else
  const QString share = executableDir() + "/share";
  if (QDir(share + "/games").exists())
    return share;
  // An installed layout: <prefix>/bin/wowmodelviewer and <prefix>/share/wowmodelviewer.
  return QDir::cleanPath(executableDir() + "/../share/wowmodelviewer");
#endif
}

QString AppPaths::pluginDir()
{
#if defined(__APPLE__)
  return QDir::cleanPath(executableDir() + "/../PlugIns");
#else
  return executableDir() + "/plugins";
#endif
}

QString AppPaths::homeDir()
{
#if defined(_WIN32)
  return executableDir();
#else
#if defined(__APPLE__)
  return QDir::homePath() + "/Library/Application Support/WoW Model Viewer";
#else
  // A portable install (a folder unpacked anywhere, run from there): a file named "portable" next to the program.
  if (QFile::exists(executableDir() + "/portable"))
    return executableDir();
  const QByteArray xdg = qgetenv("XDG_DATA_HOME");
  const QString base = xdg.isEmpty() ? QDir::homePath() + "/.local/share" : QString::fromLocal8Bit(xdg);
  return base + "/wowmodelviewer";
#endif
#endif
}

bool AppPaths::prepareHome(QString & why)
{
  const QString home = homeDir();
  if (!QDir().mkpath(home))
  {
    why = "cannot create " + home;
    QDir::setCurrent(executableDir());
    return false;
  }
  const QString res = resourceDir();
  if (QDir::cleanPath(home) != QDir::cleanPath(res))
  {
    QDir().mkpath(home + "/games");
    // games/wow: the schemas, a link into the program's data, so an update brings its schemas with it.
    if (!linkDir(res + "/games/wow", home + "/games/wow") || !linkDir(res + "/dbd", home + "/dbd"))
      why = "cannot link the program's data into " + home;
    // Downloaded again when it changes: a copy to start from.
    if (!QFile::exists(home + "/extraEncryptionKeys.csv") && QFile::exists(res + "/extraEncryptionKeys.csv"))
      QFile::copy(res + "/extraEncryptionKeys.csv", home + "/extraEncryptionKeys.csv");
  }
  QDir().mkpath(home + "/userSettings");
  return QDir::setCurrent(home) && why.isEmpty();
}
