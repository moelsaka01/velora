// The bundled UI font (Inter, linked in as a resource of src/shell.rc) reaches DirectWrite and its weights work.
#include <cstdio>

#include "ui/ui_common.h"

using namespace velora::ui;

int main() {
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            ++failed;
        }
    };
    const std::wstring& family = BundledFontFamily();
    check(!family.empty(), "the bundled font loads from the resource");
    check(family.find(L"Inter") != std::wstring::npos, "the bundled family is Inter");
    IDWriteTextFormat* regular = Format(13);
    IDWriteTextFormat* semi = Format(13, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    check(regular && semi, "text formats are created");
    if (regular && semi) {
        wchar_t name[128] = {};
        regular->GetFontFamilyName(name, 128);
        check(family == name, "Format() uses the bundled family");
        float wr = MeasureWidth(L"Velora Settings", regular), ws = MeasureWidth(L"Velora Settings", semi);
        check(wr > 40 && wr < 200, "13 pt text measures to a plausible width");
        check(ws > wr, "semibold is wider than regular (the variable weight axis is applied)");
        check(MeasureWidth(L"Velora Settings", Format(26)) > 1.9f * wr, "width scales with the size");
    }
    if (!failed) std::printf("font_test: all passed (family %ls)\n", family.c_str());
    return failed ? 1 : 0;
}
