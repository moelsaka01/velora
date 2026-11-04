#include "search/search_core.h"

#include <algorithm>
#include <cwctype>

#include "render/tokens.h"

namespace velora::search {

namespace {

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

std::wstring ExtOf(const std::wstring& name) {
    const size_t dot = name.rfind(L'.');
    return dot == std::wstring::npos || dot == 0 ? std::wstring{} : Lower(name.substr(dot));
}

bool OneOf(const std::wstring& ext, std::initializer_list<const wchar_t*> list) {
    for (const wchar_t* e : list)
        if (ext == e) return true;
    return false;
}

}  // namespace

bool ParseHotkey(const std::wstring& text, Hotkey* out) {
    Hotkey h;
    std::wstring part;
    std::vector<std::wstring> parts;
    for (wchar_t c : text + L"+") {
        if (c == L'+') {
            // trim
            while (!part.empty() && part.back() == L' ') part.pop_back();
            size_t b = 0;
            while (b < part.size() && part[b] == L' ') ++b;
            parts.push_back(Lower(part.substr(b)));
            part.clear();
        } else {
            part += c;
        }
    }
    if (parts.size() < 2) return false;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::wstring& m = parts[i];
        if (m == L"alt") h.mods |= kModAlt;
        else if (m == L"ctrl" || m == L"control") h.mods |= kModControl;
        else if (m == L"shift") h.mods |= kModShift;
        else if (m == L"win" || m == L"windows" || m == L"super") h.mods |= kModWin;
        else return false;
    }
    const std::wstring& key = parts.back();
    if (key == L"space") h.vk = 0x20;
    else if (key == L"enter" || key == L"return") h.vk = 0x0D;
    else if (key == L"tab") h.vk = 0x09;
    else if (key.size() == 1 && ((key[0] >= L'a' && key[0] <= L'z') || (key[0] >= L'0' && key[0] <= L'9')))
        h.vk = static_cast<unsigned>(std::towupper(key[0]));
    else if (key.size() >= 2 && key[0] == L'f' && std::iswdigit(key[1])) {
        const int n = std::stoi(key.substr(1));
        if (n < 1 || n > 12) return false;
        h.vk = 0x70 + static_cast<unsigned>(n - 1);
    } else return false;
    if (!h.Valid()) return false;
    if (out) *out = h;
    return true;
}

std::wstring FormatHotkey(const Hotkey& h) {
    std::wstring s;
    if (h.mods & kModControl) s += L"Ctrl+";
    if (h.mods & kModAlt) s += L"Alt+";
    if (h.mods & kModShift) s += L"Shift+";
    if (h.mods & kModWin) s += L"Win+";
    if (h.vk == 0x20) s += L"Space";
    else if (h.vk == 0x0D) s += L"Enter";
    else if (h.vk == 0x09) s += L"Tab";
    else if (h.vk >= 0x70 && h.vk <= 0x7B) s += L"F" + std::to_wstring(h.vk - 0x70 + 1);
    else s += static_cast<wchar_t>(h.vk);
    return s;
}

std::vector<std::wstring> QueryTerms(const std::wstring& query) {
    std::vector<std::wstring> terms;
    std::wstring cur;
    auto flush = [&] {
        // A word needs a letter or digit ("--" alone searches for nothing).
        const bool real = std::any_of(cur.begin(), cur.end(), [](wchar_t c) { return std::iswalnum(c) != 0; });
        if (real && terms.size() < 6) terms.push_back(cur.substr(0, 40));
        cur.clear();
    };
    for (wchar_t c : query) {
        if (std::iswalnum(c) || c == L'-' || c == L'_' || c == L'.' || c == L'+' || c == L'#' || c == L'&' || c == L'@') cur += c;
        else flush();
    }
    flush();
    return terms;
}

std::wstring BuildFileSql(const std::wstring& query, int top) {
    const std::vector<std::wstring> terms = QueryTerms(query);
    size_t chars = 0;
    for (const auto& t : terms) chars += t.size();
    if (terms.empty() || chars < 2) return {};
    std::wstring sql = L"SELECT TOP " + std::to_wstring(std::clamp(top, 1, 200)) +
                       L" System.ItemNameDisplay, System.ItemPathDisplay, System.ItemType FROM SystemIndex WHERE SCOPE='file:'";
    for (const auto& t : terms) sql += L" AND CONTAINS(System.ItemNameDisplay,'\"" + t + L"*\"')";  // t holds no quote, wildcard or blank
    sql += L" ORDER BY System.Search.Rank DESC";
    return sql;
}

const wchar_t* CategoryTitle(Category c) {
    switch (c) {
        case Category::App: return L"Applications";
        case Category::Folder: return L"Folders";
        case Category::Document: return L"Documents";
        case Category::Image: return L"Images";
        case Category::Audio: return L"Music";
        case Category::Video: return L"Videos";
        default: return L"Other Files";
    }
}

