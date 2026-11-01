# Input-path check for the live test: PrintScreen and Win+Shift+S must work before, during and after
# Velora Mode. Run it from a normal (non-elevated) PowerShell on the interactive desktop, once per
# phase:  tools\input_check.ps1 -Phase before|during|after [-Send]
#
# Without -Send it is read-only: modifier key state, the PrintScreen-key Snipping Tool setting,
# leftover Velora processes. With -Send it also injects PrintScreen and Win+Shift+S (SendInput) and
# reports whether Windows reacted (Snipping overlay process/window appeared, or the clipboard changed
# for the plain-screenshot mode). The overlay is dismissed with Esc. Output is appended to
# docs\evidence\input_check_<date>.txt.
param([Parameter(Mandatory)][ValidateSet('before', 'during', 'after')][string]$Phase, [switch]$Send)

Add-Type @"
using System; using System.Runtime.InteropServices;
public static class K {
  [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vk);
  [DllImport("user32.dll")] public static extern uint GetClipboardSequenceNumber();
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, int size);
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public KI ki; public long pad; }
  [StructLayout(LayoutKind.Sequential)] public struct KI { public ushort vk; public ushort scan; public uint flags; public uint time; public IntPtr extra; }
  public static void Key(ushort vk, bool up) {
    var i = new INPUT[1]; i[0].type = 1; i[0].ki.vk = vk; i[0].ki.flags = up ? 2u : 0u;
    SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@

$out = Join-Path $PSScriptRoot ("..\docs\evidence\input_check_" + (Get-Date -Format 'yyyy-MM-dd') + ".txt")
function Log($m) { $line = "[{0}] {1,-6} {2}" -f (Get-Date -Format 'HH:mm:ss'), $Phase, $m; $line; Add-Content -Path $out -Value $line }

$keys = @{ LWIN = 0x5B; RWIN = 0x5C; SHIFT = 0x10; CTRL = 0x11; ALT = 0x12; SNAPSHOT = 0x2C; RETURN = 0x0D; B = 0x42; S = 0x53; ESC = 0x1B }
$down = $keys.GetEnumerator() | Where-Object { [K]::GetAsyncKeyState($_.Value) -band 0x8000 } | ForEach-Object Name
Log ("keys logically down: " + $(if ($down) { $down -join ',' } else { 'none' }))

$kb = Get-ItemProperty 'HKCU:\Control Panel\Keyboard' -ErrorAction SilentlyContinue
Log ("PrintScreenKeyForSnippingEnabled=" + $(if ($null -ne $kb.PrintScreenKeyForSnippingEnabled) { $kb.PrintScreenKeyForSnippingEnabled } else { '(unset)' }))
Log ("velora processes: " + ((Get-Process velora-* -ErrorAction SilentlyContinue | ForEach-Object { $_.ProcessName + ':' + $_.Id }) -join ','))
Log ("capture processes: " + ((Get-Process ScreenClippingHost, SnippingTool -ErrorAction SilentlyContinue | ForEach-Object { $_.ProcessName + ':' + $_.Id }) -join ','))

function Probe($label, [scriptblock]$press) {
    $seq0 = [K]::GetClipboardSequenceNumber()
    $before = @(Get-Process ScreenClippingHost, SnippingTool -ErrorAction SilentlyContinue | ForEach-Object Id)
    & $press
    $reacted = $null
    for ($i = 0; $i -lt 30 -and -not $reacted; $i++) {
        Start-Sleep -Milliseconds 100
        $now = @(Get-Process ScreenClippingHost, SnippingTool -ErrorAction SilentlyContinue | ForEach-Object Id)
        if (Compare-Object $before $now | Where-Object SideIndicator -eq '=>') { $reacted = 'snipping process started' }
        elseif ([K]::GetClipboardSequenceNumber() -ne $seq0) { $reacted = 'clipboard changed' }
    }
    Start-Sleep -Milliseconds 500
    [K]::Key(0x1B, $false); [K]::Key(0x1B, $true)   # dismiss an overlay if one is up
    Log ("{0}: {1}" -f $label, $(if ($reacted) { "PASS ($reacted)" } else { 'FAIL (no reaction in 3 s)' }))
}

if ($Send) {
    Probe 'PrintScreen' { [K]::Key(0x2C, $false); [K]::Key(0x2C, $true) }
    Start-Sleep -Seconds 1
    Probe 'Win+Shift+S' { [K]::Key(0x5B, $false); [K]::Key(0x10, $false); [K]::Key(0x53, $false); [K]::Key(0x53, $true); [K]::Key(0x10, $true); [K]::Key(0x5B, $true) }
} else {
    Log 'key injection skipped (use -Send)'
}
