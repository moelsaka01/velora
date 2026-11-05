#include "menubar/app_menu.h"

#include <objbase.h>
#include <oleauto.h>
#include <UIAutomationClient.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>

#include "core/log.h"

#pragma comment(lib, "oleaut32.lib")

namespace velora::menubar {

using Microsoft::WRL::ComPtr;

// ======================= pure helpers =======================
MenuText SplitMenuText(const std::wstring& raw, wchar_t mnemonic) {
    MenuText out;
    const size_t tab = raw.find(L'\t');
    std::wstring head = raw.substr(0, tab);
    if (tab != std::wstring::npos) out.shortcut = raw.substr(tab + 1);
    for (size_t i = 0; i < head.size(); ++i) {
        if (head[i] == mnemonic) {
            if (i + 1 < head.size() && head[i + 1] == mnemonic) {  // doubled = a literal marker
                out.label += mnemonic;
                ++i;
            }
            continue;  // a single marker only underlines the next letter
        }
        out.label += head[i];
    }
    // Trailing blanks (Win32 menus pad before the tab).
    while (!out.label.empty() && out.label.back() == L' ') out.label.pop_back();
    while (!out.shortcut.empty() && out.shortcut.back() == L' ') out.shortcut.pop_back();
    return out;
}

bool IsChromiumClass(const std::wstring& cls) {
    return cls.rfind(L"Chrome_WidgetWin_", 0) == 0 || cls.rfind(L"Chrome_RenderWidgetHostHWND", 0) == 0 || cls == L"MozillaWindowClass";
}

bool MayProbeUia(const std::wstring& windowClass, bool probeChromium) {
    if (windowClass.empty()) return false;
    for (const wchar_t* shell : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"#32769", L"TaskManagerWindow"})
        if (windowClass == shell) return false;
    if (IsChromiumClass(windowClass) && !probeChromium) return false;
    return true;
}

std::wstring TitlesToString(const AppMenuBar& bar) {
    std::wstring s;
    for (const AppMenuTitle& t : bar.titles) s += (s.empty() ? L"" : L" ") + t.label;
    return s;
}

