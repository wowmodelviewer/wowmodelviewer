@echo off
REM clean_all.bat - Removes all generated build artifacts and downloaded
REM dependencies to simulate a fresh clone. Run from the repo root or let
REM the script detect it automatically.
setlocal

REM Resolve repo root (parent of scripts\)
set "REPO=%~dp0.."
pushd "%REPO%"
set "REPO=%CD%"
popd

echo.
echo === Cleaning generated artifacts to simulate a fresh clone ===
echo     Repo root: %REPO%
echo.

REM -- Build output --
if exist "%REPO%\out" (
    echo Removing out\
    rmdir /s /q "%REPO%\out"
)

REM -- FBX SDK headers --
if exist "%REPO%\ThirdParty\include\fbxsdk.h" (
    echo Removing ThirdParty\include\fbxsdk.h
    del /q "%REPO%\ThirdParty\include\fbxsdk.h"
)
if exist "%REPO%\ThirdParty\include\fbxsdk" (
    echo Removing ThirdParty\include\fbxsdk\
    rmdir /s /q "%REPO%\ThirdParty\include\fbxsdk"
)

REM -- vcpkg-built libs --
if exist "%REPO%\ThirdParty\lib\x64\libpng.lib" (
    echo Removing ThirdParty\lib\x64\libpng.lib
    del /q "%REPO%\ThirdParty\lib\x64\libpng.lib"
)
if exist "%REPO%\ThirdParty\lib\x64\zlib.lib" (
    echo Removing ThirdParty\lib\x64\zlib.lib
    del /q "%REPO%\ThirdParty\lib\x64\zlib.lib"
)

REM -- FBX SDK libs --
if exist "%REPO%\ThirdParty\lib\x64\libfbxsdk.lib" (
    echo Removing ThirdParty\lib\x64\libfbxsdk.lib
    del /q "%REPO%\ThirdParty\lib\x64\libfbxsdk.lib"
)
if exist "%REPO%\ThirdParty\lib\x64\libfbxsdk.dll" (
    echo Removing ThirdParty\lib\x64\libfbxsdk.dll
    del /q "%REPO%\ThirdParty\lib\x64\libfbxsdk.dll"
)

REM -- vcredist --
if exist "%REPO%\bin_support\vcredist_x64.exe" (
    echo Removing bin_support\vcredist_x64.exe
    del /q "%REPO%\bin_support\vcredist_x64.exe"
)

REM -- bin folder (preserve userSettings\Config.ini) --
if exist "%REPO%\bin" (
    echo Cleaning bin\ (preserving userSettings\Config.ini)
    REM Save Config.ini if it exists
    if exist "%REPO%\bin\userSettings\Config.ini" (
        copy /y "%REPO%\bin\userSettings\Config.ini" "%REPO%\Config.ini.bak" >nul
    )
    rmdir /s /q "%REPO%\bin"
    mkdir "%REPO%\bin"
    REM Restore Config.ini
    if exist "%REPO%\Config.ini.bak" (
        mkdir "%REPO%\bin\userSettings"
        move /y "%REPO%\Config.ini.bak" "%REPO%\bin\userSettings\Config.ini" >nul
    )
)

echo.
echo === Clean complete. Next steps: ===
echo     1. Open a x64 Native Tools Command Prompt (or VS Developer prompt)
echo     2. cmake --preset x64-Debug   (downloads deps automatically)
echo     3. cmake --build --preset x64-Debug
echo.

endlocal
