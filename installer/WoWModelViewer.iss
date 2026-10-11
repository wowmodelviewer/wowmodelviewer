; WoW Model Viewer - Inno Setup script (Inno Setup 6.7 or later)
; -----------------------------------------------------------------------------------------------------------------
; Build it with installer\build-installer.ps1: that script stages the exact release folder (the program, its
; runtime DLLs, the Unity player, the data files, the licence texts) and compiles this script against it:
;
;   ISCC.exe /DStage=<staged folder> /DMyAppVersion=1.0 WoWModelViewer.iss
;
; The installer is per-user (no administrator rights): the viewer keeps its settings, caches and the file list
; next to the program, so it must be installed where the user can write. {autopf} is then
; %LOCALAPPDATA%\Programs. Updates install over the previous version in its own folder (same AppId).

#ifndef Stage
  #error Define Stage (/DStage=<folder>): the staged release folder made by build-installer.ps1
#endif
#ifndef MyAppVersion
  #define MyAppVersion "1.0"
#endif
; Test builds pass their own AppId so they never touch a real installation.
#ifndef MyAppId
  #define MyAppId "{{B7E9F3A2-1C4D-4E8A-9F6B-2A3C5D7E9F10}"
#endif
#ifndef OutputBase
  #define OutputBase "WoWModelViewer-Setup-" + MyAppVersion
#endif
; The longest path of an installed file below the install folder (build-installer.ps1 passes the staged one), so the
; folder page can refuse a folder whose path would take a file past Windows' 259 characters.
#ifndef MaxRelPath
  #define MaxRelPath 117
#endif