Category ClassifyFile(const std::wstring& name, const std::wstring& itemType) {
    if (_wcsicmp(itemType.c_str(), L"Directory") == 0) return Category::Folder;
    std::wstring ext = Lower(itemType);
    if (ext.empty() || ext[0] != L'.') ext = ExtOf(name);
    if (OneOf(ext, {L".doc", L".docx", L".odt", L".rtf", L".txt", L".md", L".pdf", L".xls", L".xlsx", L".ods", L".csv", L".ppt", L".pptx", L".odp",
                    L".pages", L".numbers", L".key", L".epub", L".tex", L".log", L".json", L".xml", L".html", L".htm"}))
        return Category::Document;
    if (OneOf(ext, {L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".tif", L".tiff", L".webp", L".heic", L".svg", L".ico", L".raw"})) return Category::Image;
    if (OneOf(ext, {L".mp3", L".wav", L".flac", L".aac", L".m4a", L".ogg", L".wma", L".opus"})) return Category::Audio;
    if (OneOf(ext, {L".mp4", L".mkv", L".mov", L".avi", L".wmv", L".webm", L".m4v", L".mpg", L".mpeg"})) return Category::Video;
    return Category::Other;
}

bool IsNoiseFilePath(const std::wstring& path) {
    const std::wstring p = Lower(path);
    for (const wchar_t* part : {L"\\start menu\\", L"\\$recycle.bin", L"\\windows\\winsxs\\", L"\\windows\\system32\\", L"\\windows\\servicing\\"})
        if (p.find(part) != std::wstring::npos) return true;
    return p.size() >= 11 && p.compare(p.size() - 11, 11, L"desktop.ini") == 0;
}

std::vector<Result> MergeResults(std::vector<Result> apps, std::vector<Result> files, size_t maxApps, size_t maxFiles) {
    auto byScore = [](const Result& a, const Result& b) { return a.score > b.score; };
    std::stable_sort(apps.begin(), apps.end(), byScore);
    if (apps.size() > maxApps) apps.resize(maxApps);
    for (Result& r : apps) r.category = Category::App;
    std::stable_sort(files.begin(), files.end(), [](const Result& a, const Result& b) {
        if (a.category != b.category) return a.category < b.category;
        return a.score > b.score;
    });
    // Keep the best of each section within the budget: walk the sections in order, so Folders and Documents are never starved by Other Files.
    std::vector<Result> out = std::move(apps);
    size_t taken = 0;
    for (Result& r : files) {
        if (taken >= maxFiles) break;
        out.push_back(std::move(r));
        ++taken;
    }
    return out;
}

std::vector<Row> BuildRows(const std::vector<Result>& results) {
    std::vector<Row> rows;
    bool have = false;
    Category cur = Category::App;
    for (size_t i = 0; i < results.size(); ++i) {
        if (!have || results[i].category != cur) {
            rows.push_back({true, CategoryTitle(results[i].category), -1});
            cur = results[i].category;
            have = true;
        }
        rows.push_back({false, {}, static_cast<int>(i)});
    }
    return rows;
}

int FirstSelectable(const std::vector<Row>& rows) {
    for (size_t i = 0; i < rows.size(); ++i)
        if (!rows[i].header) return static_cast<int>(i);
    return -1;
}

int NextSelectable(const std::vector<Row>& rows, int current, int dir) {
    const int n = static_cast<int>(rows.size());
    if (n == 0 || FirstSelectable(rows) < 0) return -1;
    int i = current;
    for (int step = 0; step < n; ++step) {
        i = ((i + dir) % n + n) % n;
        if (!rows[i].header) return i;
    }
    return current;
}

RowLayout LayoutRows(const std::vector<Row>& rows) {
    namespace sp = velora::tokens::searchpanel;
    RowLayout l;
    float y = 0;
    for (const Row& r : rows) {
        const float h = r.header ? sp::kHeaderHeight : sp::kRowHeight;
        l.top.push_back(y);
        l.height.push_back(h);
        y += h;
    }
    l.total = y;
    return l;
}

float PanelHeight(const RowLayout& layout) {
    namespace sp = velora::tokens::searchpanel;
    if (layout.top.empty()) return 0;
    return std::min(sp::kResultsMaxHeight, layout.total + 2 * sp::kPanelPad);
}

float ScrollToRow(float scroll, const RowLayout& layout, int index, float viewH) {
    if (index < 0 || index >= static_cast<int>(layout.top.size())) return scroll;
    float top = layout.top[index], bottom = top + layout.height[index];
    // A header above the row stays with it.
    if (index > 0 && layout.height[index - 1] == velora::tokens::searchpanel::kHeaderHeight) top -= layout.height[index - 1];
    if (top < scroll) return top;
    if (bottom > scroll + viewH) return bottom - viewH;
    return scroll;
}

}  // namespace velora::search
