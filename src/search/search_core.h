// Search core (CLAUDE.md sec. 5 search G1): hotkey parsing, query terms, the Windows Search SQL, file classification, result
// merging and the row model of the panel. Pure (no windows.h), unit-tested. Owner: C.
#pragma once

#include <string>
#include <vector>

namespace velora::search {

// ---- hotkey ----
inline constexpr unsigned kModAlt = 1, kModControl = 2, kModShift = 4, kModWin = 8;  // same values as MOD_*
struct Hotkey {
    unsigned mods = 0;
    unsigned vk = 0;
    bool Valid() const { return vk != 0 && mods != 0; }  // a bare key would steal typing
    friend bool operator==(const Hotkey&, const Hotkey&) = default;
};
// "Alt+Space", "ctrl+shift+K", "Win+F2" ... (case-insensitive; Space, Enter, Tab, A-Z, 0-9, F1-F12). False for anything else,
// including a hotkey without a modifier.
bool ParseHotkey(const std::wstring& text, Hotkey* out);
std::wstring FormatHotkey(const Hotkey& h);

// ---- query ----
// Search words: split on blanks and punctuation that the index treats as separators; only letters, digits and "-_.+#&@" survive;
// at most 6 words, each at most 40 characters.
std::vector<std::wstring> QueryTerms(const std::wstring& query);
// The Windows Search SQL for the file part of a query ("" when the query is too short to search files: under 2 characters).
// Every word must start a word of the file name; quotes and wildcards in the input cannot change the statement.
std::wstring BuildFileSql(const std::wstring& query, int top);

// ---- results ----
enum class Category { App = 0, Folder, Document, Image, Audio, Video, Other };  // also the order of the sections
const wchar_t* CategoryTitle(Category c);

struct Result {
    Category category = Category::Other;
    std::wstring name;
    std::wstring detail;  // folder path of a file ("Application" for an app)
    std::wstring path;    // files and folders: the full path; apps: the executable when known
    int appIndex = -1;    // apps: index into the app snapshot the result was made from
    std::wstring icon;    // IconStore source
    int score = 0;
};

Category ClassifyFile(const std::wstring& name, const std::wstring& itemType);  // itemType "Directory" for folders, else ".ext"
// Index entries that are noise: Start-menu shortcuts (apps already cover them), recycle bins, system folders.
bool IsNoiseFilePath(const std::wstring& path);
// Apps first (best score first, at most maxApps), then files by section and score (at most maxFiles). Stable for equal scores.
std::vector<Result> MergeResults(std::vector<Result> apps, std::vector<Result> files, size_t maxApps = 6, size_t maxFiles = 14);

struct Row {
    bool header = false;
    std::wstring text;  // section title
    int result = -1;    // index into the merged results (-1 for a header)
};
std::vector<Row> BuildRows(const std::vector<Result>& results);
int FirstSelectable(const std::vector<Row>& rows);                 // -1 when there is no result
int NextSelectable(const std::vector<Row>& rows, int current, int dir);  // wraps around; skips headers

struct RowLayout {
    std::vector<float> top, height;  // per row, relative to the first row
    float total = 0;                 // all rows
};
RowLayout LayoutRows(const std::vector<Row>& rows);
// Height of the results panel for a row layout: padding above and below, capped at the spec's 410 pt; 0 for no rows.
float PanelHeight(const RowLayout& layout);
// Scroll offset that keeps row `index` inside a viewport of `viewH`.
float ScrollToRow(float scroll, const RowLayout& layout, int index, float viewH);

}  // namespace velora::search
