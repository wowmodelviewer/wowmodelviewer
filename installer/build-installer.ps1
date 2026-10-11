<#
.SYNOPSIS
  Stages the WoW Model Viewer release folder and compiles the installer (installer\WoWModelViewer.iss).

.DESCRIPTION
  The release is an exact list of files, nothing found by a wildcard in a build folder: the program and its
  plugins from the CMake build (Release), the wxWidgets, Qt, OpenSSL and Visual C++ runtime DLLs it loads, the
  Unity player, the data files from bin_support, and the licence texts and README. A file that is missing stops
  the build. The staged folder is what the installer installs, file for file.

  Afterwards installer\dist holds WoWModelViewer-Setup-<version>.exe and its SHA-256 (.sha256), and the stage's
  own folder holds stage-manifest.csv: every staged file with its size and SHA-256.

.EXAMPLE
  .\build-installer.ps1 -BuildDir F:\build64 -PlayerDir F:\player\tools\unity-renderer -OpenSslDir F:\openssl_x64
#>
param(
  # The CMake build folder, with the Release configuration built (wowmodelviewer, armory, wowhead, objexporter).
  [Parameter(Mandatory = $true)][string]$BuildDir,
  # The built Unity player: the folder that holds UnityRenderer.exe.
  [Parameter(Mandatory = $true)][string]$PlayerDir,
  # OpenSSL 1.1 for 64-bit Windows: libssl-1_1-x64.dll and libcrypto-1_1-x64.dll (Qt's network module loads them).
  [Parameter(Mandatory = $true)][string]$OpenSslDir,
  # Qt's bin folder. Default: the Qt the build was configured with (Qt5Core_DIR in its CMakeCache.txt).
  [string]$QtBinDir = '',
  # The wxWidgets 3.3 DLL folder. Default: ThirdParty\wxWidgets33\lib\vc14x_x64_dll in this repository.
  [string]$WxDllDir = '',
  # The Visual C++ runtime (x64 Microsoft.VC14x.CRT). Default: the newest one under the Visual Studio installations.
  [string]$VcRedistDir = '',
  [string]$Version = '1.0',
  [string]$Stage = '',
  # Test builds only: another AppId and output name, so a test never touches a real installation.
  [string]$AppId = '',
  [string]$OutputBase = '',
  [string]$Iscc = '',
  [switch]$StageOnly
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not $Stage) { $Stage = Join-Path $PSScriptRoot 'stage' }
# ISCC resolves a relative path against the script's folder, not this one: hand it an absolute one.
$Stage = $PSCmdlet.GetUnresolvedProviderPathFromPSPath($Stage)

function Fail([string]$message) { throw "build-installer: $message" }

if ($AppId -and -not $OutputBase) { Fail '-AppId is for test builds: give -OutputBase too, so a test never takes the release name' }