// ======================= tier 1: classic HMENU =======================
namespace {

std::wstring MenuString(HMENU m, UINT pos) {
    wchar_t buf[512] = {};
    const int n = GetMenuStringW(m, pos, buf, static_cast<int>(std::size(buf)), MF_BYPOSITION);
    return n > 0 ? std::wstring(buf, n) : std::wstring{};
}

// One item at `pos`; false when it has no readable text and is not a separator (owner-drawn, bitmaps).
bool ReadWin32Item(HMENU m, UINT pos, AppMenuItem* out, bool* hasSub, HMENU* sub) {
    MENUITEMINFOW mi{sizeof mi};
    mi.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_SUBMENU;
    if (!GetMenuItemInfoW(m, pos, TRUE, &mi)) return false;
    *hasSub = mi.hSubMenu != nullptr;
    *sub = mi.hSubMenu;
    AppMenuItem item;
    if (mi.fType & MFT_SEPARATOR) {
        item.separator = true;
        *out = item;
        return true;
    }
    if (mi.fType & (MFT_OWNERDRAW | MFT_BITMAP)) return false;  // dwTypeData is not text for these
    const MenuText t = SplitMenuText(MenuString(m, pos));
    if (t.label.empty()) return false;
    item.label = t.label;
    item.shortcut = t.shortcut;
    item.enabled = !(mi.fState & (MFS_GRAYED | MFS_DISABLED));
    item.checked = (mi.fState & MFS_CHECKED) != 0;
    item.id = mi.wID;
    item.submenu = mi.hSubMenu != nullptr;
    *out = item;
    return true;
}

void Initialise(HWND hwnd, HMENU sub, UINT index, unsigned timeoutMs) {
    // What the window's own menu bar sends just before it shows a drop-down: the app enables / checks / fills its items.
    DWORD_PTR result = 0;
    SendMessageTimeoutW(hwnd, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(sub), MAKELPARAM(index, 0), SMTO_ABORTIFHUNG | SMTO_NORMAL, timeoutMs, &result);
}

void ReadSubmenu(HWND hwnd, HMENU menu, int depth, unsigned timeoutMs, std::vector<AppMenuItem>* out) {
    const int n = std::min(GetMenuItemCount(menu), 200);
    for (int i = 0; i < n; ++i) {
        AppMenuItem item;
        bool hasSub = false;
        HMENU sub = nullptr;
        if (!ReadWin32Item(menu, static_cast<UINT>(i), &item, &hasSub, &sub)) continue;
        if (hasSub && depth > 1) {
            Initialise(hwnd, sub, static_cast<UINT>(i), timeoutMs);
            ReadSubmenu(hwnd, sub, depth - 1, timeoutMs, &item.children);
        }
        out->push_back(std::move(item));
    }
    // Tidy: no leading / trailing / doubled separators.
    auto& v = *out;
    while (!v.empty() && v.front().separator) v.erase(v.begin());
    while (!v.empty() && v.back().separator) v.pop_back();
    for (size_t i = 1; i < v.size();) {
        if (v[i].separator && v[i - 1].separator) v.erase(v.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
    }
}

}  // namespace

bool ReadWin32Bar(HWND hwnd, AppMenuBar* out) {
    out->tier = MenuTier::None;
    out->hwnd = hwnd;
    out->titles.clear();
    if (!IsWindow(hwnd)) return false;
    HMENU bar = GetMenu(hwnd);
    if (!bar) return false;
    const int n = std::min(GetMenuItemCount(bar), 40);
    if (n <= 0) return false;
    for (int i = 0; i < n; ++i) {
        AppMenuItem item;
        bool hasSub = false;
        HMENU sub = nullptr;
        if (!ReadWin32Item(bar, static_cast<UINT>(i), &item, &hasSub, &sub) || item.separator) continue;
        AppMenuTitle t;
        t.label = item.label;
        t.enabled = item.enabled;
        t.direct = !hasSub;
        t.id = item.id;
        t.index = static_cast<int>(out->titles.size());  // ReadWin32Items counts the readable titles the same way
        out->titles.push_back(std::move(t));
    }
    if (out->titles.empty()) return false;
    out->tier = MenuTier::Win32;
    return true;
}

bool ReadWin32Items(HWND hwnd, int titleIndex, std::vector<AppMenuItem>* out, int maxDepth, unsigned timeoutMs) {
    out->clear();
    if (!IsWindow(hwnd)) return false;
    HMENU bar = GetMenu(hwnd);
    if (!bar) return false;
    // Titles were listed without the unreadable ones; walk the bar the same way to find the real position.
    int seen = -1;
    const int n = std::min(GetMenuItemCount(bar), 40);
    for (int pos = 0; pos < n; ++pos) {
        AppMenuItem item;
        bool hasSub = false;
        HMENU sub = nullptr;
        if (!ReadWin32Item(bar, static_cast<UINT>(pos), &item, &hasSub, &sub) || item.separator) continue;
        if (++seen != titleIndex) continue;
        if (!hasSub) return false;
        Initialise(hwnd, sub, static_cast<UINT>(pos), timeoutMs);
        ReadSubmenu(hwnd, sub, std::max(1, maxDepth), timeoutMs, out);
        return !out->empty();
    }
    return false;
}

bool InvokeWin32(HWND hwnd, unsigned id) {
    if (!IsWindow(hwnd)) return false;
    return PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(static_cast<WORD>(id), 0), 0) != FALSE;
}

