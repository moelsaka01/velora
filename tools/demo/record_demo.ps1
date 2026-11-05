<#
.SYNOPSIS
  Records the Velora demo video: starts ffmpeg (gdigrab, no audio), then drives Velora through its own CLI and a few harmless apps.
.DESCRIPTION
  Run it from a non-elevated PowerShell with antivirus real-time scanning off (Velora hides the taskbar and registers appbars) and with
  private windows minimised first (press Win+D): the recording shows the whole screen. Nothing is clicked or typed for you: only
  `velora-shell --open-*` commands, Velora's own apps, and Windows apps that are closed again at the end.
  Output: <OutDir>\velora-demo-raw.mp4 and <OutDir>\segments.json (the time of each scene, used by make_demo.ps1 to blur the Wi-Fi name).
.EXAMPLE
  powershell -File tools\demo\record_demo.ps1 -OutDir $env:TEMP\velora-demo
#>
param(
    [string]$Shell = (Join-Path $PSScriptRoot '..\..\build\Release\velora-shell.exe'),
    [string]$Settings = (Join-Path $PSScriptRoot '..\..\build\Release\velora-settings.exe'),
    [Parameter(Mandatory)][string]$OutDir,
    [int]$Seconds = 66,
    [string]$ProjectsDir = 'C:\Users\Public\Documents\Projects'
)
$ErrorActionPreference = 'Stop'
$Shell = (Resolve-Path $Shell).Path
$Settings = (Resolve-Path $Settings).Path
New-Item -ItemType Directory -Force $OutDir | Out-Null
$raw = Join-Path $OutDir 'velora-demo-raw.mp4'

# Sample files for the Files scene (a neutral path: nothing private in the breadcrumb).
New-Item -ItemType Directory -Force (Join-Path $ProjectsDir 'Notes'), (Join-Path $ProjectsDir 'Photos'), (Join-Path $ProjectsDir 'Reports') | Out-Null
'Velora demo' | Set-Content (Join-Path $ProjectsDir 'Readme.md')
'item,qty' | Set-Content (Join-Path $ProjectsDir 'budget.csv')
('x' * 20000) | Set-Content (Join-Path $ProjectsDir 'archive.zip')

$started = New-Object System.Collections.Generic.List[System.Diagnostics.Process]
$segs = New-Object System.Collections.Generic.List[object]
$sw = [System.Diagnostics.Stopwatch]::StartNew()
function Mark($name) { $segs.Add([pscustomobject]@{ name = $name; t = [math]::Round($sw.Elapsed.TotalSeconds, 2) }) | Out-Null }
function Cmd($arg) { & $Shell $arg | Out-Null }

# ffmpeg stops itself after -t seconds, so the file is always finalised.
$ff = Start-Process ffmpeg -PassThru -WindowStyle Hidden -ArgumentList @('-y', '-f', 'gdigrab', '-framerate', '20', '-draw_mouse', '1', '-i', 'desktop',
    '-t', "$Seconds", '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '20', '-pix_fmt', 'yuv420p', "`"$raw`"")
$sw.Restart()
Start-Sleep -Milliseconds 1500
Mark 'start'

try {
    Start-Process $Shell | Out-Null                       # enter Velora Mode
    Start-Sleep 8; Mark 'desktop'

    Cmd '--open-apps'; Start-Sleep 5; Mark 'apps'; Cmd '--close-apps'; Start-Sleep 1
    Cmd '--open-search'; Start-Sleep 1
    & $Shell --open-search calc | Out-Null; Start-Sleep 4; Mark 'search'; Cmd '--close-search'; Start-Sleep 1
    & $Shell --open-files $ProjectsDir | Out-Null; Start-Sleep 5; Mark 'files'
    Get-Process velora-files -ErrorAction SilentlyContinue | ForEach-Object { $_.CloseMainWindow() | Out-Null }; Start-Sleep 1

    # A few ordinary apps so the Dock, the window theming and the Overview have something to show.
    foreach ($exe in 'C:\Program Files\WordCraft\wordcraft.exe', 'C:\Program Files\DeckCraft\deckcraft.exe', 'C:\Program Files\GridCraft\gridcraft.exe', 'calc.exe') {
        try { $p = Start-Process $exe -PassThru; $started.Add($p) } catch { }
        Start-Sleep -Milliseconds 700
    }
    $started.Add((Start-Process explorer.exe -ArgumentList 'C:\Windows' -PassThru))
    Start-Sleep 6; Mark 'apps-open'

    Cmd '--open-overview'; Start-Sleep 5; Mark 'overview'; Cmd '--close-overview'; Start-Sleep 1
    Cmd '--open-control'; Start-Sleep 6; Mark 'control'; Cmd '--close-control'; Start-Sleep 1
    Start-Process $Settings -ArgumentList '--page', 'theming' | Out-Null; Start-Sleep 6; Mark 'settings'
    Get-Process velora-settings -ErrorAction SilentlyContinue | ForEach-Object { $_.CloseMainWindow() | Out-Null }; Start-Sleep 1
} finally {
    Cmd '--exit'
    Mark 'exit'
    Start-Sleep 2
    foreach ($p in $started) { try { if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null } } catch { } }
    Get-Process calculatorapp, CalculatorApp -ErrorAction SilentlyContinue | ForEach-Object { $_.CloseMainWindow() | Out-Null }
}
Mark 'end'
$ff.WaitForExit(([math]::Max(5, $Seconds - $sw.Elapsed.TotalSeconds + 10)) * 1000) | Out-Null
$segs | ConvertTo-Json | Set-Content (Join-Path $OutDir 'segments.json')
"recorded $raw; scenes: " + (($segs | ForEach-Object { "$($_.name)@$($_.t)s" }) -join ', ')
