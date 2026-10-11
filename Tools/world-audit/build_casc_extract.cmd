@echo off
rem Builds casc_extract.exe (the audit's read-only CASC probe) against a CascLib static library from a viewer build.
rem   build_casc_extract.cmd <casc.lib from a viewer build> <output folder>
rem e.g. build_casc_extract.cmd F:\wmv-build\ThirdParty\casclib\Release\casc.lib F:\wowmodelviewer\build\world-audit-data\bin
rem Not part of the viewer's build or installer.
setlocal
if "%~2"=="" (
  echo usage: build_casc_extract.cmd ^<casc.lib^> ^<output folder^>
  exit /b 2
)
set "HERE=%~dp0"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || exit /b 1
if not exist "%~2" mkdir "%~2"
cl /nologo /O2 /EHsc /MD /DUNICODE /D_UNICODE /I "%HERE%..\..\ThirdParty\casclib\src" "%HERE%casc_extract.cpp" ^
   /Fo"%~2\\" /Fe"%~2\casc_extract.exe" /link "%~1" advapi32.lib wininet.lib ws2_32.lib
exit /b %errorlevel%