// ======================= tier 2: UI Automation =======================
namespace {

struct Uia {
    ComPtr<IUIAutomation> a;
    ComPtr<IUIAutomationCondition> menuBar, menuItem, menuOrSeparator, menuControl;
    ComPtr<IUIAutomationTreeWalker> walker;
    bool ok = false;
};

Uia& Client() {
    thread_local Uia u;
    if (u.ok) return u;
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&u.a)))) return u;
    ComPtr<IUIAutomation6> a6;
    if (SUCCEEDED(u.a.As(&a6))) {  // a hung app must not hold the worker for 20 s
        a6->put_ConnectionTimeout(1500);
        a6->put_TransactionTimeout(3000);
    }
    auto type = [&](long id, ComPtr<IUIAutomationCondition>* out) {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = id;
        return SUCCEEDED(u.a->CreatePropertyCondition(UIA_ControlTypePropertyId, v, out->GetAddressOf()));
    };
    ComPtr<IUIAutomationCondition> sep;
    if (!type(UIA_MenuBarControlTypeId, &u.menuBar) || !type(UIA_MenuItemControlTypeId, &u.menuItem) || !type(UIA_SeparatorControlTypeId, &sep) ||
        !type(UIA_MenuControlTypeId, &u.menuControl) || FAILED(u.a->CreateOrCondition(u.menuItem.Get(), sep.Get(), &u.menuOrSeparator)))
        return u;
    if (FAILED(u.a->get_ControlViewWalker(&u.walker))) return u;
    u.ok = true;
    return u;
}

std::wstring NameOf(IUIAutomationElement* e) {
    BSTR b = nullptr;
    std::wstring s;
    if (SUCCEEDED(e->get_CurrentName(&b)) && b) s = b;
    SysFreeString(b);
    return s;
}

long ControlTypeOf(IUIAutomationElement* e) {
    CONTROLTYPEID id = 0;
    e->get_CurrentControlType(&id);
    return id;
}

bool EnabledOf(IUIAutomationElement* e) {
    BOOL b = TRUE;
    e->get_CurrentIsEnabled(&b);
    return b != FALSE;
}

ComPtr<IUIAutomationElement> AtIndex(IUIAutomationElementArray* arr, int i) {
    ComPtr<IUIAutomationElement> e;
    if (arr) arr->GetElement(i, &e);
    return e;
}

int Length(IUIAutomationElementArray* arr) {
    int n = 0;
    if (arr) arr->get_Length(&n);
    return n;
}

// Every window has a "System" menu in its title bar; it is a menu bar too, but not the app's.
bool InTitleBar(Uia& u, IUIAutomationElement* e) {
    ComPtr<IUIAutomationElement> parent;
    return SUCCEEDED(u.walker->GetParentElement(e, &parent)) && parent && ControlTypeOf(parent.Get()) == UIA_TitleBarControlTypeId;
}

// The menu bar of a window: the first MenuBar control that is not the system menu (WinForms, Qt, XAML), else a Menu control that sits at the
// top of the window and is not a popup (WPF reports its Menu control as "menu", not "menu bar").
ComPtr<IUIAutomationElement> FindBar(Uia& u, HWND hwnd) {
    ComPtr<IUIAutomationElement> root;
    if (FAILED(u.a->ElementFromHandle(hwnd, &root)) || !root) return nullptr;
    RECT win{};
    GetWindowRect(hwnd, &win);
    for (const auto& [condition, needsPlacement] : {std::pair{u.menuBar.Get(), false}, std::pair{u.menuControl.Get(), true}}) {
        ComPtr<IUIAutomationElementArray> candidates;
        if (FAILED(root->FindAll(TreeScope_Descendants, condition, &candidates))) continue;
        for (int i = 0; i < Length(candidates.Get()); ++i) {
            ComPtr<IUIAutomationElement> m = AtIndex(candidates.Get(), i);
            if (!m || InTitleBar(u, m.Get())) continue;
            if (needsPlacement) {
                RECT r{};
                BOOL off = FALSE;
                if (FAILED(m->get_CurrentBoundingRectangle(&r)) || FAILED(m->get_CurrentIsOffscreen(&off)) || off) continue;
                if (r.top < win.top || r.top - win.top > 160 || r.right - r.left < 40) continue;
            }
            ComPtr<IUIAutomationElementArray> kids;
            if (SUCCEEDED(m->FindAll(TreeScope_Children, u.menuItem.Get(), &kids)) && Length(kids.Get()) > 0) return m;
        }
    }
    return nullptr;
}

