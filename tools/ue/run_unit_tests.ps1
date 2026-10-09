# The C++ automation tests (Source/.../Tests/: MRNetTests, MRCharInfoTests, MRWorldTests): the protocol,
# chat, character creation and rooms built at runtime. Headless, no server needed. Exits 0 when every
# test passes.
#
#   powershell -File tools/ue/run_unit_tests.ps1                 (every Meridian test)
#   powershell -File tools/ue/run_unit_tests.ps1 -Filter Meridian.Net.Chat
param(
    [string]$Filter = "Meridian",
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe",
    [int]$TimeoutSeconds = 900
)

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\MeridianRemastered\MeridianRemastered.uproject"
$log = Join-Path $repo "game\MeridianRemastered\Saved\Logs\unittests.log"
if (Test-Path $log) { Remove-Item $log }

$cmdArgs = "`"$proj`" -ExecCmds=`"Automation RunTests $Filter;Quit`" -unattended -nullrhi -nosound -nosplash -stdout -abslog=`"$log`""
$p = Start-Process -FilePath $Engine -PassThru -NoNewWindow -RedirectStandardOutput "$log.stdout" -ArgumentList $cmdArgs
if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Write-Host "FAIL: timed out after $TimeoutSeconds s (log: $log)"
    exit 1
}
$results = Select-String -Path $log -Pattern "Test Completed\. Result=\{(\w+)\} Name=\{([^}]*)\} Path=\{([^}]*)\}"
$failed = @()
foreach ($r in $results) {
    $result, $path = $r.Matches[0].Groups[1].Value, $r.Matches[0].Groups[3].Value
    if ($result -ne "Success") { $failed += "$result  $path" }
}
if ($results.Count -eq 0) { Write-Host "FAIL: no tests ran for '$Filter' (log: $log)"; exit 1 }
$failed | ForEach-Object { Write-Host "  $_" }
Write-Host ("Unit tests ({0}): {1}/{2} passed (log: {3})" -f $Filter, ($results.Count - $failed.Count), $results.Count, $log)
if ($failed.Count) { exit 1 }
exit 0
