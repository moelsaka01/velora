// Global app menus, tiers 1 and 2, against real windows of other processes:
//   * pure helpers (mnemonics, shortcuts, probe gating);
//   * tier 1 against menu_fixture.exe (a classic Win32 HMENU window): bar, drop-downs (with the WM_INITMENUPOPUP replay), submenus,
//     disabled / checked items, WM_COMMAND delivery;
//   * tier 2 (UI Automation) against a Windows Forms MenuStrip and a WPF Menu, both hosted by Windows PowerShell;
//   * optionally real Notepad (VELORA_MENU_TEST_APPS=notepad), informational only.
// Prints "COMPAT ..." lines: they are the source of docs/MENU_COMPAT.md.
#include "core/win.h"

#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include <objbase.h>

#include "menubar/app_menu.h"
#include "test_process.h"

using namespace velora::menubar;

namespace {
int g_failed = 0, g_checks = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        ++g_checks;                                                      \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                  \
        }                                                                \
    } while (0)

using testproc::Child;
using testproc::Spawn;
using testproc::Close;
using testproc::ReadFileText;
using testproc::WaitMarker;

const AppMenuItem* Find(const std::vector<AppMenuItem>& items, const std::wstring& label) {
    for (const AppMenuItem& i : items)
        if (i.label == label) return &i;
    return nullptr;
}

std::wstring Describe(const std::vector<AppMenuItem>& items) {
    std::wstring s;
    for (const AppMenuItem& i : items) s += (s.empty() ? L"" : L" | ") + (i.separator ? std::wstring(L"---") : i.label + (i.submenu ? L" >" : L""));
    return s;
}

void Pure() {
    auto t = SplitMenuText(L"&File");
    CHECK(t.label == L"File" && t.shortcut.empty());
    t = SplitMenuText(L"Save &As...\tCtrl+Shift+S");
    CHECK(t.label == L"Save As..." && t.shortcut == L"Ctrl+Shift+S");
    t = SplitMenuText(L"Fish && Chips");
    CHECK(t.label == L"Fish & Chips");
    t = SplitMenuText(L"Open\t");
    CHECK(t.label == L"Open" && t.shortcut.empty());
    t = SplitMenuText(L"_File", L'_');
    CHECK(t.label == L"File");
    CHECK(SplitMenuText(L"").label.empty());
    CHECK(IsChromiumClass(L"Chrome_WidgetWin_1") && !IsChromiumClass(L"WindowsForms10.Window.8.app.0.141b42a_r6_ad1"));
    CHECK(!MayProbeUia(L"Chrome_WidgetWin_1", false) && MayProbeUia(L"Chrome_WidgetWin_1", true));
    CHECK(MayProbeUia(L"WindowsForms10.Window.8.app", false) && MayProbeUia(L"HwndWrapper[x]", false) && MayProbeUia(L"Qt662QWindowIcon", false));
    CHECK(!MayProbeUia(L"Progman", true) && !MayProbeUia(L"Shell_TrayWnd", true) && !MayProbeUia(L"", true));
}