ComPtr<IUIAutomationElement> TitleAt(Uia& u, HWND hwnd, int index) {
    ComPtr<IUIAutomationElement> bar = FindBar(u, hwnd);
    if (!bar) return nullptr;
    ComPtr<IUIAutomationElementArray> kids;
    if (FAILED(bar->FindAll(TreeScope_Children, u.menuItem.Get(), &kids))) return nullptr;
    return AtIndex(kids.Get(), index);
}

bool ExpandCollapse(IUIAutomationElement* e, bool expand) {
    ComPtr<IUIAutomationExpandCollapsePattern> p;
    if (FAILED(e->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&p))) || !p) return false;
    return SUCCEEDED(expand ? p->Expand() : p->Collapse());
}

// A menu item opens something: its expand state says so (WPF, XAML, Qt), or - Windows Forms has no such pattern - it has menu items below it.
bool IsSubmenu(Uia& u, IUIAutomationElement* e) {
    ComPtr<IUIAutomationExpandCollapsePattern> p;
    if (SUCCEEDED(e->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&p))) && p) {
        ExpandCollapseState s = ExpandCollapseState_LeafNode;
        p->get_CurrentExpandCollapseState(&s);
        return s != ExpandCollapseState_LeafNode;
    }
    ComPtr<IUIAutomationElement> child;
    return SUCCEEDED(e->FindFirst(TreeScope_Children, u.menuItem.Get(), &child)) && child;
}

bool SameElement(Uia& u, IUIAutomationElement* a, IUIAutomationElement* b) {
    BOOL same = FALSE;
    u.a->CompareElements(a, b, &same);
    return same != FALSE;
}

// The menu items that belong to an expanded element: its own children (WPF, WinForms), else a popup menu of the same process that was
// not there before (`known`): Qt, XAML, Chromium open their drop-downs as separate windows.
ComPtr<IUIAutomationElementArray> FindMenuChildren(Uia& u, IUIAutomationElement* owner, DWORD pid, HWND mainWindow,
                                                  const std::vector<ComPtr<IUIAutomationElement>>& known) {
    for (int attempt = 0; attempt < 12; ++attempt) {  // the drop-down appears a moment after Expand()
        Sleep(attempt == 0 ? 40 : 60);
        ComPtr<IUIAutomationElementArray> own;
        if (SUCCEEDED(owner->FindAll(TreeScope_Children, u.menuOrSeparator.Get(), &own)) && Length(own.Get()) > 0) return own;
        if (SUCCEEDED(owner->FindAll(TreeScope_Descendants, u.menuOrSeparator.Get(), &own)) && Length(own.Get()) > 0) return own;
        // A popup window of the same process.
        ComPtr<IUIAutomationElement> desktop;
        if (FAILED(u.a->GetRootElement(&desktop)) || !desktop) continue;
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = static_cast<LONG>(pid);
        ComPtr<IUIAutomationCondition> samePid;
        if (FAILED(u.a->CreatePropertyCondition(UIA_ProcessIdPropertyId, v, &samePid))) continue;
        ComPtr<IUIAutomationElementArray> windows;
        if (FAILED(desktop->FindAll(TreeScope_Children, samePid.Get(), &windows))) continue;
        for (int i = 0; i < Length(windows.Get()); ++i) {
            ComPtr<IUIAutomationElement> w = AtIndex(windows.Get(), i);
            UIA_HWND nh = nullptr;
            w->get_CurrentNativeWindowHandle(&nh);
            if (nh == reinterpret_cast<UIA_HWND>(mainWindow)) continue;
            ComPtr<IUIAutomationElement> menu;
            if (ControlTypeOf(w.Get()) == UIA_MenuControlTypeId) menu = w;
            else w->FindFirst(TreeScope_Descendants, u.menuControl.Get(), &menu);
            if (!menu) continue;
            bool isKnown = false;
            for (const auto& k : known)
                if (SameElement(u, k.Get(), menu.Get())) isKnown = true;
            if (isKnown) continue;
            ComPtr<IUIAutomationElementArray> items;
            if (SUCCEEDED(menu->FindAll(TreeScope_Children, u.menuOrSeparator.Get(), &items)) && Length(items.Get()) > 0) return items;
        }
    }
    return nullptr;
}

