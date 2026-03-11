@echo off
setlocal

rem Find Visual Studio installation using vswhere
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set "VS_PATH=%%i"

if not defined VS_PATH (
    echo ERROR: Could not find Visual Studio installation.
    pause
    exit /b 1
)

call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64

echo.
echo === Building (x64 Release) ===
cmake --build out/build/x64-Release --target install

pause
