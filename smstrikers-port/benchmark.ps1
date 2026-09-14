param([int]$Seconds = 60)
Set-Location $PSScriptRoot

$log = Join-Path $PSScriptRoot 'benchmark.log'
$env:STRIKERS_BENCHMARK         = '1'
$env:STRIKERS_BENCHMARK_SECONDS = "$Seconds"
$env:STRIKERS_LOG               = $log
$env:STRIKERS_BENCH_RECORD      = Join-Path $PSScriptRoot 'benchmark-frames.csv'

$p = Start-Process -FilePath (Join-Path $PSScriptRoot 'strikers.exe') -Wait -PassThru

$found = $false
foreach ($line in Get-Content $log) {
    if ($line -like '=== strikers benchmark*') { $found = $true }
    if ($found) {
        $line
        if ($line -like '====*') { break }
    }
}

if ($p.ExitCode -ne 0) { Write-Error "strikers exited $($p.ExitCode), see benchmark.log"; exit $p.ExitCode }
if (-not $found)       { Write-Error 'no benchmark summary, see benchmark.log'; exit 1 }
