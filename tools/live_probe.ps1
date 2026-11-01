# Read-only probe of the live desktop: monitors, work areas, taskbar visibility, auto-hide, Velora files/processes.
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public static class P {
 [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string t);
 [DllImport("user32.dll")] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("shell32.dll")] public static extern UIntPtr SHAppBarMessage(uint m, ref APPBARDATA d);
 [StructLayout(LayoutKind.Sequential)] public struct APPBARDATA { public uint cb; public IntPtr hWnd; public uint cb2; public uint edge; public int l,t,r,b; public IntPtr lp; }
 public static string Taskbars() {
  var sb = new StringBuilder(); IntPtr h = FindWindow("Shell_TrayWnd", null);
  sb.Append("primary=" + (h==IntPtr.Zero?"none":(IsWindowVisible(h)?"visible":"hidden")));
  IntPtr s = IntPtr.Zero; int n=0;
  while ((s = FindWindowEx(IntPtr.Zero, s, "Shell_SecondaryTrayWnd", null)) != IntPtr.Zero) { n++; sb.Append(" secondary" + n + "=" + (IsWindowVisible(s)?"visible":"hidden")); }
  return sb.ToString(); }
 public static string AutoHide() { var d = new APPBARDATA(); d.cb=(uint)Marshal.SizeOf(d); uint st=(uint)SHAppBarMessage(4, ref d); return (st&1)!=0?"autohide=on":"autohide=off"; }
}
"@
"time=" + (Get-Date -Format o)
[P]::Taskbars(); [P]::AutoHide()
foreach ($s in [System.Windows.Forms.Screen]::AllScreens) { "monitor " + $s.DeviceName + " primary=" + $s.Primary + " bounds=" + $s.Bounds + " workarea=" + $s.WorkingArea }
"processes=" + ((Get-Process velora-* -ErrorAction SilentlyContinue | % { $_.ProcessName + ":" + $_.Id }) -join ",")
$d = Join-Path $env:LOCALAPPDATA 'Velora'
"velora_dir=" + $(if (Test-Path $d) { (Get-ChildItem $d -Force | % Name) -join ',' } else { 'absent' })
"explorer_pid=" + ((Get-Process explorer -ErrorAction SilentlyContinue).Id -join ',')