std::vector<ComPtr<IUIAutomationElement>> KnownPopups(Uia& u, DWORD pid, HWND mainWindow) {
    std::vector<ComPtr<IUIAutomationElement>> out;
    ComPtr<IUIAutomationElement> desktop;
    if (FAILED(u.a->GetRootElement(&desktop)) || !desktop) return out;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = static_cast<LONG>(pid);
    ComPtr<IUIAutomationCondition> samePid;
    ComPtr<IUIAutomationElementArray> windows;
    if (FAILED(u.a->CreatePropertyCondition(UIA_ProcessIdPropertyId, v, &samePid)) || FAILED(desktop->FindAll(TreeScope_Children, samePid.Get(), &windows))) return out;
    for (int i = 0; i < Length(windows.Get()); ++i) {
        ComPtr<IUIAutomationElement> w = AtIndex(windows.Get(), i), menu;
        UIA_HWND nh = nullptr;
        w->get_CurrentNativeWindowHandle(&nh);
        if (nh == reinterpret_cast<UIA_HWND>(mainWindow)) continue;
        if (ControlTypeOf(w.Get()) == UIA_MenuControlTypeId) menu = w;
        else w->FindFirst(TreeScope_Descendants, u.menuControl.Get(), &menu);
        if (menu) out.push_back(menu);
    }
    return out;
}

AppMenuItem ItemFrom(Uia& u, IUIAutomationElement* e) {
    AppMenuItem item;
    if (ControlTypeOf(e) == UIA_SeparatorControlTypeId) {
        item.separator = true;
        return item;
    }
    item.label = SplitMenuText(NameOf(e)).label;
    item.enabled = EnabledOf(e);
    item.submenu = IsSubmenu(u, e);
    BSTR accel = nullptr;
    if (SUCCEEDED(e->get_CurrentAcceleratorKey(&accel)) && accel) item.shortcut = accel;
    SysFreeString(accel);
    ComPtr<IUIAutomationTogglePattern> toggle;
    if (SUCCEEDED(e->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggle))) && toggle) {
        ToggleState st = ToggleState_Off;
        toggle->get_CurrentToggleState(&st);
        item.checked = st == ToggleState_On;
    } else {
        // Windows Forms reports a checked menu item only through the MSAA state.
        ComPtr<IUIAutomationLegacyIAccessiblePattern> legacy;
        DWORD state = 0;
        if (SUCCEEDED(e->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId, IID_PPV_ARGS(&legacy))) && legacy && SUCCEEDED(legacy->get_CurrentState(&state)))
            item.checked = (state & 0x10 /* STATE_SYSTEM_CHECKED */) != 0;
    }
    return item;
}

