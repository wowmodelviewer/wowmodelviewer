#Requires -Version 5.1
# setup_x64_deps.ps1 - Downloads x64 deps. Needs: VS C++ workload, vcpkg, 7-Zip
param([string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
function Write-Step([string]$msg) { Write-Host "`n>>> $msg" -ForegroundColor Cyan }
function Ensure-Dir([string]$path) { if (!(Test-Path $path)) { New-Item -ItemType Directory -Path $path -Force | Out-Null } }
Write-Step "Checking prerequisites"
$vcpkgCmd = Get-Command vcpkg -EA SilentlyContinue
$vcpkgExe = if ($vcpkgCmd) { $vcpkgCmd.Source } else { $null }
if (-not $vcpkgExe) { throw "vcpkg not found. Open a VS Developer Command Prompt." }
$7zCmd = Get-Command 7z -EA SilentlyContinue
$7zExe = if ($7zCmd) { $7zCmd.Source } else { $null }
if (-not $7zExe) {
    $candidate = "$env:ProgramFiles\7-Zip\7z.exe"
    if (Test-Path $candidate) { $7zExe = $candidate }
    else { throw "7-Zip not found. Install from https://7-zip.org or ensure 7z is on PATH." }
}
$vcpkgRoot = Split-Path $vcpkgExe
$bundleJson = Join-Path $vcpkgRoot "vcpkg-bundle.json"
if (Test-Path $bundleJson) { $baseline = (Get-Content $bundleJson | ConvertFrom-Json).embeddedsha }
else { Push-Location $vcpkgRoot; $baseline = git rev-parse HEAD 2>$null; Pop-Location }
if (-not $baseline) { throw "Could not determine vcpkg baseline." }
$vcpkgOverlay = @()
if ($env:CI -eq 'true') {
    Write-Step "CI detected: using release-only triplet overlay (skipping debug builds)"
    $vcpkgOverlay = @("--overlay-triplets", "$PSScriptRoot\vcpkg-triplets")
}
Write-Step "Installing core libs via vcpkg"
$tmp1 = Join-Path $RepoRoot "out\vcpkg_setup"; Ensure-Dir $tmp1
$j1 = "{""name"":""wmv"",""version"":""1.0.0"",""builtin-baseline"":""$baseline"",""dependencies"":[""zlib"",""libpng"",""glew""]}"
Set-Content "$tmp1\vcpkg.json" -Encoding UTF8 -Value $ExecutionContext.InvokeCommand.ExpandString($j1)
Push-Location $tmp1; & $vcpkgExe install --triplet x64-windows-static-md @vcpkgOverlay; Pop-Location
$src1 = "$tmp1\vcpkg_installed\x64-windows-static-md"
$libDst = Join-Path $RepoRoot "ThirdParty\lib\x64"; Ensure-Dir $libDst
$libsDst = Join-Path $RepoRoot "ThirdParty\libs\x64"; Ensure-Dir $libsDst
Copy-Item "$src1\lib\libpng16.lib" "$libDst\libpng.lib" -Force
Copy-Item "$src1\lib\zlib.lib" "$libDst\zlib.lib" -Force
$glewLib = Get-ChildItem "$src1\lib" -Filter "*.lib" | Where-Object { $_.Name -match "glew" } | Select-Object -First 1
if (-not $glewLib) { Write-Host "Available libs:"; Get-ChildItem "$src1\lib" -Filter "*.lib" | ForEach-Object { Write-Host "  $($_.Name)" }; throw "Could not find glew lib in $src1\lib" }
Write-Host "Found glew lib: $($glewLib.Name)"
Copy-Item $glewLib.FullName "$libsDst\glew32s.lib" -Force
$glDst = Join-Path $RepoRoot "ThirdParty\GL"; Ensure-Dir $glDst
Copy-Item "$src1\include\GL\glew.h" "$glDst\glew.h" -Force
Copy-Item "$src1\include\GL\wglew.h" "$glDst\wglew.h" -Force
Copy-Item "$src1\include\GL\glxew.h" "$glDst\glxew.h" -Force
Write-Step "Installing wxWidgets via vcpkg"
$tmp2 = Join-Path $RepoRoot "out\vcpkg_wx"; Ensure-Dir $tmp2
$j2 = "{""name"":""wmv-wx"",""version"":""1.0.0"",""builtin-baseline"":""$baseline"",""overrides"":[{""name"":""wxwidgets"",""version"":""3.2.8.1""}],""dependencies"":[{""name"":""wxwidgets"",""default-features"":false}]}"
Set-Content "$tmp2\vcpkg.json" -Encoding UTF8 -Value $ExecutionContext.InvokeCommand.ExpandString($j2)
Push-Location $tmp2; & $vcpkgExe install --triplet x64-windows-static-md @vcpkgOverlay; Pop-Location
$wxSrc = "$tmp2\vcpkg_installed\x64-windows-static-md"
$wxDst = Join-Path $RepoRoot "ThirdParty\wxWidgets_x64"
if (Test-Path $wxDst) { Remove-Item $wxDst -Recurse -Force }
Ensure-Dir "$wxDst\lib\vc_x64_lib"
Copy-Item "$wxSrc\include" "$wxDst\include" -Recurse -Force
Get-ChildItem "$wxSrc\lib" -Filter "wx*.lib" | ForEach-Object { Copy-Item $_.FullName "$wxDst\lib\vc_x64_lib\$($_.Name)" -Force }
if (Test-Path "$wxSrc\debug\lib") {
    Get-ChildItem "$wxSrc\debug\lib" -Filter "wx*.lib" | ForEach-Object { Copy-Item $_.FullName "$wxDst\lib\vc_x64_lib\$($_.Name)" -Force }
    Copy-Item "$wxSrc\debug\lib\nanosvg.lib" "$wxDst\lib\vc_x64_lib\nanosvgd.lib" -Force
    Copy-Item "$wxSrc\debug\lib\nanosvgrast.lib" "$wxDst\lib\vc_x64_lib\nanosvgrastd.lib" -Force
}
Copy-Item "$wxSrc\lib\nanosvg.lib" "$wxDst\lib\vc_x64_lib\nanosvg.lib" -Force
Copy-Item "$wxSrc\lib\nanosvgrast.lib" "$wxDst\lib\vc_x64_lib\nanosvgrast.lib" -Force
if (Test-Path "$wxSrc\lib\mswu") { Copy-Item "$wxSrc\lib\mswu" "$wxDst\lib\vc_x64_lib\mswu" -Recurse -Force }
if (Test-Path "$wxSrc\lib\mswud") { Copy-Item "$wxSrc\lib\mswud" "$wxDst\lib\vc_x64_lib\mswud" -Recurse -Force }
Write-Step "Installing Qt 6.8.3 msvc2022_64"
$qtDst = Join-Path $RepoRoot "ThirdParty\Qt\6.8.3\6.8.3\msvc2022_64"
if (Test-Path $qtDst) { Write-Host "  Already exists - skipping." }
else {
    Ensure-Dir $qtDst
    $qtTmp = Join-Path $RepoRoot "out\qt_download"; Ensure-Dir $qtTmp
    $qtBaseUrl = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_683/qt6_683"
    $qtBuildTag = "6.8.3-0-202503201308"
    $qtPlatform = "Windows-Windows_11_23H2-MSVC2022-Windows-Windows_11_23H2-X86_64"
    $qtArchives = @(
        @{ Pkg = "qt.qt6.683.win64_msvc2022_64"; Name = "qtbase" },
        @{ Pkg = "qt.qt6.683.win64_msvc2022_64"; Name = "qtsvg" },
        @{ Pkg = "qt.qt6.683.win64_msvc2022_64"; Name = "qtdeclarative" },
        @{ Pkg = "qt.qt6.683.win64_msvc2022_64"; Name = "qttools" },
        @{ Pkg = "qt.qt6.683.win64_msvc2022_64"; Name = "qttranslations" },
        @{ Pkg = "qt.qt6.683.addons.qt5compat.win64_msvc2022_64"; Name = "qt5compat" }
    )
    foreach ($arc in $qtArchives) {
        $fileName = "${qtBuildTag}$($arc.Name)-${qtPlatform}.7z"
        $url = "${qtBaseUrl}/$($arc.Pkg)/${fileName}"
        $outFile = Join-Path $qtTmp $fileName
        if (!(Test-Path $outFile)) {
            Write-Host "  Downloading $($arc.Name)..."
            Invoke-WebRequest -Uri $url -OutFile $outFile -UseBasicParsing
        }
        Write-Host "  Extracting $($arc.Name)..."
        & $7zExe x $outFile -o"$qtDst" -aoa -bd | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "7z extraction failed for $fileName" }
    }
    if (!(Test-Path $qtDst)) { Write-Warning "Qt download failed - expected path not found: $qtDst" }
}
Write-Step "Installing FBX SDK 2020.3.9"
$fbxIncDst = Join-Path $RepoRoot "ThirdParty\include"
$fbxLibDst = Join-Path $RepoRoot "ThirdParty\lib\x64"
if (Test-Path "$fbxIncDst\fbxsdk.h") { Write-Host "  Already exists - skipping." }
else {
    $fbxTmp = Join-Path $RepoRoot "out\fbx_download"; Ensure-Dir $fbxTmp
    $fbxExe = Join-Path $fbxTmp "fbx202039_fbxsdk_vs2022_win.exe"
    if (!(Test-Path $fbxExe)) {
        Write-Host "  Downloading FBX SDK 2020.3.9 (~115 MB) ..."
        Invoke-WebRequest -Uri "https://damassets.autodesk.net/content/dam/autodesk/www/files/fbx202039_fbxsdk_vs2022_win.exe" -OutFile $fbxExe -UseBasicParsing
    }
    # Extract the NSIS installer with 7-Zip
    $fbxExtract = Join-Path $fbxTmp "extracted"; Ensure-Dir $fbxExtract
    Write-Host "  Extracting FBX SDK ..."
    & $7zExe x $fbxExe -o"$fbxExtract" -aoa -bd | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "7z extraction failed for FBX SDK installer" }
    # Locate the include dir inside the extraction (may be nested under $INSTDIR or similar)
    $fbxIncSrc = Get-ChildItem $fbxExtract -Recurse -Directory -Filter "include" |
                 Where-Object { Test-Path (Join-Path $_.FullName "fbxsdk.h") } |
                 Select-Object -First 1
    if (-not $fbxIncSrc) { throw "Could not find fbxsdk.h in extracted FBX SDK" }
    $fbxRoot = $fbxIncSrc.Parent.FullName
    Write-Host "  FBX SDK root: $fbxRoot"
    # Copy headers
    Ensure-Dir $fbxIncDst
    Copy-Item "$fbxRoot\include\fbxsdk.h" "$fbxIncDst\fbxsdk.h" -Force
    if (Test-Path "$fbxIncDst\fbxsdk") { Remove-Item "$fbxIncDst\fbxsdk" -Recurse -Force }
    Copy-Item "$fbxRoot\include\fbxsdk" "$fbxIncDst\fbxsdk" -Recurse -Force
    # Copy release dynamic library (lib + dll)
    Ensure-Dir $fbxLibDst
    $fbxLibFile = Get-ChildItem $fbxRoot -Recurse -Filter "libfbxsdk.lib" |
                  Where-Object { $_.FullName -match "x64[\\/]release" } |
                  Select-Object -First 1
    if (-not $fbxLibFile) { throw "Could not find libfbxsdk.lib under x64/release in extracted FBX SDK" }
    $fbxRelDir = $fbxLibFile.DirectoryName
    Write-Host "  FBX lib dir: $fbxRelDir"
    Copy-Item "$fbxRelDir\libfbxsdk.lib" "$fbxLibDst\libfbxsdk.lib" -Force
    Copy-Item "$fbxRelDir\libfbxsdk.dll" "$fbxLibDst\libfbxsdk.dll" -Force
    Write-Host "  FBX SDK 2020.3.9 installed."
}
Write-Step "Downloading vcredist_x64.exe"
$vcredistDst = Join-Path $RepoRoot "bin_support\vcredist_x64.exe"
if (Test-Path $vcredistDst) { Write-Host "  Already exists - skipping." }
else {
    try { Invoke-WebRequest -Uri "https://aka.ms/vs/17/release/vc_redist.x64.exe" -OutFile $vcredistDst -UseBasicParsing }
    catch { Write-Warning "Failed to download vcredist_x64.exe: $_" }
}
Write-Step "Done! Open in Visual Studio, select x64-Debug or x64-Release, and build."
