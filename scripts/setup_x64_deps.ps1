#Requires -Version 5.1
# setup_x64_deps.ps1 - Downloads x64 deps. Needs: VS C++ workload, vcpkg, 7-Zip
param([string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
function Write-Step([string]$msg) { Write-Host "`n>>> $msg" -ForegroundColor Cyan }
function Ensure-Dir([string]$path) { if (!(Test-Path $path)) { New-Item -ItemType Directory -Path $path -Force | Out-Null } }
function Format-Size([long]$bytes) { if ($bytes -ge 1MB) { return "{0:N1} MB" -f ($bytes / 1MB) } else { return "{0:N0} KB" -f ($bytes / 1KB) } }
function Format-Speed([double]$bps) { if ($bps -ge 1MB) { return "{0:N1} MB/s" -f ($bps / 1MB) } else { return "{0:N0} KB/s" -f ($bps / 1KB) } }
function Download-WithProgress([string]$Uri, [string]$OutFile, [string]$Label) {
    try { $consoleWidth = [Console]::WindowWidth } catch { $consoleWidth = 0 }
    if ($consoleWidth -le 0) { $consoleWidth = 120 }
    $request = [System.Net.HttpWebRequest]::Create($Uri)
    $request.AllowAutoRedirect = $true
    $response = $request.GetResponse()
    $totalBytes = $response.ContentLength
    $stream = $response.GetResponseStream()
    $fileStream = [System.IO.File]::Create($OutFile)
    $buffer = New-Object byte[] 65536
    $downloaded = [long]0
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $lastReport = [long]0
    $barWidth = 50
    try {
        while (($bytesRead = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $fileStream.Write($buffer, 0, $bytesRead)
            $downloaded += $bytesRead
            if ($sw.ElapsedMilliseconds - $lastReport -ge 250) {
                $lastReport = $sw.ElapsedMilliseconds
                $elapsed = $sw.Elapsed.TotalSeconds
                $speed = if ($elapsed -gt 0) { $downloaded / $elapsed } else { 0 }
                if ($totalBytes -gt 0) {
                    $pct = [int](($downloaded / $totalBytes) * 100)
                    $filled = [int](($downloaded / $totalBytes) * $barWidth)
                    $empty = $barWidth - $filled
                    $bar = ('#' * $filled) + ('-' * $empty)
                    $line = "  $Label  $(Format-Size $downloaded) / $(Format-Size $totalBytes)  $(Format-Speed $speed)  [$bar] $pct%"
                } else {
                    $line = "  $Label  $(Format-Size $downloaded)  $(Format-Speed $speed)"
                }
                [Console]::Write("`r$($line.PadRight($consoleWidth - 1))")
            }
        }
    } finally {
        $fileStream.Close()
        $stream.Close()
        $response.Close()
    }
    $elapsed = $sw.Elapsed.TotalSeconds
    $speed = if ($elapsed -gt 0) { $downloaded / $elapsed } else { 0 }
    if ($totalBytes -gt 0) {
        $bar = '#' * $barWidth
        $line = "  $Label  $(Format-Size $downloaded) / $(Format-Size $totalBytes)  $(Format-Speed $speed)  [$bar] 100%"
    } else {
        $line = "  $Label  $(Format-Size $downloaded)  $(Format-Speed $speed)"
    }
    [Console]::Write("`r$($line.PadRight($consoleWidth - 1))")
    [Console]::WriteLine()
}
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
$j1 = "{""name"":""wmv"",""version"":""1.0.0"",""builtin-baseline"":""$baseline"",""dependencies"":[""zlib"",""libpng""]}"
Set-Content "$tmp1\vcpkg.json" -Encoding UTF8 -Value $ExecutionContext.InvokeCommand.ExpandString($j1)
Push-Location $tmp1; & $vcpkgExe install --triplet x64-windows-static-md @vcpkgOverlay; Pop-Location
$src1 = "$tmp1\vcpkg_installed\x64-windows-static-md"
$libDst = Join-Path $RepoRoot "ThirdParty\lib\x64"; Ensure-Dir $libDst
Copy-Item "$src1\lib\libpng16.lib" "$libDst\libpng.lib" -Force
Copy-Item "$src1\lib\zlib.lib" "$libDst\zlib.lib" -Force
Write-Step "Installing FBX SDK 2020.3.9"
$fbxIncDst = Join-Path $RepoRoot "ThirdParty\include"
$fbxLibDst = Join-Path $RepoRoot "ThirdParty\lib\x64"
if (Test-Path "$fbxIncDst\fbxsdk.h") { Write-Host "  Already exists - skipping." }
else {
    $fbxTmp = Join-Path $RepoRoot "out\fbx_download"; Ensure-Dir $fbxTmp
    $fbxExe = Join-Path $fbxTmp "fbx202039_fbxsdk_vs2022_win.exe"
    if (!(Test-Path $fbxExe)) {
        Write-Host "  Downloading FBX SDK 2020.3.9 ..."
        Download-WithProgress -Uri "https://damassets.autodesk.net/content/dam/autodesk/www/files/fbx202039_fbxsdk_vs2022_win.exe" -OutFile $fbxExe -Label "FBX SDK 2020.3.9"
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
    try { Download-WithProgress -Uri "https://aka.ms/vs/17/release/vc_redist.x64.exe" -OutFile $vcredistDst -Label "vcredist_x64.exe" }
    catch { Write-Warning "Failed to download vcredist_x64.exe: $_" }
}
Write-Step "Done! Open in Visual Studio, select x64-Debug or x64-Release, and build."
