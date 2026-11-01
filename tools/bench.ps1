<#
.SYNOPSIS
  Idle resource benchmark for Velora Mode (CLAUDE.md sec. 6).

.DESCRIPTION
  For each run: sample Windows Mode (no Velora) for -SampleSec, start velora-shell.exe, wait -WaitSec,
  sample Velora Mode for -SampleSec, then exit with `velora-shell --exit` and time it.
  Records, once per second: system committed bytes, per-process working set / private bytes / CPU time of
  velora-shell and velora-watchdog, and (best effort) GPU process memory.
  Prints median and range over the runs and writes the raw samples to bench/<stamp>_run<N>.csv plus a
  summary JSON. Defaults follow the spec (wait 2 min, sample 60 s, 3 runs).

  Known deviation: the script does not reboot the machine ("clean boot"); run it from a quiet desktop and
  read the numbers as measured on that desktop, not as a lab figure. Close the on-demand apps first.

.EXAMPLE
  powershell -File tools\bench.ps1 -Shell build\Release\velora-shell.exe
#>
param(
    [string]$Shell = (Join-Path $PSScriptRoot '..\build\Release\velora-shell.exe'),
    [int]$Runs = 3,
    [int]$WaitSec = 120,
    [int]$SampleSec = 60,
    [string]$OutDir = (Join-Path $PSScriptRoot '..\bench')
)

$ErrorActionPreference = 'Stop'
$Shell = (Resolve-Path $Shell).Path
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'

function Get-Committed { (Get-Counter '\Memory\Committed Bytes' -ErrorAction Stop).CounterSamples[0].CookedValue }

function Get-GpuMemory([int[]]$procIds) {
    $total = 0.0
    try {
        $samples = (Get-Counter '\GPU Process Memory(*)\Local Usage' -ErrorAction Stop).CounterSamples
        foreach ($s in $samples) { foreach ($p in $procIds) { if ($s.InstanceName -like "pid_${p}_*") { $total += $s.CookedValue } } }
    } catch { return $null }
    return $total
}

function Sample-Phase([string]$phase, [int]$seconds, [int[]]$procIds, $rows) {
    $prev = @{}
    foreach ($p in $procIds) { $pr = Get-Process -Id $p -ErrorAction SilentlyContinue; if ($pr) { $prev[$p] = $pr.TotalProcessorTime.TotalSeconds } }
    for ($i = 0; $i -lt $seconds; $i++) {
        $t0 = Get-Date
        $ws = 0.0; $priv = 0.0; $cpu = 0.0
        foreach ($p in $procIds) {
            $pr = Get-Process -Id $p -ErrorAction SilentlyContinue
            if (-not $pr) { continue }
            $ws += $pr.WorkingSet64; $priv += $pr.PrivateMemorySize64
            $now = $pr.TotalProcessorTime.TotalSeconds
            $cpu += ($now - $prev[$p]); $prev[$p] = $now
        }
        $gpu = if ($procIds.Count) { Get-GpuMemory $procIds } else { $null }
        $rows.Add([pscustomobject]@{
            phase = $phase; second = $i; committedBytes = [int64](Get-Committed)
            velWorkingSetBytes = [int64]$ws; velPrivateBytes = [int64]$priv
            velCpuPercent = [math]::Round(100.0 * $cpu / [Environment]::ProcessorCount, 3)  # whole machine = 100%
            gpuLocalBytes = $(if ($null -ne $gpu) { [int64]$gpu } else { $null })
        }) | Out-Null
        $sleep = 1000 - ((Get-Date) - $t0).TotalMilliseconds
        if ($sleep -gt 0) { Start-Sleep -Milliseconds $sleep }
    }
}

function Median($v) { $s = @($v | Sort-Object); if (-not $s.Count) { return $null }; $s[[int][math]::Floor(($s.Count - 1) / 2)] }

if (Get-Process velora-shell -ErrorAction SilentlyContinue) { throw 'velora-shell is already running; exit it first (velora-shell --exit).' }

$summary = @()
for ($run = 1; $run -le $Runs; $run++) {
    Write-Host "== run $run/$Runs"
    $rows = New-Object System.Collections.ArrayList
    Sample-Phase 'windows' $SampleSec @() $rows

    $sw = [Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process -FilePath $Shell -PassThru
    $enterMs = $null
    $tray = Add-Type -PassThru -Name W$run -Namespace B -MemberDefinition '[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern System.IntPtr FindWindow(string c, string t); [System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool IsWindowVisible(System.IntPtr h);'
    while ($sw.ElapsedMilliseconds -lt 15000) {
        $h = $tray::FindWindow('Shell_TrayWnd', $null)
        if ($h -ne [IntPtr]::Zero -and -not $tray::IsWindowVisible($h)) { $enterMs = $sw.ElapsedMilliseconds; break }
        Start-Sleep -Milliseconds 10
    }
    Start-Sleep -Seconds $WaitSec
    $ids = @($proc.Id) + @((Get-Process velora-watchdog -ErrorAction SilentlyContinue).Id)
    Sample-Phase 'velora' $SampleSec $ids $rows

    $sw.Restart()
    # -Wait: velora-shell is a GUI-subsystem exe, so a plain call returns at once. The helper returns when the shell is gone.
    Start-Process -FilePath $Shell -ArgumentList "--exit" -Wait
    $exitMs = $sw.ElapsedMilliseconds
    $csv = Join-Path $OutDir "${stamp}_run$run.csv"
    $rows | Export-Csv -NoTypeInformation -Path $csv

    $w = $rows | Where-Object phase -eq 'windows'
    $v = $rows | Where-Object phase -eq 'velora'
    $baseline = Median ($w.committedBytes)
    $withVel = Median ($v.committedBytes)
    $summary += [pscustomobject]@{
        run = $run; enterMs = $enterMs; exitMs = $exitMs
        committedDeltaMB = [math]::Round(($withVel - $baseline) / 1MB, 1)
        shellPlusWatchdogPrivateMB = [math]::Round((Median ($v.velPrivateBytes)) / 1MB, 1)
        shellPlusWatchdogWorkingSetMB = [math]::Round((Median ($v.velWorkingSetBytes)) / 1MB, 1)
        idleCpuPercent = [math]::Round((Median ($v.velCpuPercent)), 3)
        gpuLocalMB = $(if ($v[0].gpuLocalBytes -ne $null) { [math]::Round((Median ($v.gpuLocalBytes)) / 1MB, 1) } else { $null })
        csv = $csv
    }
    Start-Sleep -Seconds 3
}

$summary | Format-Table -AutoSize
foreach ($m in 'enterMs', 'exitMs', 'committedDeltaMB', 'shellPlusWatchdogPrivateMB', 'shellPlusWatchdogWorkingSetMB', 'idleCpuPercent', 'gpuLocalMB') {
    $vals = @($summary | ForEach-Object { $_.$m } | Where-Object { $null -ne $_ })
    if ($vals.Count) { '{0,-32} median {1}  range {2} .. {3}' -f $m, (Median $vals), ($vals | Measure-Object -Minimum).Minimum, ($vals | Measure-Object -Maximum).Maximum }
}
$summary | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $OutDir "${stamp}_summary.json")
