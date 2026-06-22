; WoW Model Viewer Midnight - Inno Setup installer script
; -----------------------------------------------------------------------------
; Builds a per-user installer (no admin required). WMV writes its config, log and
; the listfile cache into a "userSettings"/next-to-exe layout, so it MUST be
; installed somewhere the user can write -- hence PrivilegesRequired=lowest, which
; resolves {autopf} to %LOCALAPPDATA%\Programs. The VC++ runtime is shipped
; app-local (vcruntime140/msvcp140/concrt140.dll), so no separate redist/admin step.
;
; Compile:  "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" WoWModelViewerMidnight.iss
; Output:   installer\dist\WoWModelViewerMidnight-Setup.exe

#define MyAppName "WoW Model Viewer Midnight"
#define MyAppVersion "0.3.1"
#define MyAppPublisher "Skogdesign"
#define MyAppExeName "wowmodelviewer.exe"
; 0.3.0 is the 64-bit build: source the x64 Release tree (build64).
#define Rel "..\build64\Source\wowmodelviewer\Release"
#define IconsDir "..\bin_support\Icons"

#define MyAppURL "https://github.com/Skogdesign/WMW-2.0"

[Setup]
AppId={{B7E9F3A2-1C4D-4E8A-9F6B-2A3C5D7E9F10}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
VersionInfoVersion={#MyAppVersion}
VersionInfoProductVersion={#MyAppVersion}
DefaultDirName={autopf}\WoW Model Viewer Midnight
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=dist
; versioned output so each release has a distinct asset name
OutputBaseFilename=WoWModelViewerMidnight-Setup-{#MyAppVersion}
SetupIconFile={#IconsDir}\WMW-Midnight.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; 0.3.0 ships a 64-bit application: install into the 64-bit Program Files and require x64 Windows.
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; main executable
Source: "{#Rel}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; all runtime DLLs in the Release root: core, wow, Qt5*, OpenSSL, and the app-local
; VC++ CRT (vcruntime140/msvcp140/concrt140). The explicit file list below avoids
; shipping dev artifacts (wowdb.sqlite*, userSettings\, screenshots, log.txt).
Source: "{#Rel}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
; support data the loader reads relative to the exe
Source: "{#Rel}\listfile.csv"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Rel}\extraEncryptionKeys.csv"; DestDir: "{app}"; Flags: ignoreversion
; Qt + importer plugins, DB schema and definitions
Source: "{#Rel}\plugins\*"; DestDir: "{app}\plugins"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Rel}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Rel}\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Rel}\games\*"; DestDir: "{app}\games"; Flags: ignoreversion recursesubdirs createallsubdirs
; The 12.0 schema/data is maintained in the tracked bin_support\ tree. Ship it explicitly AFTER
; the build-staging games\* above (so it overrides) -- otherwise the installer can pick up a stale
; build-staging copy of database.xml, which silently breaks characters/races on a fresh install.
Source: "..\bin_support\wow\12.0\*"; DestDir: "{app}\games\wow\12.0"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Rel}\dbd\*"; DestDir: "{app}\dbd"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; remove runtime-generated files so uninstall leaves nothing behind
Type: filesandordirs; Name: "{app}\userSettings"
Type: files; Name: "{app}\wowdb.sqlite"
Type: files; Name: "{app}\wowdb.sqlite.build"
