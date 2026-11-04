// Search core: hotkey parsing, query terms and SQL, classification, merging, row model and layout. Pure logic.
#include <cstdio>

#include "search/search_core.h"

using namespace velora::search;

int main() {
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            ++failed;
        }
    };

    // Hotkeys.
    Hotkey h;
    check(ParseHotkey(L"Alt+Space", &h) && h.mods == kModAlt && h.vk == 0x20, "Alt+Space");
    check(ParseHotkey(L"ctrl + shift + k", &h) && h.mods == (kModControl | kModShift) && h.vk == 'K', "case and blanks");
    check(ParseHotkey(L"Win+F2", &h) && h.mods == kModWin && h.vk == 0x71, "function key");
    check(ParseHotkey(L"Ctrl+Alt+7", &h) && h.vk == '7', "digit");
    check(ParseHotkey(L"Ctrl+Alt+Up", &h) && h.mods == (kModControl | kModAlt) && h.vk == 0x26 && FormatHotkey(h) == L"Ctrl+Alt+Up", "arrow key");
    check(!ParseHotkey(L"Space", &h), "a bare key would steal typing");
    check(!ParseHotkey(L"Alt+", &h) && !ParseHotkey(L"", &h) && !ParseHotkey(L"Alt+Banana", &h) && !ParseHotkey(L"Foo+K", &h), "garbage");
    check(!ParseHotkey(L"Alt+F13", &h) && !ParseHotkey(L"Alt+F0", &h), "function keys F1-F12 only");
    check(FormatHotkey(Hotkey{kModAlt, 0x20}) == L"Alt+Space" && FormatHotkey(Hotkey{kModControl | kModShift, 'K'}) == L"Ctrl+Shift+K" &&
              FormatHotkey(Hotkey{kModWin, 0x72}) == L"Win+F3",
          "format");
    Hotkey rt;
    check(ParseHotkey(FormatHotkey(Hotkey{kModControl | kModAlt, 'Q'}), &rt) && rt == Hotkey{kModControl | kModAlt, 'Q'}, "round trip");

    // Terms and SQL.
    auto t = QueryTerms(L"  quarterly   report-2026 ");
    check(t.size() == 2 && t[0] == L"quarterly" && t[1] == L"report-2026", "terms split on blanks, keep hyphens");
    check(QueryTerms(L"a'b\"c*d%e").size() == 5, "quotes, wildcards and percent signs split terms");
    check(QueryTerms(L"1 2 3 4 5 6 7 8").size() == 6, "at most 6 terms");
    check(QueryTerms(std::wstring(100, L'x'))[0].size() == 40, "terms are cut at 40");
    check(BuildFileSql(L"", 10).empty() && BuildFileSql(L"a", 10).empty() && BuildFileSql(L"  ", 10).empty(), "too short for files");
    check(!BuildFileSql(L"ab", 10).empty(), "two characters search");
    const std::wstring sql = BuildFileSql(L"quarterly report", 30);
    check(sql.find(L"SELECT TOP 30") == 0, "top");
    check(sql.find(L"FROM SystemIndex") != std::wstring::npos && sql.find(L"SCOPE='file:'") != std::wstring::npos, "index scope");
    check(sql.find(L"CONTAINS(System.ItemNameDisplay,'\"quarterly*\"')") != std::wstring::npos &&
              sql.find(L"CONTAINS(System.ItemNameDisplay,'\"report*\"')") != std::wstring::npos,
          "every word is a prefix");
    check(sql.find(L" AND CONTAINS") != std::wstring::npos, "words are ANDed");
    // Injection: nothing from the user can add a quote, a comment or a statement.
    const std::wstring evil = BuildFileSql(L"x' OR 1=1 --\" ); DROP", 10);
    int quotes = 0;
    for (wchar_t c : evil)
        if (c == L'\'') ++quotes;
    check(quotes == 2 * 5 + 2 && evil.find(L"1=1") == std::wstring::npos, "no quote or operator survives from the input");
    check(BuildFileSql(L"abc", 100000).find(L"TOP 200") != std::wstring::npos, "TOP is capped");

    // Classification.
    check(ClassifyFile(L"x", L"Directory") == Category::Folder, "folder");
    check(ClassifyFile(L"report.pdf", L".pdf") == Category::Document, "pdf");
    check(ClassifyFile(L"IMG.JPG", L".JPG") == Category::Image, "jpg uppercase");
    check(ClassifyFile(L"song.flac", L".flac") == Category::Audio && ClassifyFile(L"clip.mkv", L".mkv") == Category::Video, "audio/video");
    check(ClassifyFile(L"tool.exe", L".exe") == Category::Other, "other");
    check(ClassifyFile(L"notes.txt", L"") == Category::Document, "falls back to the name's extension");
    check(IsNoiseFilePath(L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Notepad.lnk") && IsNoiseFilePath(L"C:\\$Recycle.Bin\\S-1\\x") &&
              IsNoiseFilePath(L"C:\\Users\\me\\desktop.ini") && !IsNoiseFilePath(L"C:\\Users\\me\\Documents\\notes.txt"),
          "noise paths");

    // Merge and rows.
    auto mk = [](Category c, const wchar_t* name, int score) {
        Result r;
        r.category = c;
        r.name = name;
        r.score = score;
        return r;
    };
    std::vector<Result> apps = {mk(Category::App, L"Notes", 10), mk(Category::App, L"Notepad", 90), mk(Category::App, L"Snotes", 5)};
    std::vector<Result> files = {mk(Category::Document, L"b.txt", 5), mk(Category::Folder, L"Notes folder", 3), mk(Category::Document, L"a.txt", 50),
                                 mk(Category::Image, L"n.png", 1)};
    auto merged = MergeResults(apps, files);
    check(merged.size() == 7, "all kept under the budget");
    check(merged[0].name == L"Notepad" && merged[1].name == L"Notes" && merged[2].name == L"Snotes", "apps by score");
    check(merged[3].category == Category::Folder && merged[4].name == L"a.txt" && merged[5].name == L"b.txt" && merged[6].category == Category::Image,
          "files by section, then score");
    check(MergeResults(apps, files, 1, 2).size() == 3, "budgets cap apps and files");

    auto rows = BuildRows(merged);
    check(rows.size() == 7 + 4, "four section headers (apps, folders, documents, images)");
    check(rows[0].header && rows[0].text == L"Applications" && !rows[1].header && rows[1].result == 0, "first header then first result");
    check(FirstSelectable(rows) == 1, "the first selectable row skips the header");
    check(NextSelectable(rows, 1, 1) == 2 && NextSelectable(rows, 3, 1) == 5, "down skips headers");
    check(NextSelectable(rows, 1, -1) == static_cast<int>(rows.size()) - 1, "up wraps to the last result");
    check(NextSelectable(rows, static_cast<int>(rows.size()) - 1, 1) == 1, "down wraps to the first result");
    check(FirstSelectable({}) == -1 && NextSelectable({}, -1, 1) == -1, "no rows");

    // Layout.
    auto layout = LayoutRows(rows);
    check(layout.top.size() == rows.size() && layout.top[0] == 0 && layout.top[1] == layout.height[0], "rows stack");
    check(layout.total == 4 * 28.0f + 7 * 48.0f, "4 headers + 7 rows");
    auto small = LayoutRows(BuildRows(MergeResults(apps, {})));
    check(PanelHeight(small) == 28.0f + 3 * 48.0f + 16, "panel = rows + padding");
    check(PanelHeight(layout) == 410.0f, "a long list is capped at 410");
    std::vector<Row> many(40);
    for (int i = 0; i < 40; ++i) many[i].result = i;
    check(PanelHeight(LayoutRows(many)) == 410.0f, "panel capped at 410");
    check(PanelHeight(LayoutRows({})) == 0, "no rows, no panel");
    check(ScrollToRow(0, layout, 10, 200) > 0 && ScrollToRow(500, layout, 1, 200) < 500, "scroll follows the selection");

    std::printf("%s\n", failed ? "FAILED" : "PASS");
    return failed ? 1 : 0;
}
