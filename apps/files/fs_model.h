// Directory model of Velora Files: listing, sorting, formatting, sidebar locations, history, icon keys and the
// preferences stored in settings.json. Pure logic where possible (unit-tested). Owner: C.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/win.h"

namespace velora::files {

struct Entry {
    std::wstring name;
    std::wstring path;
    bool isDir = false;
    bool hidden = false;       // hidden or system attribute
    std::uint64_t size = 0;    // bytes (files)
    std::uint64_t modified = 0;  // FILETIME ticks (UTC)
    DWORD attrs = 0;
};

struct Listing {
    std::vector<Entry> entries;
    DWORD error = 0;  // Win32 error when the directory could not be read
};

// Reads one directory (long paths supported). Hidden and system entries are left out unless `showHidden`.
Listing ListDirectory(const std::wstring& dir, bool showHidden);

enum class SortKey { Name, Modified, Size, Kind };
// Folders first, then the key; ties break by natural name order. `ascending` flips the key, never the folder grouping.
void SortEntries(std::vector<Entry>* entries, SortKey key, bool ascending);

std::wstring FormatSize(std::uint64_t bytes);  // "4.2 MB", "812 KB", "0 bytes"
// "Today, 14:32" / "Yesterday, 09:05" / "2 Nov 2026"; both arguments are FILETIME ticks (UTC).
std::wstring FormatModified(std::uint64_t modified, std::uint64_t now);
// Description of the kind of an entry ("Folder", "Text Document"); cached per extension. UI thread.
std::wstring KindOf(const Entry& e);
// "C:\Users\me\Docs" -> {"C:", "Users", "me", "Docs"} with the cumulative path of each part.
struct Crumb {
    std::wstring label, path;
};
std::vector<Crumb> Breadcrumbs(const std::wstring& path);

struct Place {
    std::wstring label, path;
    int glyph = 0;  // files::Glyph (drawn by the window)
};
std::vector<Place> FavoritePlaces();  // Home, Desktop, Documents, Downloads, Pictures, Music, Videos (those that exist)
struct DriveInfo {
    std::wstring label, path;  // "Windows (C:)", "C:\"
    DWORD type = 0;            // GetDriveType
    std::uint64_t total = 0, free = 0;
};
std::vector<DriveInfo> Drives();

class History {
public:
    // Records a visit: drops the forward entries; the same path twice in a row is ignored.
    void Visit(const std::wstring& path);
    bool CanBack() const { return pos_ > 0; }
    bool CanForward() const { return pos_ + 1 < list_.size(); }
    std::wstring Back();     // "" when there is nothing to go back to
    std::wstring Forward();
    std::wstring Current() const { return list_.empty() ? std::wstring{} : list_[pos_]; }

private:
    std::vector<std::wstring> list_;
    size_t pos_ = 0;
};

// Icons: `key` identifies the picture (one per file extension for ordinary files, one per path for folders and for files
// that carry their own icon), `source` is a real path the shell can load it from.
struct IconKey {
    std::wstring key, source;
};
IconKey IconKeyFor(const Entry& e);

struct Prefs {
    bool iconView = false;
    SortKey sort = SortKey::Name;
    bool ascending = true;
    bool showHidden = false;
    int width = 960, height = 620;  // DIPs
};
std::string PrefsToJson(const Prefs& p);
Prefs PrefsFromJson(const std::string& text);
Prefs LoadPrefs(const std::wstring& settingsPath);
bool SavePrefs(const std::wstring& settingsPath, const Prefs& p);  // read-modify-write of the "files" section

}  // namespace velora::files