if (-not $QtBinDir) {
  $cache = Join-Path $BuildDir 'CMakeCache.txt'
  if (-not (Test-Path $cache)) { Fail "no CMakeCache.txt in $BuildDir; pass -QtBinDir" }
  $line = Select-String -Path $cache -Pattern '^Qt5Core_DIR:PATH=(.+)$' | Select-Object -First 1
  if (-not $line) { Fail "Qt5Core_DIR is not in $cache; pass -QtBinDir" }
  # <qt>/lib/cmake/Qt5Core -> <qt>/bin
  $QtBinDir = Join-Path (Split-Path (Split-Path (Split-Path $line.Matches[0].Groups[1].Value))) 'bin'
}
if (-not $WxDllDir) { $WxDllDir = Join-Path $repo 'ThirdParty\wxWidgets33\lib\vc14x_x64_dll' }
if (-not $VcRedistDir) {
  # <VS>\<year>\<edition>\VC\Redist\MSVC\<version>\x64\Microsoft.VC14x.CRT
  $roots = @("${env:ProgramFiles}\Microsoft Visual Studio", "${env:ProgramFiles(x86)}\Microsoft Visual Studio") | Where-Object { Test-Path $_ }
  $crt = foreach ($r in $roots) { Get-Item "$r\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC14*.CRT" -ErrorAction SilentlyContinue }
  $VcRedistDir = ($crt | Where-Object { $_.Parent.Parent.Name -match '^\d+\.\d+' } |
    Sort-Object { [version]$_.Parent.Parent.Name } | Select-Object -Last 1).FullName
  if (-not $VcRedistDir) { Fail 'no x64 Microsoft.VC14x.CRT folder found; pass -VcRedistDir' }
}
if (-not $Iscc) {
  $Iscc = @("${env:LOCALAPPDATA}\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "${env:ProgramFiles}\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
}

# Destination in the install folder -> source.
$files = [ordered]@{}
$rel = 'Release'
$files['wowmodelviewer.exe'] = "$BuildDir\Source\wowmodelviewer\$rel\wowmodelviewer.exe"
$files['core.dll'] = "$BuildDir\Source\core\$rel\core.dll"
$files['wow.dll'] = "$BuildDir\Source\games\wow\$rel\wow.dll"
$files['plugins\armory.dll'] = "$BuildDir\Source\plugins\importers\armory\$rel\armory.dll"
$files['plugins\wowhead.dll'] = "$BuildDir\Source\plugins\importers\wowhead\$rel\wowhead.dll"
$files['plugins\objexporter.dll'] = "$BuildDir\Source\plugins\exporters\obj\$rel\objexporter.dll"
foreach ($n in 'wxbase333u_vc14x_x64.dll', 'wxmsw333u_core_vc14x_x64.dll', 'wxmsw333u_aui_vc14x_x64.dll') { $files[$n] = Join-Path $WxDllDir $n }
# Only a QCoreApplication is made (no Qt windows), so no Qt platform or image plugins are needed.
foreach ($n in 'Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Network.dll', 'Qt5Xml.dll') { $files[$n] = Join-Path $QtBinDir $n }
foreach ($n in 'libssl-1_1-x64.dll', 'libcrypto-1_1-x64.dll') { $files[$n] = Join-Path $OpenSslDir $n }
foreach ($n in 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll') { $files[$n] = Join-Path $VcRedistDir $n }
foreach ($n in 'AnimationData.csv', 'ChrRacesCustomization.csv', 'database.xml') { $files["games\wow\12.0\$n"] = "$repo\bin_support\wow\12.0\$n" }
foreach ($f in Get-ChildItem "$repo\bin_support\dbd" -Filter '*.dbd') { $files["dbd\$($f.Name)"] = $f.FullName }
# Not shipped: the community file list and the encryption key list. The viewer downloads both when it opens a client,
# and neither source publishes a licence to redistribute them under.
$files['LICENSE.txt'] = "$repo\LICENSE"
$files['THIRD-PARTY-NOTICES.txt'] = "$repo\THIRD-PARTY-NOTICES.txt"
$files['README.txt'] = "$PSScriptRoot\README.txt"
# The Unity player: what a Windows player build is made of (its program, the Unity runtime, DirectX 12 and
# DirectStorage, Mono and the player's data), whole. Anything else in the folder -- the debug information Unity marks
# as not for shipping, a log or a crash dump from a test run -- stays out.
$playerRoot = (Get-Item -LiteralPath $PlayerDir).FullName.TrimEnd('\')
foreach ($n in 'UnityRenderer.exe', 'UnityPlayer.dll', 'UnityCrashHandler64.exe', 'dstorage.dll', 'dstoragecore.dll') {
  $files["tools\unity-renderer\$n"] = Join-Path $playerRoot $n
}
foreach ($d in 'D3D12', 'MonoBleedingEdge', 'UnityRenderer_Data') {
  if (-not (Test-Path -LiteralPath (Join-Path $playerRoot $d) -PathType Container)) { Fail "no $d folder in $PlayerDir" }
  foreach ($f in Get-ChildItem -LiteralPath (Join-Path $playerRoot $d) -Recurse -File) {
    $files["tools\unity-renderer\" + $f.FullName.Substring($playerRoot.Length + 1)] = $f.FullName
  }
}
$unexpected = Get-ChildItem -LiteralPath $playerRoot | Where-Object {
  $_.Name -notin 'UnityRenderer.exe', 'UnityPlayer.dll', 'UnityCrashHandler64.exe', 'dstorage.dll', 'dstoragecore.dll',
    'D3D12', 'MonoBleedingEdge', 'UnityRenderer_Data' -and $_.Name -notlike '*_DoNotShip' }
if ($unexpected) { Write-Warning ("left out of the player: " + (($unexpected | ForEach-Object { $_.Name }) -join ', ')) }

$missing = @($files.GetEnumerator() | Where-Object { -not (Test-Path -LiteralPath $_.Value -PathType Leaf) } | ForEach-Object { "  $($_.Key) <- $($_.Value)" })
if ($missing.Count) { Fail ("missing files:`n" + ($missing -join "`n")) }

# The program's own version (its version resource) must be the one the installer says it installs.
$exeVersion = (Get-Item $files['wowmodelviewer.exe']).VersionInfo.ProductVersion
if ($exeVersion -ne $Version) { Fail "wowmodelviewer.exe is version '$exeVersion', not '$Version' (GlobalSettings.cpp, wmv.rc)" }

if (Test-Path $Stage) {
  # Only an earlier stage is replaced: a folder holding the staged program, or an empty one.
  $isStage = (Test-Path (Join-Path $Stage 'wowmodelviewer.exe')) -and (Test-Path (Join-Path $Stage 'tools\unity-renderer\UnityRenderer.exe'))
  if (-not $isStage -and (Get-ChildItem -LiteralPath $Stage -Force | Select-Object -First 1)) { Fail "$Stage exists and is not an earlier stage; choose another -Stage" }
  Remove-Item -LiteralPath $Stage -Recurse -Force
}
New-Item -ItemType Directory -Force $Stage | Out-Null
$manifest = foreach ($e in $files.GetEnumerator()) {
  $dst = Join-Path $Stage $e.Key
  New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
  Copy-Item -LiteralPath $e.Value -Destination $dst
  [pscustomobject]@{ File = $e.Key; Bytes = (Get-Item -LiteralPath $dst).Length; SHA256 = (Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash; Source = $e.Value }
}
$manifestPath = Join-Path (Split-Path $Stage -Parent) 'stage-manifest.csv'
$manifest | Export-Csv -NoTypeInformation -Encoding UTF8 $manifestPath
'Staged {0} files, {1:N1} MB -> {2}' -f $manifest.Count, (($manifest | Measure-Object Bytes -Sum).Sum / 1MB), $Stage
if ($StageOnly) { return }

if (-not $Iscc -or -not (Test-Path $Iscc)) { Fail 'ISCC.exe (Inno Setup 6.7 or later) not found; pass -Iscc' }
# The longest installed path below the install folder: the folder page refuses a folder that would take it past
# Windows' path limit.
$maxRelPath = ($files.Keys | Measure-Object -Property Length -Maximum).Maximum
$isccArgs = @("/DStage=$Stage", "/DMyAppVersion=$Version", "/DMaxRelPath=$maxRelPath")
if ($AppId) { $isccArgs += "/DMyAppId=$AppId" }
if ($OutputBase) { $isccArgs += "/DOutputBase=$OutputBase" }
$isccArgs += '/Q', (Join-Path $PSScriptRoot 'WoWModelViewer.iss')
& $Iscc @isccArgs
if ($LASTEXITCODE -ne 0) { Fail "ISCC failed (exit $LASTEXITCODE)" }

$base = if ($OutputBase) { $OutputBase } else { "WoWModelViewer-Setup-$Version" }
$setup = Join-Path $PSScriptRoot "dist\$base.exe"
$hash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$setup.sha256", "$hash *$base.exe`n")   # the sha256sum format, LF-ended
'{0} ({1:N1} MB)  SHA-256 {2}' -f $setup, ((Get-Item $setup).Length / 1MB), $hash
