#Requires -Version 5.1
# setup_x64_deps.ps1 - Downloads x64 deps. Needs: VS C++ workload, Python 3, vcpkg
param([string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
function Write-Step([string]$msg) { Write-Host "`n>>> $msg" -ForegroundColor Cyan }
function Ensure-Dir([string]$path) { if (!(Test-Path $path)) { New-Item -ItemType Directory -Path $path -Force | Out-Null } }
Write-Step "Checking prerequisites"
$vcpkgExe = (Get-Command vcpkg -EA SilentlyContinue).Source
if (-not $vcpkgExe) { throw "vcpkg not found. Open a VS Developer Command Prompt." }
$pythonExe = (Get-Command python -EA SilentlyContinue).Source
if (-not $pythonExe) { throw "python not found. Python 3 required for Qt download." }
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
$j1 = "{""name"":""wmv"",""version"":""1.0.0"",""builtin-baseline"":""$baseline"",""dependencies"":[""zlib"",""libpng"",""libjpeg-turbo"",""glew""]}"
Set-Content "$tmp1\vcpkg.json" -Encoding UTF8 -Value $ExecutionContext.InvokeCommand.ExpandString($j1)
Push-Location $tmp1; & $vcpkgExe install --triplet x64-windows-static-md @vcpkgOverlay; Pop-Location
$src1 = "$tmp1\vcpkg_installed\x64-windows-static-md\lib"
$libDst = Join-Path $RepoRoot "ThirdParty\lib\x64"; Ensure-Dir $libDst
$libsDst = Join-Path $RepoRoot "ThirdParty\libs\x64"; Ensure-Dir $libsDst
Copy-Item "$src1\jpeg.lib" "$libDst\jpeg.lib" -Force
Copy-Item "$src1\libpng16.lib" "$libDst\libpng.lib" -Force
Copy-Item "$src1\zlib.lib" "$libDst\zlib.lib" -Force
$glewLib = Get-ChildItem "$src1" -Filter "*.lib" | Where-Object { $_.Name -match "glew" } | Select-Object -First 1
if (-not $glewLib) { Write-Host "Available libs:"; Get-ChildItem "$src1" -Filter "*.lib" | ForEach-Object { Write-Host "  $($_.Name)" }; throw "Could not find glew lib in $src1" }
Write-Host "Found glew lib: $($glewLib.Name)"
Copy-Item $glewLib.FullName "$libsDst\glew32s.lib" -Force
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
Write-Step "Installing OpenSSL 1.1.x DLLs via vcpkg (required by Qt 5.13.2)"
$tmp3 = Join-Path $RepoRoot "out\vcpkg_ssl"; Ensure-Dir $tmp3
$j3 = "{""name"":""wmv-ssl"",""version"":""1.0.0"",""builtin-baseline"":""$baseline"",""overrides"":[{""name"":""openssl"",""version"":""1.1.1n"",""port-version"":1}],""dependencies"":[""openssl""]}"
Set-Content "$tmp3\vcpkg.json" -Encoding UTF8 -Value $ExecutionContext.InvokeCommand.ExpandString($j3)
Push-Location $tmp3; & $vcpkgExe install --triplet x64-windows @vcpkgOverlay; Pop-Location
$sslBin = "$tmp3\vcpkg_installed\x64-windows\bin"
Copy-Item "$sslBin\libssl-1_1-x64.dll" (Join-Path $RepoRoot "ThirdParty\lib\libssl-1_1-x64.dll") -Force
Copy-Item "$sslBin\libcrypto-1_1-x64.dll" (Join-Path $RepoRoot "ThirdParty\lib\libcrypto-1_1-x64.dll") -Force
Write-Step "Installing Qt 5.13.2 msvc2017_64"
$qtDst = Join-Path $RepoRoot "ThirdParty\Qt\5.13.2\msvc2017_64"
if (Test-Path $qtDst) { Write-Host "  Already exists - skipping." }
else {
    & $pythonExe -m pip show aqtinstall 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { & $pythonExe -m pip install aqtinstall }
    $qtTmp = Join-Path $RepoRoot "out\qt_download"; Ensure-Dir $qtTmp
    & $pythonExe -m aqt install-qt windows desktop 5.13.2 win64_msvc2017_64 --outputdir $qtTmp
    $qtSrc = Join-Path $qtTmp "5.13.2\msvc2017_64"
    if (Test-Path $qtSrc) { Ensure-Dir (Split-Path $qtDst); Move-Item $qtSrc $qtDst -Force }
    else { Write-Warning "Qt download failed." }
}
Write-Step "Downloading vcredist_x64.exe"
$vcredistDst = Join-Path $RepoRoot "bin_support\vcredist_x64.exe"
if (Test-Path $vcredistDst) { Write-Host "  Already exists - skipping." }
else {
    try { Invoke-WebRequest -Uri "https://aka.ms/vs/17/release/vc_redist.x64.exe" -OutFile $vcredistDst -UseBasicParsing }
    catch { Write-Warning "Failed to download vcredist_x64.exe: $_" }
}
Write-Step "Done! Open in Visual Studio, select x64-Debug or x64-Release, and build."
