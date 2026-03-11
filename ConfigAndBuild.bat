@echo off
setlocal

rem Find Visual Studio installation using vswhere
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set "VS_PATH=%%i"

if not defined VS_PATH (
    echo ERROR: Could not find Visual Studio installation.
    pause
    exit /b 1
)

echo Found Visual Studio at: %VS_PATH%

rem Set up MSVC x64 environment (required for Ninja builds)
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64

echo.
echo === Configuring (x64 Release) ===
cmake --preset x64-Release
if %ERRORLEVEL% neq 0 (
    echo Configuration failed.
    pause
    exit /b 1
)

echo.
echo === Building and Installing (x64 Release) ===
cmake --build out/build/x64-Release --target install

pause
