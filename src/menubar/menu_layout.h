// Menu popup geometry and keyboard/mouse navigation: pure maths (no windows.h), unit-tested. Owner: B.
//
// Numbers come from docs/MACOS_UI_SPEC.md ("Menu bar and menus"): 24 pt rows, a separator group adds 11 pt (5.5 above
// and below the hairline, so one separator plus one row is a 35 pt pitch), 5 pt inner padding, 201.5 pt wide.
// Seven rows and four separators come to 222 pt, which is the measured popup height.
#pragma once

#include <string>
#include <vector>

namespace velora::menubar {

struct MenuItem {
    unsigned id = 0;
    std::wstring label;
    bool separator = false;
    bool enabled = true;
    bool checked = false;     // a tick in front of the label (app menus)
    bool submenu = false;     // an arrow at the right edge; choosing the row opens what is behind it
    std::wstring shortcut;    // right-aligned, dimmed ("Ctrl+N")
};

inline MenuItem Separator() { return MenuItem{0, {}, true, false}; }

struct MenuLayout {
    float width = 0, height = 0;
    std::vector<float> top;     // per item, relative to the popup top
    std::vector<float> bottom;  // per item
};

MenuLayout ComputeMenuLayout(const std::vector<MenuItem>& items);
// Index of the selectable item at popup-relative `y`; -1 over a separator, a disabled item, the padding or outside.
int HitTestMenu(const MenuLayout& layout, const std::vector<MenuItem>& items, float y);
// The next selectable item after `current` (-1: none yet) in direction `dir` (+1 / -1), wrapping around; -1 if none.
int NextSelectable(const std::vector<MenuItem>& items, int current, int dir);
// Width of the highlighted title capsule for a label of `labelWidth` DIPs.
float TitleCapsuleWidth(float labelWidth);
// Left edge of a popup of `popupWidth` anchored at `anchorLeft`, kept inside [monitorLeft, monitorRight].
float PopupLeft(float anchorLeft, float popupWidth, float monitorLeft, float monitorRight);

}  // namespace velora::menubar
