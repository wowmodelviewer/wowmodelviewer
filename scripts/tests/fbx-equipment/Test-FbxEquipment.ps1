param(
  [Parameter(Mandatory=$true)][string]$Runtime,
  [Parameter(Mandatory=$true)][string]$Inspector,
  [Parameter(Mandatory=$true)][string]$OutputDirectory,
  [string]$Build = '12.1.0.69933',
  [string]$Clips = '0,1',
  [string]$Case = 'all'
)
$ErrorActionPreference = 'Stop'
$Runtime = (Resolve-Path -LiteralPath $Runtime).Path
$Inspector = (Resolve-Path -LiteralPath $Inspector).Path
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$exe = Join-Path $Runtime 'wowmodelviewer.exe'
$npc = 'creature/tyrande3/tyrande3.m2'
$racial = 'character/nightelf/female/nightelffemale_hd.m2'

function Run-Wmv([string]$Arguments, [string]$Log, [string]$Descriptor = '') {
  $statusPath = [regex]::Match($Arguments, '-fbxexport "([^"]+)"').Groups[1].Value + '.status'
  if (Test-Path -LiteralPath $statusPath) { Remove-Item -LiteralPath $statusPath }
  $info = New-Object System.Diagnostics.ProcessStartInfo
  $info.FileName = $exe
  $info.Arguments = $Arguments
  $info.WorkingDirectory = $Runtime
  $info.UseShellExecute = $false
  $info.CreateNoWindow = $true
  $info.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
  $info.EnvironmentVariables['SystemDrive'] = 'C:'
  $info.EnvironmentVariables['WMV_FBX_SELFTEST'] = '1'
  $info.EnvironmentVariables.Remove('WMV_FBX_DESCRIBE')
  if ($Descriptor) { $info.EnvironmentVariables['WMV_FBX_DESCRIBE'] = $Descriptor }
  # Keep the existing listfile deterministic: holding its download staging file makes the
  # optional network refresh use its documented on-disk fallback, without changing app settings.
  $downloadStage = Join-Path $Runtime 'listfile.csv.new'
  $stageExisted = Test-Path -LiteralPath $downloadStage
  $listLock = [IO.File]::Open($downloadStage, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
  $process = $null
  try {
    $process = [System.Diagnostics.Process]::Start($info)
    if (-not $process.WaitForExit(300000)) { $process.Kill(); throw 'WMV export timed out' }
    # WMV returns through wx OnInit(false); the .status file, not the process exit code,
    # is the export result. Capture its own logger before another run replaces it.
    Copy-Item -LiteralPath (Join-Path $Runtime 'userSettings/log.txt') -Destination $Log
  } finally {
    if ($process) { $process.Dispose() }
    $listLock.Dispose()
    if (-not $stageExisted) { Remove-Item -LiteralPath $downloadStage }
  }
}

function Write-Snapshot([string]$Path, [string]$Model, [hashtable]$Items, [bool]$Racial) {
  $entries = for ($slot = 0; $slot -lt 14; $slot++) {
    if (-not $Racial -and $slot -ne 9 -and $slot -ne 10) { continue }
    $id = 0
    if ($Items.ContainsKey($slot)) { $id = $Items[$slot] }
    '<item><slot value="{0}"/><id value="{1}"/><displayId value="-1"/><level value="0"/></item>' -f $slot,$id
  }
  $modelXml = ''
  if ($Racial) { $modelXml = '<model><file name="'+$Model+'"/><CharDetails/></model>' }
  '<SavedCharacter version="2.0" equipmentModel="'+$Model+'">'+$modelXml+'<equipment>'+($entries -join '')+'</equipment></SavedCharacter>' |
    Set-Content -LiteralPath $Path -Encoding UTF8
}

$cases = @(
  @{Name='tyrande-bow'; Model=$npc; Items=@{10=213160}; Racial=$false; Triangles=12054; Animated=$true},
  @{Name='tyrande-right'; Model=$npc; Items=@{9=19019}; Racial=$false; Triangles=12054; Animated=$false},
  @{Name='tyrande-both'; Model=$npc; Items=@{9=19019;10=213160}; Racial=$false; Triangles=12054; Animated=$false},
  @{Name='tyrande-empty'; Model=$npc; Items=@{}; Racial=$false; Triangles=12054; Animated=$false},
  @{Name='tyrande-display-only'; Model=$npc; Items=@{10=213160}; Racial=$false; Triangles=12054; Animated=$false; DisplayOnly=$true},
  # The racial body's hair/customization depends on the runtime's defaults (RandomLooks).
  # Its complete snapshot route, bow geometry and bone are stable; its triangle count is not.
  @{Name='racial-bow'; Model=$racial; Items=@{10=213160}; Racial=$true; Triangles=0; Animated=$true}
)

foreach ($test in $cases) {
  if ($Case -ne 'all' -and $Case -ne $test.Name) { continue }
  $dir = Join-Path $OutputDirectory $test.Name
  New-Item -ItemType Directory -Path $dir -Force | Out-Null
  $inputFile = Join-Path $dir 'input.chr'
  Write-Snapshot $inputFile $test.Model $test.Items $test.Racial
  $descriptorFile = Join-Path $dir 'descriptor.json'
  $producerOutput = Join-Path $dir 'producer.fbx'
  $asset = '-mo "'+$test.Model+'" -fbxequipment "'+$inputFile+'"'
  if ($test.Racial) { $asset = '"'+$inputFile+'"' }
  Run-Wmv ($asset+' -fbxexport "'+$producerOutput+'" -build "'+$Build+'"') (Join-Path $dir 'producer.log') $descriptorFile
  if ((Get-Content -LiteralPath ($producerOutput+'.status') -Raw) -ne 'OK') { throw "Producer failed: $($test.Name)" }
  $descriptor = Get-Content -LiteralPath $descriptorFile -Raw | ConvertFrom-Json
  try {
    if (-not $test.Racial -and $descriptor.assetArgs -notmatch '-fbxequipment') { throw 'NPC descriptor omitted equipment' }
    if ($test.Racial -and $descriptor.assetArgs -match '-mo|-fbxequipment') { throw 'Racial descriptor changed route' }
    if ($test.DisplayOnly) {
      # NPC equipment may identify an appearance directly, without an ItemID.
      [xml]$snapshot = Get-Content -LiteralPath $descriptor.snapshot -Raw
      $item = $snapshot.SelectSingleNode('/SavedCharacter/equipment/item[slot/@value="10"]')
      if ([int]$item.displayId.value -le 0) { throw 'Producer did not resolve the bow appearance' }
      $item.id.SetAttribute('value','-1')
      $snapshot.Save($descriptor.snapshot)
    }
    Copy-Item -LiteralPath $descriptor.snapshot -Destination (Join-Path $dir 'saved-equipment.chr')
    $output = Join-Path $dir 'model.fbx'
    $anim = 0
    if ($test.Animated) { $anim = 1 }
    Run-Wmv ($descriptor.assetArgs+' -fbxexport "'+$output+'" -fbxcomponent -fbxanim '+$anim+' -fbxclips '+$Clips+' -build "'+$Build+'"') (Join-Path $dir 'export.log')
    if ((Get-Content -LiteralPath ($output+'.status') -Raw) -ne 'OK') { throw "Export failed: $($test.Name)" }
    $json = & $Inspector $output
    if ($LASTEXITCODE -ne 0) { throw 'Independent FBX import failed' }
    $json | Set-Content -LiteralPath (Join-Path $dir 'inspection.json') -Encoding UTF8
    $result = $json | ConvertFrom-Json
    $body = @($result.meshes | Where-Object { $_.name -eq $test.Model.Replace('.m2','') })
    if ($body.Count -ne 1 -or $body[0].triangles -le 0 -or
        ($test.Triangles -gt 0 -and $body[0].triangles -ne $test.Triangles)) { throw "Unexpected body geometry: $($test.Name)" }
    if (@($result.meshes).Count -ne (1+$test.Items.Count)) { throw "Wrong equipment geometry count: $($test.Name)" }
    $attachments = @($result.meshes | Where-Object { $_.name -ne $body[0].name })
    foreach ($mesh in $attachments) {
      if (-not $mesh.boneParent -or $mesh.materials -lt 1 -or @($mesh.textures).Count -lt 1) { throw 'Attachment lost bone/material/texture' }
    }
    if ($test.Items.ContainsKey(10)) {
      $bow = @($attachments | Where-Object { $_.name -eq 'item/objectcomponents/weapon/bow_1h_430nightelf_c_01' })
      $bone = 223
      if ($test.Racial) { $bone = 234 }
      if ($bow.Count -ne 1 -or $bow[0].triangles -ne 1972 -or $bow[0].parent -notlike "*_bone_$bone") { throw 'Incorrect bow geometry/attachment bone' }
      if (-not (Test-Path -LiteralPath (Join-Path $dir 'bow_1h_430nightelf_c_01_0_unit0.png'))) { throw 'Missing raw bow texture' }
      if ($test.Animated) {
        if (@($bow[0].motion | Where-Object { $_.localError -gt 0.00001 }).Count) { throw 'Bow slips relative to attachment bone' }
        if (-not @($bow[0].motion | Where-Object { $_.movement -gt 0.01 }).Count) { throw 'Bow does not follow animated skeleton' }
      }
    }
    if ($test.Animated -and @($result.clips).Count -ne ($Clips.Split(',').Count)) { throw 'Selected animation count changed' }
    Write-Output ("PASS {0}: meshes={1}, bodyTriangles={2}, clips={3}" -f $test.Name,@($result.meshes).Count,$body[0].triangles,@($result.clips).Count)
  } finally {
    if ($descriptor.snapshot -and (Test-Path -LiteralPath $descriptor.snapshot)) { Remove-Item -LiteralPath $descriptor.snapshot }
  }
}

if ($Case -eq 'invalid-snapshots') {
  $dir = Join-Path $OutputDirectory 'invalid-snapshots'
  New-Item -ItemType Directory -Path $dir -Force | Out-Null
  $valid = Join-Path $dir 'valid.chr'
  Write-Snapshot $valid $npc @{10=213160} $false
  $text = Get-Content -LiteralPath $valid -Raw
  $invalid = @{
    'malformed' = '<SavedCharacter'
    'wrong-model' = $text.Replace($npc,'creature/chicken2/chicken2.m2')
    'missing-slot' = $text -replace '<item><slot value="9"/>.*?</item>',''
    'missing-item' = $text.Replace('213160','999999999')
  }
  foreach ($name in $invalid.Keys) {
    $path = Join-Path $dir ($name+'.chr')
    $invalid[$name] | Set-Content -LiteralPath $path -Encoding UTF8
    $output = Join-Path $dir ($name+'.fbx')
    Run-Wmv ('-mo "'+$npc+'" -fbxequipment "'+$path+'" -fbxexport "'+$output+'" -build "'+$Build+'"') (Join-Path $dir ($name+'.log'))
    $status = Get-Content -LiteralPath ($output+'.status') -Raw
    if (-not $status.StartsWith('ERROR') -or (Test-Path -LiteralPath $output)) { throw "Invalid snapshot accepted: $name" }
    Write-Output "PASS rejected $name without writing an FBX"
  }
}
