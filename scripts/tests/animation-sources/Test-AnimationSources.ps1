param(
 [Parameter(Mandatory=$true)][string]$Runtime,
 [Parameter(Mandatory=$true)][string]$RegressionExe,
 [Parameter(Mandatory=$true)][string]$RawDirectory,
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [string]$Build='12.1.0.69933',
 [switch]$ViewerOnly
)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$Runtime=(Resolve-Path -LiteralPath $Runtime).Path
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
function Run-Hidden([string]$Exe,[string]$Arguments,[string]$Label,[hashtable]$Environment=@{}) {
 $info=New-Object Diagnostics.ProcessStartInfo
 $info.FileName=$Exe; $info.Arguments=$Arguments; $info.WorkingDirectory=$Runtime
 $info.UseShellExecute=$false; $info.CreateNoWindow=$true; $info.WindowStyle='Hidden'
 $info.EnvironmentVariables['SystemDrive']='C:'
 foreach($key in $Environment.Keys){$info.EnvironmentVariables[$key]=$Environment[$key]}
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 # Remove only logs in this isolated test runtime, so a failed startup cannot
 # pass by reusing a previous process's success marker.
 if($Exe.EndsWith('wowmodelviewer.exe')) {
  foreach($log in @('log.txt','unityRenderer.log')) {
   $logPath=Join-Path $Runtime "userSettings/$log"
   if(Test-Path -LiteralPath $logPath){Remove-Item -LiteralPath $logPath}
  }
 }
 $process=[Diagnostics.Process]::Start($info)
 $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
 try {
  if(-not $process.WaitForExit(600000)){$process.Kill();throw "$Label timed out"}
  $stdout.Result | Set-Content "$OutputDirectory/$Label.stdout.txt"
  $stderr.Result | Set-Content "$OutputDirectory/$Label.stderr.txt"
  $process.ExitCode | Set-Content "$OutputDirectory/$Label.exitcode.txt"
  if($Exe.EndsWith('wowmodelviewer.exe')){
   Copy-Item "$Runtime/userSettings/log.txt" "$OutputDirectory/$Label.log"
   if(Test-Path "$Runtime/userSettings/unityRenderer.log"){Copy-Item "$Runtime/userSettings/unityRenderer.log" "$OutputDirectory/$Label.unity.log"}
   # wx returns -1 when a successful headless OnInit finishes without its GUI loop.
   if($process.ExitCode -notin @(-1,0)){throw "$Label crashed: $($process.ExitCode)"}
  } else {if($process.ExitCode -ne 0){throw "$Label failed: $($process.ExitCode)"}}
 } finally {$process.Dispose()}
}
# Lock only the optional listfile refresh staging file, preserving the existing local listfile.
$stage="$Runtime/listfile.csv.new";$existed=Test-Path $stage
$lock=[IO.File]::Open($stage,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try {
 $fbx="$OutputDirectory/HumanMale-corrected.fbx"
 if(-not $ViewerOnly) {
 if(Test-Path $fbx){throw 'Output already exists; use a new directory'}
 Run-Hidden "$Runtime/wowmodelviewer.exe" ('-mo character/human/male/humanmale_hd.m2 -build '+$Build+' -fbxexport "'+$fbx+'" -fbxmesh 0 -fbxskin 0 -fbxclips 44,48,67,68,72,78,113,124,139,140,141,142,143,155') 'export' @{WMV_FBX_SELFTEST='1'}
 if((Get-Content "$fbx.status" -Raw).Trim() -ne 'OK'){throw 'Export failed'}
 Run-Hidden $RegressionExe ('"'+$RawDirectory+'" "'+$fbx+'" "'+$OutputDirectory+'/comparison.json"') 'regression'
 }
 $sequence='anim:#139;anim:#140;anim:#141;anim:#142;anim:#143;anim:#78;anim:#218;anim:#44;anim:#48;anim:#67;anim:#68;anim:#72;anim:#113;anim:#124;anim:#155'
 Run-Hidden "$Runtime/wowmodelviewer.exe" ('-mo character/human/male/humanmale_hd.m2 -build '+$Build+' -unityipctest') 'unity' @{WMV_IPCTEST_SEQUENCE=$sequence}
 if(-not (Select-String "$OutputDirectory/unity.log" -Pattern '\[unityipc-test\] RESULT: PASS')){throw 'Unity test did not pass'}
 if(Select-String @("$OutputDirectory/unity.log","$OutputDirectory/unity.unity.log") -Pattern 'file is busy in the viewer|failed to fetch|not playable'){throw 'A selected animation could not load'}
 foreach($index in @(139,140,141,142,143,78,218,44,48,67,68,72,113,124,155)) {
  $pattern='sequence \['+$index+'\].* [1-9][0-9]* (of [0-9]+ )?bone\(s\) move'
  if(-not (Select-String "$OutputDirectory/unity.unity.log" -Pattern $pattern)){throw "No moving animation for sequence $index"}
 }
 if($ViewerOnly){Write-Output "PASS: hidden Unity sequence; evidence $OutputDirectory"}
 else {Write-Output "PASS: export, independent curve comparison and hidden Unity sequence; evidence $OutputDirectory"}
} finally {$lock.Dispose();if(-not $existed){Remove-Item -LiteralPath $stage}}
