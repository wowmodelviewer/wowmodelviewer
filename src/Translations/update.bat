@echo off
rem This file is for manually updating all translation files.

rem Set this to your WoW Model Viewer SDK folder.
set WMVSDK=..\..\..\wmv_sdk

rem Don't touch below this line!
setlocal disableDelayedExpansion
set "wmvfiles="
set "umfiles="
for /r %%F in (wowmodelviewer*.ts) do call set wmvfiles=%%wmvfiles%% "%%F"
for /r %%F in (updatemanager*.ts) do call set umfiles=%%umfiles%% "%%F"
%WMVSDK%\Qt\Bin\lupdate.exe -locations absolute ..\forms ..\wowmodelviewer -ts %wmvfiles%
%WMVSDK%\Qt\Bin\lupdate.exe -no-obsolete -locations absolute ..\UpdateManager -ts %umfiles%