void Tier1(const std::wstring& fixtureExe, const std::wstring& dir) {
    const std::wstring marker = dir + L"\\t1.marker";
    Child c;
    CHECK(Spawn(L"\"" + fixtureExe + L"\" --marker \"" + marker + L"\"", L"Velora Menu Fixture", 15000, &c));
    if (!c.hwnd) return;

    AppMenuBar bar;
    CHECK(ReadWin32Bar(c.hwnd, &bar) && bar.tier == MenuTier::Win32);
    std::printf("INFO tier1 bar: %ls\n", TitlesToString(bar).c_str());
    CHECK(bar.titles.size() == 4 && bar.titles[0].label == L"File" && bar.titles[1].label == L"Edit" && bar.titles[2].label == L"Help" && bar.titles[3].label == L"Run");
    CHECK(!bar.titles[0].direct && bar.titles[3].direct && bar.titles[3].id == 400);

    std::vector<AppMenuItem> file;
    CHECK(ReadWin32Items(c.hwnd, 0, &file));
    std::printf("INFO tier1 File: %ls\n", Describe(file).c_str());
    const AppMenuItem* nw = Find(file, L"New");
    CHECK(nw && nw->shortcut == L"Ctrl+N" && nw->id == 101 && nw->enabled);
    const AppMenuItem* recent = Find(file, L"Recent");
    CHECK(recent && recent->submenu && recent->children.size() == 2 && recent->children[0].label == L"one.txt" && recent->children[1].id == 112);
    CHECK(file.size() == 7 && file[2].separator && file[4].separator);  // New, Open, ---, Recent, ---, Opened, Exit
    const AppMenuItem* opened = nullptr;
    for (const AppMenuItem& i : file)
        if (i.label.rfind(L"Opened", 0) == 0) opened = &i;
    CHECK(opened && opened->label == L"Opened 1 times");
    std::vector<AppMenuItem> again;
    CHECK(ReadWin32Items(c.hwnd, 0, &again));
    opened = nullptr;
    for (const AppMenuItem& i : again)
        if (i.label.rfind(L"Opened", 0) == 0) opened = &i;
    CHECK(opened && opened->label == L"Opened 2 times");  // WM_INITMENUPOPUP was replayed before each read

    std::vector<AppMenuItem> edit;
    CHECK(ReadWin32Items(c.hwnd, 1, &edit) && edit.size() == 3);
    if (edit.size() == 3) {
        CHECK(edit[0].label == L"Cut" && !edit[0].enabled && edit[0].shortcut == L"Ctrl+X");
        CHECK(edit[1].label == L"Copy" && edit[1].enabled && !edit[1].checked);
        CHECK(edit[2].label == L"Paste" && edit[2].checked);
    }
    std::vector<AppMenuItem> none;
    CHECK(!ReadWin32Items(c.hwnd, 3, &none));  // "Run" is a command, not a drop-down
    CHECK(!ReadWin32Items(c.hwnd, 9, &none));

    // Invoke: WM_COMMAND reaches the other process.
    CHECK(nw && InvokeWin32(c.hwnd, nw->id) && WaitMarker(marker, "cmd 101"));
    CHECK(InvokeWin32(c.hwnd, recent->children[1].id) && WaitMarker(marker, "cmd 112"));
    CHECK(InvokeWin32(c.hwnd, bar.titles[3].id) && WaitMarker(marker, "cmd 400"));
    std::printf("COMPAT | Velora menu fixture (classic Win32 HMENU) | 1 | bar %zu titles, drop-downs, submenu, disabled/checked, shortcuts, INITMENUPOPUP replay, WM_COMMAND delivered | tested\n", bar.titles.size());

    // Gone window: no crash, nothing to read.
    HWND dead = c.hwnd;
    CHECK(InvokeWin32(c.hwnd, 103));  // Exit
    CHECK(WaitForSingleObject(c.pi.hProcess, 5000) == WAIT_OBJECT_0);
    CHECK(!ReadWin32Bar(dead, &bar) && !InvokeWin32(dead, 101));
    Close(&c);
}

