<#
.SYNOPSIS
  Records the Velora demo video: starts ffmpeg (gdigrab, no audio), then drives Velora through its own CLI and a few harmless apps.
.DESCRIPTION
  Run it from a non-elevated PowerShell with antivirus real-time scanning off (Velora hides the taskbar and registers appbars). The
  script minimises your windows (like Win+D), hides the desktop icons for the length of the recording (and puts them back), uses
  only apps without personal content (DeckCraft, GridCraft, Calculator, Paint) and a sample folder under C:\Users\Public.
  Nothing is clicked or typed for you: only `velora-shell --open-*` commands, Velora's own apps, and apps that are closed again.
  Output: <OutDir>\velora-demo-raw.mp4 and <OutDir>\segments.json (when each scene ended, used by make_demo.ps1 to blur the Wi-Fi
  name and the file hits of the Search scene).
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

Add-Type @"
using System; using System.Runtime.InteropServices;
public class DesktopIcons {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
  [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  // "Show desktop icons" toggle: WM_COMMAND 0x7402 to the desktop's SHELLDLL_DefView.
  public static bool Toggle() {
    IntPtr view = IntPtr.Zero;
    IntPtr prog = FindWindowEx(IntPtr.Zero, IntPtr.Zero, "Progman", null);
    if (prog != IntPtr.Zero) view = FindWindowEx(prog, IntPtr.Zero, "SHELLDLL_DefView", null);
    if (view == IntPtr.Zero) EnumWindows((h, l) => { IntPtr v = FindWindowEx(h, IntPtr.Zero, "SHELLDLL_DefView", null); if (v != IntPtr.Zero) { view = v; return false; } return true; }, IntPtr.Zero);
    if (view == IntPtr.Zero) return false;
    SendMessage(view, 0x111, (IntPtr)0x7402, IntPtr.Zero);
    return true;
  }
}
"@
function Icons-Hidden { try { return ((Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' -ErrorAction Stop).HideIcons -eq 1) } catch { return $false } }

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

$iconsWereShown = -not (Icons-Hidden)
(New-Object -ComObject Shell.Application).MinimizeAll()      # like Win+D: nothing private stays on screen
if ($iconsWereShown) { [DesktopIcons]::Toggle() | Out-Null }  # hide desktop icons for the recording
Start-Sleep 2

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
    & $Shell --open-search gridcraft | Out-Null; Start-Sleep 4; Mark 'search'; Cmd '--close-search'; Start-Sleep 1
    & $Shell --open-files $ProjectsDir | Out-Null; Start-Sleep 5; Mark 'files'
    Get-Process velora-files -ErrorAction SilentlyContinue | ForEach-Object { $_.CloseMainWindow() | Out-Null }; Start-Sleep 1

    # A few ordinary apps so the Dock, the window theming and the Overview have something to show.
    foreach ($exe in 'C:\Program Files\DeckCraft\deckcraft.exe', 'C:\Program Files\GridCraft\gridcraft.exe', 'calc.exe', 'mspaint.exe') {
        try { $p = Start-Process $exe -PassThru; $started.Add($p) } catch { }
        Start-Sleep -Milliseconds 900
    }
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
    Get-Process CalculatorApp, mspaint, PaintApp -ErrorAction SilentlyContinue | ForEach-Object { $_.CloseMainWindow() | Out-Null }
    if ($iconsWereShown -and (Icons-Hidden)) { [DesktopIcons]::Toggle() | Out-Null }  # desktop icons back
}
Mark 'end'
$ff.WaitForExit(([math]::Max(5, $Seconds - $sw.Elapsed.TotalSeconds + 10)) * 1000) | Out-Null
$segs | ConvertTo-Json | Set-Content (Join-Path $OutDir 'segments.json')
"recorded $raw; scenes: " + (($segs | ForEach-Object { "$($_.name)@$($_.t)s" }) -join ', ')
