/*
 * TextureExport.h
 *
 * Where the Textures viewer's exports go: the Quick export setting, the texture export folder, the file a
 * texture is saved as there, the checks on a folder, and showing a saved file in Explorer. No pixels or bytes
 * are made here: the view's writers (TextureView::exportPng / exportBlp) do that, the same for both workflows.
 *
 * TWO WORKFLOWS, ONE WRITER. Export (the normal one) asks where with Save As. Quick export saves at once into
 * the texture export folder, under the texture's own folder in the game ("creature/bear/bearskinblack.png").
 * Which one a button, a key or Browse's menu uses is the Quick export setting (TextureView::exportRequested).
 *
 * THE FOLDER. The setting holds a folder, or nothing for the default: Exports\Textures in the viewer's own
 * folder (the running program's, never the working directory). The installer only installs where the user can
 * write (installer\WoWModelViewer.iss), so the default is writable there; a copy put where it is not (Program
 * Files) is told so when an export fails, and Options > Settings > Export changes the folder. Nothing is ever
 * saved anywhere else instead.
 *
 * THE FILE NAME. A texture with a name keeps its path in the game, folders and all, below the export folder;
 * one known only by its FileDataID goes to FileDataID\FileDataID_<id>.<ext>. A path's parts that Windows
 * cannot take (reserved characters, device names such as CON, a trailing dot or space, "..") are made safe,
 * and then the FileDataID is added to the name, so two different textures never share a file: the same
 * texture in the same format always goes to the same file and replaces the copy saved before, nothing else
 * does. The result is checked to lie inside the export folder before anything is written.
 */

#ifndef TEXTUREEXPORT_H
#define TEXTUREEXPORT_H

#include <functional>

#include <QString>

#include "TextureCatalog.h"

namespace TextureExport
{
  // PNG: the decoded texture at full resolution, with its alpha. Original: the file as read from the game
  // client (a BLP, or whatever a FileDataID opened).
  enum class Format { Png, Original };

  // ---- Settings (Config.ini, written when they change).
  bool quickExport();                         // TextureViewer/QuickExport
  void setQuickExport(bool on);
  QString defaultFolder();                    // <the viewer's folder>/Exports/Textures
  QString configuredFolder();                 // TextureViewer/QuickExportFolder; empty: the default
  QString folder();                           // the folder in use: the configured one, or the default
  void setFolder(const QString & folder);     // empty: back to the default

  // ---- A folder for exports: a folder that is there, or that can be made, and that can be written to.
  struct FolderCheck
  {
    bool ok = false;
    bool exists = false;   // there already (else made by the first export)
    QString error;         // why not, for the user
  };
  // Tries it the way an export writes: a test folder with a file in it, in the folder (or, while it is not there,
  // in the nearest folder above it that is, never above its drive or network share), removed again. A folder
  // where they cannot be removed again is refused too (an export could not replace its file there).
  FolderCheck checkFolder(const QString & folder);

  // ---- The file a texture is saved as, relative to the export folder ('/' between the parts). blpContent:
  // whether the original file is a BLP (it decides an unnamed original's extension, .blp or .unk).
  QString relativePath(const TextureEntry & entry, Format format, bool blpContent);
  // root/relative, made absolute, checked to lie inside root. False, with why, when it does not.
  bool resolveInside(const QString & root, const QString & relative, QString & absolute, QString & error);

  // ---- Explorer: the folder of this file, with the file selected (the folder alone where that fails).
  void showInExplorer(const QString & file);
  // The folder itself (made first when it is not there).
  void openFolder(const QString & folder);
  // For the tests: called instead of opening Explorer.
  extern std::function<void(const QString & path, bool selectFile)> explorerHook;

  // Native separators, for showing a path.
  QString display(const QString & path);
}

#endif // TEXTUREEXPORT_H
