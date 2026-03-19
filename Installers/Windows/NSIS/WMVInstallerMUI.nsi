!include "MUI2.nsh"

# Root of the repository relative to this .nsi file.
!define wmvroot "..\..\..\" 

# Name of the installer
Name "WoW Model Viewer"

# set the name of the installer
outFile "${wmvroot}bin\WMV_Installer.exe"

# Installer / uninstaller icon (the restored WMV icon).
!define MUI_ICON   "${wmvroot}bin_support\Icons\wmv.ico"
!define MUI_UNICON "${wmvroot}bin_support\Icons\wmv.ico"

# force installed app to be run as admin
RequestExecutionLevel admin

# custom header fo all pages
# setup header banner
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "..\NSIS_header.bmp"

# License page
!insertmacro MUI_PAGE_LICENSE "..\..\License.rtf"

!insertmacro MUI_PAGE_DIRECTORY
#!define MUI_PAGE_HEADER_TEXT "Installation directory choice"
# set desktop as install directory
InstallDir $PROGRAMFILES64\WoWModelViewer

!insertmacro MUI_PAGE_INSTFILES 

!insertmacro MUI_LANGUAGE "English"

############################
# begin of install section #
############################
Section "Install"
 
# define output path
setOutPath $INSTDIR
 
# specify file to go in output path
File "${wmvroot}bin\wowmodelviewer.exe"
File "${wmvroot}bin\*.dll"

# Install icon files for shortcuts and runtime window icon.
File "${wmvroot}bin_support\Icons\wmv.ico"
File "${wmvroot}bin_support\Icons\wmv_16.png"

# Install bundled fonts for the UI font selector.
SetOutPath $INSTDIR\fonts
File /r "${wmvroot}bin_support\fonts\*.*"

# Auto-include all WoW version directories
# To add a new version, just create a new folder under bin_support\wow\.
SetOutPath $INSTDIR\games\wow
File /r "${wmvroot}bin_support\wow\*.*"

CreateDirectory $INSTDIR\localisation
SetOutPath $INSTDIR\localisation
File "${wmvroot}bin_support\localisation\*.mo"

CreateDirectory $INSTDIR\userSettings

# create shortcuts
setOutPath $INSTDIR
CreateShortCut "$DESKTOP\WoW Model Viewer.lnk" "$INSTDIR\wowmodelviewer.exe" "" "$INSTDIR\wmv.ico"
 
# create start-menu items
CreateDirectory "$SMPROGRAMS\WoW Model Viewer"
CreateShortCut "$SMPROGRAMS\WoW Model Viewer\Uninstall.lnk" "$INSTDIR\uninstaller.exe" "" "$INSTDIR\uninstaller.exe" 0
CreateShortCut "$SMPROGRAMS\WoW Model Viewer\WoW Model Viewer.lnk" "$INSTDIR\wowmodelviewer.exe" "" "$INSTDIR\wmv.ico" 0

WriteRegStr HKLM "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\layers" "$INSTDIR\wowmodelviewer.exe" "RUNASADMIN"
WriteRegStr HKCU "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\layers" "$INSTDIR\wowmodelviewer.exe" "RUNASADMIN"

# define uninstaller name
writeUninstaller $INSTDIR\uninstaller.exe

# install vcredist package and launch if not found
ReadRegDword $0 HKLM "SOFTWARE\Microsoft\DevDiv\vc\Servicing\14.0\RuntimeMinimum" "Install"
${If} $0 == ""
File "${wmvroot}bin_support\vcredist_x64.exe"
ExecWait '"$INSTDIR\vcredist_x64.exe" /install /quiet /norestart'
Delete "$INSTDIR\vcredist_x64.exe"
${EndIf}

sectionEnd
##########################
# end of install section #
##########################
 
# create a section to define what the uninstaller does.
# the section will always be named "Uninstall"
section "Uninstall"
 
# Always delete uninstaller first
delete $INSTDIR\uninstaller.exe
 
# now delete installed file
RMDir /r "$INSTDIR\*.*"    
 
# Remove the installation directory
RMDir "$INSTDIR"
 
# Delete Shortcuts
Delete "$DESKTOP\WoW Model Viewer.lnk"
Delete "$SMPROGRAMS\WoW Model Viewer\*.*"
RMDir "$SMPROGRAMS\WoW Model Viewer"

# cleanup reg
DeleteRegKey HKLM "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\layers"
DeleteRegKey HKCU "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\layers"
 
sectionEnd
