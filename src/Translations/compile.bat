@echo off
rem This file is for manually compiling all translation files.

rem Set this to your WoW Model Viewer SDK folder.
set WMVSDK=..\..\..\wmv_sdk

rem Don't touch below this line!
setlocal disableDelayedExpansion
set "files="
for /r %%F in (*.ts) do call set files=%%files%% "%%F"
%WMVSDK%\Qt\Bin\lrelease.exe -nounfinished %files%