// Shared checks for the two UIA fixtures. `mnemonic` titles: the File / Edit menus of the PowerShell scripts below.
void Tier2(const wchar_t* what, const std::wstring& script, const std::wstring& scriptDir, const std::wstring& dir, const wchar_t* titlePart, bool wpf) {
    const std::wstring marker = dir + L"\\" + what + L".marker";
    const std::wstring file = scriptDir + L"\\" + what + L".ps1";
    {
        std::ofstream f(file, std::ios::binary);
        std::string bytes;
        for (wchar_t c : script) bytes += static_cast<char>(c);  // the scripts are ASCII
        f << bytes;
    }
    Child c;
    const bool up = Spawn(L"powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File \"" + file + L"\" -Marker \"" + marker + L"\"", titlePart, 60000, &c);
    CHECK(up);
    if (!up) {
        Close(&c);
        return;
    }
    wchar_t cls[128] = {};
    GetClassNameW(c.hwnd, cls, 128);
    std::printf("INFO %ls: window class %ls\n", what, cls);
    CHECK(GetMenu(c.hwnd) == nullptr);  // nothing for tier 1 here
    AppMenuBar bar;
    CHECK(MayProbeUia(cls, false));
    bool read = ReadUiaBar(c.hwnd, &bar);
    for (int i = 0; i < 10 && !read; ++i) {  // the toolkit may need a moment to publish its tree
        Sleep(500);
        read = ReadUiaBar(c.hwnd, &bar);
    }
    CHECK(read && bar.tier == MenuTier::Uia);
    wchar_t dump[8] = {};
    if (read && GetEnvironmentVariableW(L"VELORA_UIA_DUMP", dump, 8) && !bar.titles.empty())
        std::printf("DUMP %ls title0:\n%ls\n", what, DescribeUiaMenu(c.hwnd, bar.titles[0].index).c_str());
    std::printf("INFO %ls bar: %ls\n", what, TitlesToString(bar).c_str());
    CHECK(bar.titles.size() == 2 && bar.titles[0].label == L"File" && bar.titles[1].label == L"Edit");
    if (bar.titles.size() == 2) {
        std::vector<AppMenuItem> items;
        CHECK(ReadUiaItems(c.hwnd, bar.titles[0].index, {}, &items));
        std::printf("INFO %ls File: %ls\n", what, Describe(items).c_str());
        const AppMenuItem* nw = Find(items, L"New");
        const AppMenuItem* recent = Find(items, L"Recent");
        CHECK(nw && nw->enabled && Find(items, L"Open...") && Find(items, L"Exit"));
        if (nw) std::printf("INFO %ls New shortcut: \"%ls\"\n", what, nw->shortcut.c_str());
        CHECK(recent && recent->submenu);
        bool sep = false;
        for (const AppMenuItem& i : items) sep = sep || i.separator;
        CHECK(sep);

        std::vector<AppMenuItem> edit;
        CHECK(ReadUiaItems(c.hwnd, bar.titles[1].index, {}, &edit));
        std::printf("INFO %ls Edit: %ls\n", what, Describe(edit).c_str());
        const AppMenuItem* cut = Find(edit, L"Cut");
        const AppMenuItem* paste = Find(edit, L"Paste");
        CHECK(cut && !cut->enabled && Find(edit, L"Copy") && Find(edit, L"Copy")->enabled);
        CHECK(paste && paste->checked);

        std::vector<AppMenuItem> sub;
        if (recent) CHECK(ReadUiaItems(c.hwnd, bar.titles[0].index, {recent->uiaIndex}, &sub));
        std::printf("INFO %ls Recent: %ls\n", what, Describe(sub).c_str());
        CHECK(sub.size() == 2 && sub[0].label == L"one.txt" && sub[1].label == L"two.txt");

        // Invoke through the menu: a command, and one inside the submenu.
        CHECK(nw && InvokeUia(c.hwnd, bar.titles[0].index, {nw->uiaIndex}, L"New") && WaitMarker(marker, "click new"));
        if (recent && sub.size() == 2) CHECK(InvokeUia(c.hwnd, bar.titles[0].index, {recent->uiaIndex, sub[1].uiaIndex}, L"two.txt") && WaitMarker(marker, "click r2"));
        // A disabled item is refused, a wrong label is not blindly invoked.
        if (cut) CHECK(!InvokeUia(c.hwnd, bar.titles[1].index, {cut->uiaIndex}, L"Cut"));
        CHECK(!InvokeUia(c.hwnd, bar.titles[1].index, {1}, L"No such item"));
        // Everything was closed again after reading: a second read gives the same answer.
        std::vector<AppMenuItem> again;
        CHECK(ReadUiaItems(c.hwnd, bar.titles[0].index, {}, &again) && again.size() == items.size());
    }
    std::printf("COMPAT | %ls fixture (hosted by Windows PowerShell) | 2 | bar, drop-downs, submenu, disabled/checked, Invoke | tested\n", wpf ? L"WPF" : L"Windows Forms");
    Close(&c);
}