#define MyAppName "WoW Model Viewer"
#define MyAppPublisher "WoW Model Viewer"
#define MyAppURL "https://github.com/wowmodelviewer/wowmodelviewer"
#define MyAppExeName "wowmodelviewer.exe"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
AppCopyright=Released under the GNU General Public License v3 or later
VersionInfoVersion={#MyAppVersion}
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
VersionInfoDescription={#MyAppName} Setup
VersionInfoCompany={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
; The folder page is always shown, for an update too (then it suggests the folder of the version installed). The
; folder chosen must be one the user can write to; the page's checks and questions, Inno's "folder exists" one
; included, are in NextButtonClick.
DisableDirPage=no
DirExistsWarning=no
DefaultGroupName={#MyAppName}
; 0.11.0's Start menu folder carried the old name ("WoW Model Viewer Midnight"); use the new one.
UsePreviousGroup=no
DisableProgramGroupPage=yes
; Lets an unattended install leave out the Start menu shortcut (/NOICONS); the wizard does not show the choice.
AllowNoIcons=yes
DisableWelcomePage=no
PrivilegesRequired=lowest
; Per-user, Setup never asks for elevation and writes only where the user can write anyway, so Windows'
; RedirectionGuard would protect nothing; it would only refuse a folder the user moved to another drive through a
; junction.
RedirectionGuard=no
; The viewer holds this mutex while it runs (WowModelViewApp::OnInit): Setup and Uninstall ask to close it first.
AppMutex=WoWModelViewer-B7E9F3A2-1C4D-4E8A-9F6B-2A3C5D7E9F10
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 21H1 (build 19043), the oldest Windows the Unity 6 player supports.
MinVersion=10.0.19043
LicenseFile={#Stage}\LICENSE.txt
OutputDir=dist
OutputBaseFilename={#OutputBase}
SetupIconFile=..\bin_support\icons\WMW-Midnight.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
SetupLogging=yes
; The look follows Windows' light or dark mode, as the viewer does, on the viewer's own panel colours
; (UiStyle.cpp: the light and dark palettes' panelBackground).
WizardStyle=modern dynamic windows11 hidebevels
WizardBackColor=#f9f9f9
WizardBackColorDynamicDark=#242427
; Made by art\make_art.py from the splash logo and the program icon; Setup picks the size for the DPI.
WizardImageFile=art\wizard-*.png
WizardImageFileDynamicDark=art\wizard-*.png
WizardSmallImageFile=art\small-*.png
WizardSmallImageFileDynamicDark=art\small-*.png

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel2=This will install [name/ver] on your computer.%n%nWoW Model Viewer shows the characters, creatures, items, buildings and textures of the World of Warcraft clients installed on this PC, and exports them.%n%nIt is a fan-made tool. It is not affiliated with or endorsed by Blizzard Entertainment. World of Warcraft and Warcraft are trademarks or registered trademarks of Blizzard Entertainment, Inc.
FinishedLabel=Setup has finished installing [name] on your computer.%n%nThe first time a game client is opened, the viewer downloads the community file list (about 150 MB), which it needs to find the game's files.
FinishedLabelNoIcons=Setup has finished installing [name] on your computer.%n%nThe first time a game client is opened, the viewer downloads the community file list (about 150 MB), which it needs to find the game's files.

[CustomMessages]
FolderNotWritable=Setup cannot write to this folder:%n%n%1%n%nWoW Model Viewer keeps its settings, the file list it downloads and its caches next to itself, so it has to be installed in a folder you can write to: the suggested one, another folder in your user folder, or a folder on another drive. Program Files will not work.
FolderTooLong=This folder's path is too long:%n%n%1%n%nSome of WoW Model Viewer's files would get paths longer than Windows allows. Choose a folder whose path has at most %2 characters.
FolderMoves=WoW Model Viewer is already installed in:%n%n%1%n%nInstalling into another folder leaves that copy where it is, with its settings and downloads, and Windows will then list only the new one. Do not use the old copy's uninstaller afterwards (it would remove the new one from the list of installed apps): delete its folder by hand. To move WoW Model Viewer cleanly, uninstall it first and then install it into the new folder.%n%nInstall into the new folder anyway?

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; An update from 0.11.0. That installer copied every DLL of its build folder (among them the FBX SDK, wxWidgets
; 3.2 and stale copies of the program's own DLLs), the FBX exporter plugin, which this version does not have, and
; Qt plugin folders it does not need; the viewer loads every DLL in plugins\. So the DLLs go before the new set is
; copied, only where the viewer was installed before.
Type: files; Name: "{app}\*.dll"; Check: ViewerWasHere
Type: files; Name: "{app}\plugins\*.dll"; Check: ViewerWasHere
Type: filesandordirs; Name: "{app}\platforms"; Check: ViewerWasHere
Type: filesandordirs; Name: "{app}\imageformats"; Check: ViewerWasHere
; The Unity player is replaced as a whole, so a file an older player had does not stay behind.
Type: filesandordirs; Name: "{app}\tools\unity-renderer"; Check: ViewerWasHere
; 0.11.0's shortcuts, under the old name: only those that start the viewer being updated.
Type: files; Name: "{userprograms}\WoW Model Viewer Midnight\WoW Model Viewer Midnight.lnk"; Check: ShortcutIntoApp('{userprograms}\WoW Model Viewer Midnight\WoW Model Viewer Midnight.lnk')
Type: files; Name: "{userprograms}\WoW Model Viewer Midnight\Uninstall WoW Model Viewer Midnight.lnk"; Check: ShortcutIntoApp('{userprograms}\WoW Model Viewer Midnight\Uninstall WoW Model Viewer Midnight.lnk')
Type: dirifempty; Name: "{userprograms}\WoW Model Viewer Midnight"
Type: files; Name: "{userdesktop}\WoW Model Viewer Midnight.lnk"; Check: ShortcutIntoApp('{userdesktop}\WoW Model Viewer Midnight.lnk')

[Files]
; The staged folder is exactly the release (build-installer.ps1 lists every file), so all of it is installed.
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\README.txt"; Description: "Show the README"; Flags: postinstall shellexec skipifsilent unchecked

[Registry]
; The viewport player (Unity) keeps its window settings under HKCU\Software\WoW Model Viewer. Nothing is written there
; at install time; uninstalling removes them. (Uninstall goes through these in reverse: the player's key, then its
; parent if that is empty then.)
Root: HKCU; Subkey: "Software\WoW Model Viewer"; Flags: uninsdeletekeyifempty dontcreatekey
Root: HKCU; Subkey: "Software\WoW Model Viewer\WoW Model Viewer Renderer"; Flags: uninsdeletekey dontcreatekey

[UninstallDelete]
; What the viewer writes next to itself while it runs: its settings and log, the database cache, the file list
; it downloads (and its download bookkeeping) and the refreshed encryption keys.
Type: filesandordirs; Name: "{app}\userSettings"
Type: filesandordirs; Name: "{app}\dbcache"
Type: files; Name: "{app}\listfile.csv"
Type: files; Name: "{app}\listfile.csv.*"
Type: files; Name: "{app}\extraEncryptionKeys.csv"
Type: files; Name: "{app}\extraEncryptionKeys.csv.*"
Type: files; Name: "{app}\wowdb.sqlite*"
; Folders an earlier version made (an update installs into them, so this uninstaller did not create them).
Type: dirifempty; Name: "{app}\games\wow\12.0"
Type: dirifempty; Name: "{app}\games\wow"
Type: dirifempty; Name: "{app}\games"
Type: dirifempty; Name: "{app}\dbd"
Type: dirifempty; Name: "{app}\plugins"
Type: dirifempty; Name: "{app}\tools"
Type: dirifempty; Name: "{app}"

[Code]
var
  ViewerWasHereFolder: String;
  ViewerWasHereResult: Boolean;

{ True when the chosen folder already holds the viewer: an update. Asked before the first file is copied, and
  remembered for that folder, so the answer is the folder as it was before this installation (going Back and
  choosing another folder asks again). }
function ViewerWasHere: Boolean;
var
  Folder: String;
begin
  Folder := ExpandConstant('{app}');
  if CompareText(Folder, ViewerWasHereFolder) <> 0 then
  begin
    ViewerWasHereResult := FileExists(AddBackslash(Folder) + '{#MyAppExeName}');
    ViewerWasHereFolder := Folder;
  end;
  Result := ViewerWasHereResult;
end;

{ True when files can be written in the folder as Setup will make it. A folder that exists gets a test file. One that
  does not exist yet is made the way Setup will make it: a test folder in the nearest existing folder above it, and a
  test file in that (a drive root such as C:\ lets users make folders there but not files, and a new folder gets
  write rights from it). Everything made for the test is removed again. }
function FolderIsWritable(Folder: String): Boolean;
var
  Probe, Parent, TestDir, TestFile: String;
begin
  Result := False;
  Folder := RemoveBackslashUnlessRoot(Folder);
  Probe := Folder;
  while not DirExists(Probe) do
  begin
    Parent := ExtractFileDir(Probe);
    if (Parent = '') or (CompareText(Parent, Probe) = 0) then
      Exit;
    Probe := Parent;
  end;
  TestDir := '';
  if CompareText(Probe, Folder) <> 0 then
  begin
    TestDir := AddBackslash(Probe) + 'wmv-setup-write-test-' + GetDateTimeString('yyyymmddhhnnsszzz', #0, #0);
    if not CreateDir(TestDir) then
      Exit;
    Probe := TestDir;
  end;
  TestFile := AddBackslash(Probe) + 'wmv-setup-write-test.tmp';
  if SaveStringToFile(TestFile, 'test', False) then
  begin
    DeleteFile(TestFile);
    Result := True;
  end;
  if TestDir <> '' then
    RemoveDir(TestDir);
end;

{ True when Folder is Root or a folder below it (by name; a short 8.3 name of Root counts too). }
function FolderIsUnder(Folder, Root: String): Boolean;
var
  ShortRoot: String;
begin
  Folder := AddBackslash(Folder);
  Root := AddBackslash(Root);
  ShortRoot := AddBackslash(GetShortName(RemoveBackslashUnlessRoot(Root)));
  Result := (CompareText(Copy(Folder, 1, Length(Root)), Root) = 0) or
            (CompareText(Copy(Folder, 1, Length(ShortRoot)), ShortRoot) = 0);
end;

{ Program Files and Windows: the viewer, run as the user, cannot write there. Setup started with "Run as
  administrator" could, so these are refused by name and not only by the write test. }
function FolderIsSystem(Folder: String): Boolean;
begin
  Result := FolderIsUnder(Folder, ExpandConstant('{commonpf64}')) or
            FolderIsUnder(Folder, ExpandConstant('{commonpf32}')) or
            FolderIsUnder(Folder, ExpandConstant('{win}'));
end;

{ The folder page. The viewer writes beside itself, so the folder must be one the user can write to, not in Program
  Files or Windows; its path must leave room for the longest installed file. On an update, a folder other than the
  installed one is confirmed first (only while the installed one still holds the viewer); an existing folder that is
  not the installed one gets Inno's own "folder exists" question (DirExistsWarning=no: asked here, after the checks,
  so one click never gets two questions).
  Also called on a silent install: a refused folder is an error there as well (a message box, as Setup's own errors
  are, unless /SUPPRESSMSGBOXES) and ends Setup (exit code 1); the questions are not asked, and the folder given is
  used. }
function NextButtonClick(CurPageID: Integer): Boolean;
var
  Folder, Previous: String;
begin
  Result := True;
  if CurPageID <> wpSelectDir then
    Exit;
  Folder := RemoveBackslashUnlessRoot(WizardDirValue);
  Previous := WizardForm.PrevAppDir;
  if Previous <> '' then
    Previous := RemoveBackslashUnlessRoot(Previous);
  if Length(AddBackslash(Folder)) + {#MaxRelPath} > 259 then
  begin
    SuppressibleMsgBox(FmtMessage(CustomMessage('FolderTooLong'), [Folder, IntToStr(258 - {#MaxRelPath})]),
      mbError, MB_OK, IDOK);
    Result := False;
  end
  else if FolderIsSystem(Folder) or not FolderIsWritable(Folder) then
  begin
    SuppressibleMsgBox(FmtMessage(CustomMessage('FolderNotWritable'), [Folder]), mbError, MB_OK, IDOK);
    Result := False;
  end
  else if WizardSilent or ((Previous <> '') and (CompareText(Previous, Folder) = 0)) then
    Result := True
  else if (Previous <> '') and FileExists(AddBackslash(Previous) + '{#MyAppExeName}') then
    Result := SuppressibleMsgBox(FmtMessage(CustomMessage('FolderMoves'), [Previous]),
      mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDYES) = IDYES
  else if DirExists(Folder) then
    Result := SuppressibleMsgBox(FmtMessage(SetupMessage(msgDirExists), [Folder]),
      mbConfirmation, MB_YESNO, IDYES) = IDYES;
end;

{ True when the shortcut exists and starts a program in the installation folder, so a shortcut to another copy of
  the viewer is left alone. Link is a path with constants, expanded here. }
function ShortcutIntoApp(Link: String): Boolean;
var
  Shell, Shortcut: Variant;
  Path, Target: String;
begin
  Result := False;
  Path := ExpandConstant(Link);
  if not FileExists(Path) then
    Exit;
  try
    Shell := CreateOleObject('WScript.Shell');
    Shortcut := Shell.CreateShortcut(Path);
    Target := Shortcut.TargetPath;
    Result := Pos(Lowercase(AddBackslash(ExpandConstant('{app}'))), Lowercase(Target)) = 1;
  except
    Result := False;
  end;
end;
