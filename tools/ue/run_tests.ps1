# The tests that need no server, one after another: the C++ unit tests, then the movement test.
# `-Online` adds the online smoke test (needs the Shards dev stack: `npm run dev` in meridian-browser).
#
#   powershell -File tools/ue/run_tests.ps1
#   powershell -File tools/ue/run_tests.ps1 -Online
param([switch]$Online)
$suites = [ordered]@{ "unit" = "run_unit_tests.ps1"; "move" = "run_move_test.ps1" }
if ($Online) { $suites["net"] = "run_net_test.ps1" }
$results = [ordered]@{}
foreach ($name in $suites.Keys) {
    # the last suite's engine and its shader workers must be gone: the movement test measures speeds,
    # and ran at 83% of them while the unit tests' processes were still exiting (2026-10-09)
    $deadline = (Get-Date).AddMinutes(5)
    while ((Get-Process -Name "UnrealEditor-Cmd", "ShaderCompileWorker" -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 2
    }
    Write-Host "=== $name ==="
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $suites[$name]) | Out-Host
    $results[$name] = $LASTEXITCODE
}
Write-Host ""
Write-Host "Summary:"
$bad = 0
foreach ($name in $results.Keys) {
    $ok = $results[$name] -eq 0
    if (-not $ok) { $bad++ }
    Write-Host ("  {0,-6} {1}" -f $name, $(if ($ok) { "PASS" } else { "FAIL" }))
}
exit $bad