const char* kWinFormsScript = R"PS(param([string]$Marker)
Add-Type -AssemblyName System.Windows.Forms
$form = New-Object System.Windows.Forms.Form
$form.Text = 'Velora WinForms Fixture'
$form.Width = 520; $form.Height = 320
$form.StartPosition = 'Manual'; $form.Location = New-Object System.Drawing.Point(120, 120)
$menu = New-Object System.Windows.Forms.MenuStrip
function New-Item2($text, $tag) {
  $i = New-Object System.Windows.Forms.ToolStripMenuItem($text)
  $i.Tag = $tag
  $i.add_Click({ param($s, $e) Add-Content -Path $Marker -Value ('click ' + $s.Tag) }.GetNewClosure())
  return $i
}
$file = New-Object System.Windows.Forms.ToolStripMenuItem('&File')
$new = New-Item2 '&New' 'new'
$new.ShortcutKeys = [System.Windows.Forms.Keys]::Control -bor [System.Windows.Forms.Keys]::N
$open = New-Item2 '&Open...' 'open'
$recent = New-Object System.Windows.Forms.ToolStripMenuItem('&Recent')
[void]$recent.DropDownItems.Add((New-Item2 'one.txt' 'r1'))
[void]$recent.DropDownItems.Add((New-Item2 'two.txt' 'r2'))
[void]$file.DropDownItems.Add($new)
[void]$file.DropDownItems.Add($open)
[void]$file.DropDownItems.Add((New-Object System.Windows.Forms.ToolStripSeparator))
[void]$file.DropDownItems.Add($recent)
[void]$file.DropDownItems.Add((New-Item2 'E&xit' 'exit'))
$edit = New-Object System.Windows.Forms.ToolStripMenuItem('&Edit')
$cut = New-Item2 'Cu&t' 'cut'; $cut.Enabled = $false
$paste = New-Item2 '&Paste' 'paste'; $paste.Checked = $true
[void]$edit.DropDownItems.Add($cut)
[void]$edit.DropDownItems.Add((New-Item2 '&Copy' 'copy'))
[void]$edit.DropDownItems.Add($paste)
[void]$menu.Items.Add($file)
[void]$menu.Items.Add($edit)
$form.MainMenuStrip = $menu
$form.Controls.Add($menu)
[void][System.Windows.Forms.Application]::Run($form)
)PS";

const char* kWpfScript = R"PS(param([string]$Marker)
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase
$w = New-Object System.Windows.Window
$w.Title = 'Velora WPF Fixture'; $w.Width = 520; $w.Height = 320
$w.WindowStartupLocation = 'Manual'; $w.Left = 140; $w.Top = 140
function New-Mi($header, $tag) {
  $m = New-Object System.Windows.Controls.MenuItem
  $m.Header = $header; $m.Tag = $tag
  $m.Add_Click({ param($s, $e) Add-Content -Path $Marker -Value ('click ' + $s.Tag) }.GetNewClosure())
  return $m
}
$menu = New-Object System.Windows.Controls.Menu
$file = New-Mi '_File' 'file'
$new = New-Mi '_New' 'new'; $new.InputGestureText = 'Ctrl+N'
$recent = New-Mi '_Recent' 'recent'
[void]$recent.Items.Add((New-Mi 'one.txt' 'r1'))
[void]$recent.Items.Add((New-Mi 'two.txt' 'r2'))
[void]$file.Items.Add($new)
[void]$file.Items.Add((New-Mi '_Open...' 'open'))
[void]$file.Items.Add((New-Object System.Windows.Controls.Separator))
[void]$file.Items.Add($recent)
[void]$file.Items.Add((New-Mi 'E_xit' 'exit'))
$edit = New-Mi '_Edit' 'edit'
$cut = New-Mi 'Cu_t' 'cut'; $cut.IsEnabled = $false
$paste = New-Mi '_Paste' 'paste'; $paste.IsCheckable = $true; $paste.IsChecked = $true
[void]$edit.Items.Add($cut)
[void]$edit.Items.Add((New-Mi '_Copy' 'copy'))
[void]$edit.Items.Add($paste)
[void]$menu.Items.Add($file)
[void]$menu.Items.Add($edit)
$dock = New-Object System.Windows.Controls.DockPanel
[System.Windows.Controls.DockPanel]::SetDock($menu, 'Top')
[void]$dock.Children.Add($menu)
$w.Content = $dock
[void]$w.ShowDialog()
)PS";

