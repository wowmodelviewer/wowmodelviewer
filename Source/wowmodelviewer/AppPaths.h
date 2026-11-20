/*
 * AppPaths.h
 *
 * WHERE THE VIEWER KEEPS ITS FILES.
 *
 *  - The program's own read-only data (database schemas games/wow/<ver>, table definitions dbd/, the first
 *    encryption key list, the icon): next to the executable on Windows, the bundle's Contents/Resources on macOS,
 *    share/ next to the executable on Linux (an AppImage's too).
 *  - Its plugins: <exe>/plugins on Windows and Linux, the bundle's Contents/PlugIns on macOS.
 *  - THE HOME FOLDER: everything the viewer writes -- userSettings/ (settings, logs), dbcache/, the downloaded
 *    listfile and keys, Exports/. Next to the executable on Windows (as it always was); on macOS
 *    ~/Library/Application Support/WoW Model Viewer, on Linux $XDG_DATA_HOME/wowmodelviewer (~/.local/share), or the
 *    executable's folder when a file named "portable" is next to it (a portable install, Linux).
 *
 * The viewer runs with the home folder as its working directory, so every relative path the code has always used
 * (userSettings/Config.ini, dbcache/, listfile.csv, games/wow/12.0/database.xml, dbd/) still resolves: prepareHome()
 * links the read-only data into the home folder (games, dbd) and seeds what is downloaded later (the key list).
 */

#ifndef APPPATHS_H
#define APPPATHS_H

#include <QString>

namespace AppPaths
{
  QString executableDir();
  QString resourceDir();
  QString pluginDir();
  QString homeDir();

  // Makes the home folder, links the read-only data into it and makes it the working directory. Returns false
  // (with why) when the folder cannot be made; the viewer then runs in the executable's folder.
  bool prepareHome(QString & why);
}

#endif