// Opens `title` and follows `path`; returns the element array of the last open menu. `opened` collects the elements to collapse again.
ComPtr<IUIAutomationElementArray> OpenPath(Uia& u, HWND hwnd, IUIAutomationElement* title, const std::vector<int>& path,
                                           std::vector<ComPtr<IUIAutomationElement>>* opened) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    auto known = KnownPopups(u, pid, hwnd);
    // Some toolkits (Windows Forms) have no expand pattern and keep the drop-down items in the tree anyway.
    if (ExpandCollapse(title, true)) opened->push_back(title);
    ComPtr<IUIAutomationElementArray> items = FindMenuChildren(u, title, pid, hwnd, known);
    ComPtr<IUIAutomationElement> owner = title;
    for (int p : path) {
        if (!items || p < 0 || p >= Length(items.Get())) return nullptr;
        ComPtr<IUIAutomationElement> sub = AtIndex(items.Get(), p);
        known = KnownPopups(u, pid, hwnd);
        if (!sub) return nullptr;
        if (ExpandCollapse(sub.Get(), true)) opened->push_back(sub);
        items = FindMenuChildren(u, sub.Get(), pid, hwnd, known);
    }
    return items;
}

void CloseOpened(const std::vector<ComPtr<IUIAutomationElement>>& opened) {
    for (auto it = opened.rbegin(); it != opened.rend(); ++it) ExpandCollapse(it->Get(), false);
}

}  // namespace

bool ReadUiaBar(HWND hwnd, AppMenuBar* out) {
    out->tier = MenuTier::None;
    out->hwnd = hwnd;
    out->titles.clear();
    Uia& u = Client();
    if (!u.ok || !IsWindow(hwnd)) return false;
    ComPtr<IUIAutomationElement> bar = FindBar(u, hwnd);
    if (!bar) return false;
    ComPtr<IUIAutomationElementArray> kids;
    if (FAILED(bar->FindAll(TreeScope_Children, u.menuItem.Get(), &kids))) return false;
    const int n = std::min(Length(kids.Get()), 40);
    for (int i = 0; i < n; ++i) {
        ComPtr<IUIAutomationElement> e = AtIndex(kids.Get(), i);
        if (!e) continue;
        AppMenuTitle t;
        t.label = SplitMenuText(NameOf(e.Get())).label;
        t.enabled = EnabledOf(e.Get());
        t.direct = !IsSubmenu(u, e.Get());
        t.index = i;  // the UIA child index
        if (t.label.empty()) continue;
        out->titles.push_back(std::move(t));
    }
    if (out->titles.empty()) return false;
    out->tier = MenuTier::Uia;
    return true;
}

bool ReadUiaItems(HWND hwnd, int titleIndex, const std::vector<int>& path, std::vector<AppMenuItem>* out) {
    out->clear();
    Uia& u = Client();
    if (!u.ok || !IsWindow(hwnd)) return false;
    ComPtr<IUIAutomationElement> title = TitleAt(u, hwnd, titleIndex);
    if (!title) return false;
    std::vector<ComPtr<IUIAutomationElement>> opened;
    ComPtr<IUIAutomationElementArray> items = OpenPath(u, hwnd, title.Get(), path, &opened);
    if (items) {
        for (int i = 0; i < std::min(Length(items.Get()), 200); ++i) {
            ComPtr<IUIAutomationElement> e = AtIndex(items.Get(), i);
            if (!e) continue;
            AppMenuItem item = ItemFrom(u, e.Get());
            item.uiaIndex = i;
            if (item.label.empty() && !item.separator) continue;
            out->push_back(std::move(item));
        }
    }
    CloseOpened(opened);
    while (!out->empty() && out->front().separator) out->erase(out->begin());
    while (!out->empty() && out->back().separator) out->pop_back();
    return !out->empty();
}

