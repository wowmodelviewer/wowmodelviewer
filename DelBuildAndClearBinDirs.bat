@echo off

rem Remove CMake build output (new preset-based layout)
if exist "out" (
    echo Deleting out/ build directory...
    rmdir /s /q out
)

rem Remove legacy build directory if present
if exist "build" (
    echo Deleting legacy build/ directory...
    rmdir /s /q build
)

cd bin

set "EXCEPTION_FILE1=COPYING"
set "EXCEPTION_FILE2=wowmodelviewer.exe - Shortcut.lnk"
set "EXCEPTION_DIR=userSettings"

rem Loop through files only
for /f "delims=" %%F in ('dir /b /a:-d') do (
    if /I not "%%F"=="%EXCEPTION_FILE1%" if /I not "%%F"=="%EXCEPTION_FILE2%" (
        echo Deleting file: %%F
        del "%%F"
    )
)

rem Loop through directories only
for /d %%D in (*) do (
    if /I not "%%D"=="%EXCEPTION_DIR%" (
        echo Deleting dir: %%D
        rd /s /q "%%D"
    )
)
