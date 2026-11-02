#include "menubar/menu_layout.h"

#include <algorithm>

#include "render/tokens.h"

namespace velora::menubar {

namespace tm = velora::tokens::menu;

MenuLayout ComputeMenuLayout(const std::vector<MenuItem>& items) {
    MenuLayout l;
    l.width = tm::kPopupWidth;
    float y = tm::kPopupPadding;
    for (const MenuItem& it : items) {
        float h = it.separator ? tm::kSeparatorHeight : tm::kRowHeight;
        l.top.push_back(y);
        l.bottom.push_back(y + h);
        y += h;
    }
    l.height = y + tm::kPopupPadding;
    return l;
}

int HitTestMenu(const MenuLayout& layout, const std::vector<MenuItem>& items, float y) {
    for (size_t i = 0; i < items.size() && i < layout.top.size(); ++i)
        if (y >= layout.top[i] && y < layout.bottom[i]) return (items[i].separator || !items[i].enabled) ? -1 : static_cast<int>(i);
    return -1;
}

int NextSelectable(const std::vector<MenuItem>& items, int current, int dir) {
    const int n = static_cast<int>(items.size());
    if (n == 0 || (dir != 1 && dir != -1)) return -1;
    int i = current;
    if (i < 0 || i >= n) i = dir > 0 ? -1 : n;  // nothing selected: start from the matching end
    for (int step = 0; step < n; ++step) {
        i = ((i + dir) % n + n) % n;
        if (!items[i].separator && items[i].enabled) return i;
    }
    return -1;
}

float TitleCapsuleWidth(float labelWidth) { return labelWidth + 2 * tm::kCapsulePadX; }

float PopupLeft(float anchorLeft, float popupWidth, float monitorLeft, float monitorRight) {
    return std::max(monitorLeft, std::min(anchorLeft, monitorRight - popupWidth));
}

}  // namespace velora::menubar