bool InvokeUia(HWND hwnd, int titleIndex, const std::vector<int>& path, const std::wstring& label) {
    Uia& u = Client();
    if (!u.ok || !IsWindow(hwnd) || path.empty()) return false;
    ComPtr<IUIAutomationElement> title = TitleAt(u, hwnd, titleIndex);
    if (!title) return false;
    // Open everything but the last step, then invoke the item at the last index.
    std::vector<int> parents(path.begin(), path.end() - 1);
    std::vector<ComPtr<IUIAutomationElement>> opened;
    ComPtr<IUIAutomationElementArray> items = OpenPath(u, hwnd, title.Get(), parents, &opened);
    bool ok = false;
    const int last = path.back();
    if (items && last >= 0 && last < Length(items.Get())) {
        ComPtr<IUIAutomationElement> e = AtIndex(items.Get(), last);
        // The menu may have changed since it was listed: the label must still match, else look for it by name.
        if (!e || SplitMenuText(NameOf(e.Get())).label != label) {
            e = nullptr;
            for (int i = 0; i < Length(items.Get()); ++i) {
                ComPtr<IUIAutomationElement> c = AtIndex(items.Get(), i);
                if (c && SplitMenuText(NameOf(c.Get())).label == label) {
                    e = c;
                    break;
                }
            }
        }
        if (e && EnabledOf(e.Get())) {
            ComPtr<IUIAutomationInvokePattern> inv;
            if (SUCCEEDED(e->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&inv))) && inv) ok = SUCCEEDED(inv->Invoke());
            else {
                ComPtr<IUIAutomationTogglePattern> tg;
                if (SUCCEEDED(e->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&tg))) && tg) ok = SUCCEEDED(tg->Toggle());
            }
        }
    }
    if (!ok) CloseOpened(opened);  // after a successful invoke the app closes its own menu
    return ok;
}

namespace {
void DumpTree(Uia& u, IUIAutomationElement* e, int depth, std::wstring* out) {
    if (depth > 3) return;
    ComPtr<IUIAutomationElementArray> kids;
    ComPtr<IUIAutomationCondition> all;
    u.a->CreateTrueCondition(&all);
    if (FAILED(e->FindAll(TreeScope_Children, all.Get(), &kids))) return;
    for (int i = 0; i < std::min(Length(kids.Get()), 30); ++i) {
        ComPtr<IUIAutomationElement> k = AtIndex(kids.Get(), i);
        BSTR cls = nullptr;
        k->get_CurrentClassName(&cls);
        BOOL off = FALSE;
        k->get_CurrentIsOffscreen(&off);
        *out += std::wstring(depth * 2, L' ') + L"type=" + std::to_wstring(ControlTypeOf(k.Get())) + L" '" + NameOf(k.Get()) + L"' class=" + (cls ? cls : L"") +
                (off ? L" offscreen" : L"") + (IsSubmenu(u, k.Get()) ? L" submenu" : L"") + L"\n";
        SysFreeString(cls);
        DumpTree(u, k.Get(), depth + 1, out);
    }
}
}  // namespace

std::wstring DescribeUiaMenu(HWND hwnd, int titleIndex) {
    std::wstring out;
    Uia& u = Client();
    if (!u.ok) return L"no UI Automation";
    ComPtr<IUIAutomationElement> title = TitleAt(u, hwnd, titleIndex);
    if (!title) return L"no title " + std::to_wstring(titleIndex);
    out += L"title '" + NameOf(title.Get()) + L"' expand=" + (ExpandCollapse(title.Get(), true) ? L"ok" : L"failed") + L"\n";
    Sleep(400);
    DumpTree(u, title.Get(), 1, &out);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    out += L"-- popups of the process:\n";
    for (auto& m : KnownPopups(u, pid, hwnd)) DumpTree(u, m.Get(), 1, &out);
    ExpandCollapse(title.Get(), false);
    return out;
}

bool InvokeUiaTitle(HWND hwnd, int titleIndex) {
    Uia& u = Client();
    if (!u.ok || !IsWindow(hwnd)) return false;
    ComPtr<IUIAutomationElement> title = TitleAt(u, hwnd, titleIndex);
    if (!title) return false;
    ComPtr<IUIAutomationInvokePattern> inv;
    return SUCCEEDED(title->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&inv))) && inv && SUCCEEDED(inv->Invoke());
}

}  // namespace velora::menubar
