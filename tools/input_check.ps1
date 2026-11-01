# Input-path check for the live test: PrintScreen and Win+Shift+S must work before, during and after
# Velora Mode. READ-ONLY: this script never injects keys. Run it from a normal (non-elevated)
# PowerShell on the interactive desktop, once per phase:
#
#   tools\input_check.ps1 -Phase before|during|after [-Watch]
#
# Without -Watch it records state only: modifier keys logically down, the PrintScreen-key Snipping Tool
# setting, leftover Velora processes, capture processes. With -Watch it then asks YOU to press
# PrintScreen and, afterwards, Win+Shift+S (15 s each) and reports whether Windows reacted (a Snipping
# process started, or the clipboard changed for the plain-screenshot mode). Press Esc to dismiss the
# overlay. Output is appended to docs\evidence\input_check_<date>.txt.
param([Parameter(Mandatory)][ValidateSet('before', 'during', 'after')][string]$Phase, [switch]$Watch)

Add-Type @"
using System; using System.Runtime.InteropServices;
public static class K {
  [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vk);
  [DllImport("user32.dll")] public static extern uint GetClipboardSequenceNumber();
}
"@

$out = Join-Path $PSScriptRoot ("..\docs\evidence\input_check_" + (Get-Date -Format 'yyyy-MM-dd') + ".txt")
function Log($m) { $line = "[{0}] {1,-6} {2}" -f (Get-Date -Format 'HH:mm:ss'), $Phase, $m; $line; Add-Content -Path $out -Value $line }

$keys = @{ LWIN = 0x5B; RWIN = 0x5C; SHIFT = 0x10; CTRL = 0x11; ALT = 0x12; SNAPSHOT = 0x2C; RETURN = 0x0D; B = 0x42; S = 0x53 }
$down = $keys.GetEnumerator() | Where-Object { [K]::GetAsyncKeyState($_.Value) -band 0x8000 } | ForEach-Object Name
Log ("keys logically down: " + $(if ($down) { $down -join ',' } else { 'none' }))

$kb = Get-ItemProperty 'HKCU:\Control Panel\Keyboard' -ErrorAction SilentlyContinue
Log ("PrintScreenKeyForSnippingEnabled=" + $(if ($null -ne $kb.PrintScreenKeyForSnippingEnabled) { $kb.PrintScreenKeyForSnippingEnabled } else { '(unset)' }))
Log ("velora processes: " + ((Get-Process velora-* -ErrorAction SilentlyContinue | ForEach-Object { $_.ProcessName + ':' + $_.Id }) -join ','))
Log ("capture processes: " + ((Get-Process ScreenClippingHost, SnippingTool -ErrorAction SilentlyContinue | ForEach-Object { $_.ProcessName + ':' + $_.Id }) -join ','))

function Observe($label, $seconds) {
    $seq0 = [K]::GetClipboardSequenceNumber()
    $before = @(Get-Process ScreenClippingHost, SnippingTool -ErrorAction SilentlyContinue | ForEach-Object Id)
    Write-Host ">>> Now press $label (you have $seconds s)"
    $reacted = $null
    for ($i = 0; $i -lt ($seconds * 10) -and -not $reacted; $i++) {
        Start-Sleep -Milliseconds 100
        $now = @(Get-Process ScreenClippingHost, SnippingTool -ErrorAction SilentlyContinue | ForEach-Object Id)
        if (Compare-Object $before $now | Where-Object SideIndicator -eq '=>') { $reacted = 'snipping process started' }
        elseif ([K]::GetClipboardSequenceNumber() -ne $seq0) { $reacted = 'clipboard changed' }
    }
    Log ("{0}: {1}" -f $label, $(if ($reacted) { "PASS ($reacted)" } else { "FAIL (no reaction in $seconds s)" }))
    if ($reacted) { Write-Host "    (press Esc to dismiss the overlay, then wait)"; Start-Sleep -Seconds 4 }
}

if ($Watch) {
    Observe 'PrintScreen' 15
    Observe 'Win+Shift+S' 15
} else {
    Log 'key observation skipped (use -Watch and press the keys yourself)'
}
