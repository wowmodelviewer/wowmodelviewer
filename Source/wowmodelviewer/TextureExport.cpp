/*
 * TextureExport.cpp -- see TextureExport.h.
 */

#include "TextureExport.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>

#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include "util.h"
#include "logger/Logger.h"

namespace TextureExport
{
  std::function<void(const QString & path, bool selectFile)> explorerHook;

  namespace
  {
    const char * QuickKey = "TextureViewer/QuickExport";
    const char * FolderKey = "TextureViewer/QuickExportFolder";

    QString configFile() { return QString::fromWCharArray(cfgPath.wc_str()); }

    // A name Windows keeps for a device, alone or before an extension ("con", "aux.blp", "com1").
    bool deviceName(const QString & part)
    {
      const QString stem = part.section('.', 0, 0).trimmed().toUpper();
      if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL")
        return true;
      return stem.size() == 4 && (stem.startsWith("COM") || stem.startsWith("LPT")) && stem[3] >= '1' && stem[3] <= '9';
    }

    // One part of a game path as a Windows file or folder name: the characters Windows refuses become '_', a
    // trailing dot or space (which Windows would drop, merging "a." into "a") becomes '_', and a device name
    // gets a '_' in front. 'changed' is set when the part is not kept as it was.
    QString safePart(const QString & part, bool & changed)
    {
      static const QString refused = QStringLiteral("<>:\"/\\|?*");
      QString out;
      out.reserve(part.size());
      for (const QChar c : part)
        out += (c.unicode() < 32 || refused.contains(c)) ? QChar('_') : c;
      int end = out.size();
      while (end > 0 && (out[end - 1] == '.' || out[end - 1] == ' '))
        end--;
      if (end < out.size())
        out = out.left(end) + QString(out.size() - end, QChar('_'));
      if (deviceName(out))
        out.prepend('_');
      if (out != part)
        changed = true;
      return out;
    }

    // The nearest folder at or above this path that exists, never above the drive or the network share the path
    // is on; empty when there is none (a drive or a share that is not there).
    QString nearestFolder(const QString & path, QString & fileInTheWay)
    {
      QString p = QDir::cleanPath(path);
      // The top: "C:/", or "//server/share" for a network folder (above it is the server, then "/", the current
      // drive's root, which is not where the path is).
      QString top;
      if (p.startsWith("//"))
        top = p.section('/', 0, 3);
      else if (p.size() >= 3 && p[1] == ':' && p[2] == '/')
        top = p.left(3);
      while (true)
      {
        const QFileInfo info(p);
        if (info.isDir())
          return p;
        if (info.exists())
        {
          fileInTheWay = p;
          return QString();
        }
        const QString parent = QFileInfo(p).path();
        if (parent.isEmpty() || parent == p || parent == "." || parent == "/" || p.compare(top, Qt::CaseInsensitive) == 0)
          return QString();
        p = parent;
      }
    }

    // A name for a test file or folder that nothing else uses.
    QString testName()
    {
      return QString("wmv-write-test-%1-%2").arg((qulonglong)GetCurrentProcessId()).arg(QDateTime::currentMSecsSinceEpoch());
    }
  }

  // ------------------------------------------------------------------------------------------ settings

  bool quickExport()
  {
    QSettings config(configFile(), QSettings::IniFormat);
    return config.value(QuickKey, false).toBool();
  }

  void setQuickExport(bool on)
  {
    QSettings config(configFile(), QSettings::IniFormat);
    config.setValue(QuickKey, on);
    config.sync();
  }

  QString defaultFolder()
  {
    // The running program's folder: where the installer put it, whatever the working directory is.
    const wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    return QDir::cleanPath(QString::fromWCharArray(exe.GetPath().wc_str()) + "/Exports/Textures");
  }

  QString configuredFolder()
  {
    QSettings config(configFile(), QSettings::IniFormat);
    return config.value(FolderKey).toString().trimmed();
  }

  QString folder()
  {
    const QString configured = configuredFolder();
    return configured.isEmpty() ? defaultFolder() : QDir::cleanPath(QDir(configured).absolutePath());
  }

  void setFolder(const QString & folder)
  {
    QSettings config(configFile(), QSettings::IniFormat);
    const QString clean = folder.trimmed().isEmpty() ? QString() : QDir::cleanPath(QDir(folder.trimmed()).absolutePath());
    if (clean.isEmpty() || QDir::cleanPath(clean).compare(defaultFolder(), Qt::CaseInsensitive) == 0)
      config.remove(FolderKey);   // the default, wherever the viewer is
    else
      config.setValue(FolderKey, clean);
    config.sync();
    LOG_INFO << "[texexport] export folder" << (clean.isEmpty() ? QString("(the default)") : clean);
  }

  QString display(const QString & path)
  {
    return QDir::toNativeSeparators(path);
  }

  // -------------------------------------------------------------------------------------------- checks