void Notepad() {
    wchar_t env[64] = {};
    if (!GetEnvironmentVariableW(L"VELORA_MENU_TEST_APPS", env, 64) || std::wstring(env).find(L"notepad") == std::wstring::npos) return;
    Child c;
    if (!Spawn(L"notepad.exe", L"Notepad", 20000, &c)) {
        std::printf("INFO notepad: no window\n");
        Close(&c);
        return;
    }
    wchar_t cls[128] = {};
    GetClassNameW(c.hwnd, cls, 128);
    AppMenuBar bar;
    bool tier1 = ReadWin32Bar(c.hwnd, &bar);
    bool tier2 = false;
    for (int i = 0; i < 6 && !tier1 && !tier2; ++i) {
        tier2 = ReadUiaBar(c.hwnd, &bar);
        if (!tier2) Sleep(500);
    }
    std::printf("INFO notepad: class %ls tier1=%d tier2=%d titles: %ls\n", cls, tier1, tier2, TitlesToString(bar).c_str());
    if (bar.tier == MenuTier::Uia && !bar.titles.empty()) {
        std::vector<AppMenuItem> items;
        const bool ok = ReadUiaItems(c.hwnd, bar.titles[0].index, {}, &items);
        std::printf("INFO notepad: %ls items: %ls\n", bar.titles[0].label.c_str(), ok ? Describe(items).c_str() : L"(none read)");
    }
    std::printf("COMPAT | Notepad (Windows 11) | %d | titles: %ls | informational (no invoke)\n", tier1 ? 1 : (tier2 ? 2 : 0), TitlesToString(bar).c_str());
    Close(&c);
}

}  // namespace

int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // UI Automation clients belong in the multithreaded apartment
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc < 2) {
        std::printf("usage: app_menu_test <menu_fixture.exe>\n");
        return 2;
    }
    const int wlen = MultiByteToWideChar(CP_ACP, 0, argv[1], -1, nullptr, 0);
    std::wstring fixture(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, fixture.data(), wlen);
    fixture.resize(wcslen(fixture.c_str()));
    wchar_t full[MAX_PATH] = {};
    if (GetFullPathNameW(fixture.c_str(), MAX_PATH, full, nullptr)) fixture = full;  // CreateProcess wants a plain Windows path

    wchar_t tmp[MAX_PATH] = {}, longTmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    GetLongPathNameW(tmp, longTmp, MAX_PATH);
    std::wstring dir = std::wstring(longTmp) + L"velora-appmenu-test-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);

    Pure();
    Tier1(fixture, dir);
    const std::wstring wf(kWinFormsScript, kWinFormsScript + strlen(kWinFormsScript)), wp(kWpfScript, kWpfScript + strlen(kWpfScript));
    Tier2(L"winforms", wf, dir, dir, L"Velora WinForms Fixture", false);
    Tier2(L"wpf", wp, dir, dir, L"Velora WPF Fixture", true);
    Notepad();

    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
