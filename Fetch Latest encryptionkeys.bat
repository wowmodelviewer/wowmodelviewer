@echo off

echo Deleting old extraEncryptionKeys.csv if present.
del extraEncryptionKeys.csv

echo Fetching latest encryption keys from https://raw.githubusercontent.com/wowdev/TACTKeys/master/WoW.txt
curl -LO "https://raw.githubusercontent.com/wowdev/TACTKeys/master/WoW.txt"

echo Renaming WoW.txt to extraEncryptionKeys.csv
ren WoW.txt extraEncryptionKeys.csv

echo Replacing all spaces with semicolons in extraEncryptionKeys.csv to allow it to work with WoWModelViewer
setlocal enabledelayedexpansion
(for /f "usebackq delims=" %%a in ("extraEncryptionKeys.csv") do (
    set "line=%%a"
    set "line=!line: =;!"
    set "line=!line:  =;!"
    echo !line!
)) > temp.csv
del extraEncryptionKeys.csv
ren temp.csv extraEncryptionKeys.csv