  FolderCheck checkFolder(const QString & folderPath)
  {
    FolderCheck result;
    if (folderPath.trimmed().isEmpty())
    {
      result.error = QStringLiteral("No folder is set.");
      return result;
    }
    const QString target = QDir::cleanPath(QDir(folderPath.trimmed()).absolutePath());
    const QFileInfo info(target);
    if (info.exists() && !info.isDir())
    {
      result.error = QString("%1 is a file, not a folder.").arg(display(target));
      return result;
    }
    result.exists = info.isDir();

    // The test is made the way an export makes its files: a folder (an export makes the texture's game folders)
    // with a file in it, in the folder itself or -- while it is not there -- in the nearest folder above it. (A
    // drive root such as C:\ lets users make folders in it but not files; exports never write there directly.)
    QString probeIn = target;
    if (!result.exists)
    {
      QString file;
      probeIn = nearestFolder(target, file);
      if (probeIn.isEmpty())
      {
        result.error = file.isEmpty() ? QString("%1 cannot be made: the drive or network folder it is on cannot be reached.").arg(display(target))
                                      : QString("%1 cannot be made: %2 is a file.").arg(display(target), display(file));
        return result;
      }
    }
    const QString testDir = QDir(probeIn).filePath(testName());
    if (!QDir().mkdir(testDir))
    {
      result.error = result.exists ? QString("%1 cannot be written to: folders cannot be made in it.").arg(display(target))
                                   : QString("%1 cannot be made: folders cannot be made in %2.").arg(display(target), display(probeIn));
      return result;
    }
    QFile test(QDir(testDir).filePath(testName() + ".tmp"));
    const bool wrote = test.open(QIODevice::WriteOnly) && test.write("test", 4) == 4;
    test.close();
    const bool removed = (!test.exists() || test.remove()) && QDir().rmdir(testDir);
    if (!removed)
      LOG_WARNING << "[texexport] the write test could not be removed:" << testDir;
    if (!wrote)
    {
      result.error = result.exists ? QString("%1 cannot be written to.").arg(display(target))
                                   : QString("%1 cannot be made: the folder it would be in cannot be written to.").arg(display(target));
      return result;
    }
    if (!removed)
    {
      // Files can be made there but not removed: a file exported again could not be replaced either.
      result.error = QString("%1 cannot be used: files saved there cannot be replaced or removed.").arg(display(target));
      return result;
    }
    result.ok = true;
    return result;
  }

  // --------------------------------------------------------------------------------------------- paths

  QString relativePath(const TextureEntry & entry, Format format, bool blpContent)
  {
    const QString extension = format == Format::Png ? QString("png") : (blpContent ? QString("blp") : QString("unk"));
    const auto byFileDataId = [&]() {
      return entry.fileDataId > 0 ? QString("FileDataID/FileDataID_%1.%2").arg(entry.fileDataId).arg(extension) : QString();
    };
    if (entry.unnamed || entry.path.isEmpty())
      return byFileDataId();

    // The game path's folders and name, each made safe for Windows; "." and ".." (and empty parts) are left out.
    QString path = entry.path;
    path.replace('\\', '/');
    bool changed = path.startsWith('/');
    QStringList parts;
    for (const QString & part : path.split('/'))
    {
      if (part.isEmpty())
        continue;
      if (part == "." || part == "..")
      {
        changed = true;
        continue;
      }
      const QString safe = safePart(part, changed);
      if (safe.isEmpty())
      {
        changed = true;
        continue;
      }
      parts << safe;
    }
    if (parts.isEmpty())
      return byFileDataId();

    // The name for the format: a PNG's .blp becomes .png (another extension stays, "x.tga.png"); the
    // original keeps its own name.
    QString name = parts.takeLast();
    if (format == Format::Png)
    {
      if (name.endsWith(".blp", Qt::CaseInsensitive))
        name.chop(4);
      name += ".png";
    }
    // A path that had to be changed could now be another texture's: the FileDataID (or, on a client without
    // FileDataIDs, a short hash of the game path) keeps it apart.
    if (changed)
    {
      const QString tag = entry.fileDataId > 0
                            ? QString::number(entry.fileDataId)
                            : QString(QCryptographicHash::hash(entry.path.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
      const int dot = name.lastIndexOf('.');
      name = dot > 0 ? name.left(dot) + "_" + tag + name.mid(dot) : name + "_" + tag;
    }
    parts << name;
    return parts.join('/');
  }

  bool resolveInside(const QString & root, const QString & relative, QString & absolute, QString & error)
  {
    const QString base = QDir::cleanPath(QDir(root).absolutePath());
    if (relative.isEmpty())
    {
      error = QStringLiteral("The texture has no name or FileDataID to save it under.");
      return false;
    }
    if (QDir::isAbsolutePath(relative) || relative.contains(':'))
    {
      error = QString("%1 is not a path inside the export folder.").arg(display(relative));
      return false;
    }
    const QString full = QDir::cleanPath(base + "/" + relative);
    const QString inside = base.endsWith('/') ? base : base + "/";
    if (!full.startsWith(inside, Qt::CaseInsensitive) || full.size() <= inside.size())
    {
      error = QString("%1 would not be inside the export folder %2.").arg(display(relative), display(base));
      return false;
    }
    absolute = full;
    return true;
  }

  // ------------------------------------------------------------------------------------------- Explorer

  void showInExplorer(const QString & file)
  {
    if (explorerHook)
    {
      explorerHook(file, true);
      return;
    }
    // Explorer's own way: the folder opened (or brought up) with the file selected in it.
    const std::wstring native = display(file).toStdWString();
    bool shown = false;
    if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(native.c_str()))
    {
      shown = SUCCEEDED(SHOpenFolderAndSelectItems(item, 0, nullptr, 0));
      ILFree(item);
    }
    if (!shown)
    {
      LOG_WARNING << "[texexport] could not select" << file << "in Explorer; opening its folder";
      openFolder(QFileInfo(file).absolutePath());
    }
  }

  void openFolder(const QString & folderPath)
  {
    if (explorerHook)
    {
      explorerHook(folderPath, false);
      return;
    }
    QDir().mkpath(folderPath);
    const std::wstring native = display(folderPath).toStdWString();
    ShellExecuteW(nullptr, L"open", native.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  }
}
