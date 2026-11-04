// Velora Files: file safety (CLAUDE.md sec. 11, item 3) on a temp tree - copy / move / rename / new folder / Recycle Bin,
// conflict policies, permission denied, locked file, long path, data objects (clipboard + drag and drop), SHA-256 after every
// transfer - plus the pure model helpers (names, sorting, history, preferences, text editing). Nothing outside the temp tree
// is touched; deletes are recycled by design.
#include "core/win.h"

#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "files/file_ops.h"
#include "files/fs_model.h"
#include "ui/text_edit.h"

using namespace velora::files;
namespace fs = std::filesystem;

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

std::wstring g_root;

void Write(const std::wstring& path, const std::string& content) {
    HANDLE h = CreateFileW((path.size() >= 248 && path.rfind(L"\\\\?\\", 0) != 0 ? L"\\\\?\\" + path : path).c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD n = 0;
    WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &n, nullptr);
    CloseHandle(h);
}

bool Exists(const std::wstring& p) {
    return GetFileAttributesW((p.size() >= 248 && p.rfind(L"\\\\?\\", 0) != 0 ? L"\\\\?\\" + p : p).c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring Dir(const std::wstring& name) {
    std::wstring d = JoinPath(g_root, name);
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

// Contents with a deterministic spread of bytes so a truncated or damaged copy cannot hash the same.
std::string Payload(int seed, size_t size) {
    std::string s(size, '\0');
    unsigned x = static_cast<unsigned>(seed) * 2654435761u + 1;
    for (char& c : s) {
        x = x * 1664525u + 1013904223u;
        c = static_cast<char>(x >> 24);
    }
    return s;
}

OpOptions Quiet(Conflict c = Conflict::Ask) {
    OpOptions o;
    o.ui = false;
    o.conflict = c;
    return o;
}

void RunHidden(const std::wstring& cmd) {
    std::wstring line = cmd;
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 30000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

void PureHelpers() {
    CHECK(ValidateFileName(L"report.txt").empty());
    CHECK(!ValidateFileName(L"").empty());
    CHECK(!ValidateFileName(L"a/b").empty());
    CHECK(!ValidateFileName(L"a:b").empty());
    CHECK(!ValidateFileName(L"what?").empty());
    CHECK(!ValidateFileName(L"trailing.").empty());
    CHECK(!ValidateFileName(L"trailing ").empty());
    CHECK(!ValidateFileName(L"CON").empty());
    CHECK(!ValidateFileName(L"nul.txt").empty());
    CHECK(!ValidateFileName(L"com1").empty());
    CHECK(ValidateFileName(L"console.txt").empty());
    CHECK(!ValidateFileName(std::wstring(256, L'a')).empty());
    CHECK(ValidateFileName(std::wstring(255, L'a')).empty());

    auto taken = [](std::initializer_list<const wchar_t*> names) {
        return [names](const std::wstring& n) {
            for (const wchar_t* x : names)
                if (n == x) return true;
            return false;
        };
    };
    CHECK(UniqueName(L"a.txt", false, taken({})) == L"a.txt");
    CHECK(UniqueName(L"a.txt", false, taken({L"a.txt"})) == L"a (2).txt");
    CHECK(UniqueName(L"a.txt", false, taken({L"a.txt", L"a (2).txt"})) == L"a (3).txt");
    CHECK(UniqueName(L"a.b", true, taken({L"a.b"})) == L"a.b (2)");  // a folder name has no extension
    CHECK(UniqueName(L".gitignore", false, taken({L".gitignore"})) == L".gitignore (2)");

    CHECK(ParentOf(L"C:\\a\\b") == L"C:\\a");
    CHECK(ParentOf(L"C:\\a") == L"C:\\");
    CHECK(ParentOf(L"C:\\").empty());
    CHECK(LeafOf(L"C:\\a\\b.txt") == L"b.txt");
    CHECK(LeafOf(L"C:\\a\\b\\") == L"b");
    CHECK(JoinPath(L"C:\\", L"x") == L"C:\\x");
    CHECK(JoinPath(L"C:\\a", L"x") == L"C:\\a\\x");

    // Drop effects: same folder / into itself = none; Ctrl copies; Shift moves; same volume moves; another volume copies.
    CHECK(DropEffectFor(L"C:\\a\\f.txt", L"C:\\a", false, false) == DROPEFFECT_NONE);
    CHECK(DropEffectFor(L"C:\\a", L"C:\\a\\sub", false, false) == DROPEFFECT_NONE);
    CHECK(DropEffectFor(L"C:\\a\\f.txt", L"C:\\b", false, false) == DROPEFFECT_MOVE);
    CHECK(DropEffectFor(L"C:\\a\\f.txt", L"C:\\b", true, false) == DROPEFFECT_COPY);
    CHECK(DropEffectFor(L"C:\\a\\f.txt", L"Z:\\b", false, false) != DROPEFFECT_NONE);
    CHECK(DropEffectFor(L"C:\\a\\f.txt", L"Z:\\b", false, true) == DROPEFFECT_MOVE);

    auto crumbs = Breadcrumbs(L"C:\\Users\\me\\Docs\\");
    CHECK(crumbs.size() == 4);
    if (crumbs.size() == 4) {
        CHECK(crumbs[0].label == L"C:" && crumbs[0].path == L"C:\\");
        CHECK(crumbs[3].label == L"Docs" && crumbs[3].path == L"C:\\Users\\me\\Docs");
    }
    CHECK(Breadcrumbs(L"C:\\").size() == 1);
    auto unc = Breadcrumbs(L"\\\\srv\\share\\x");
    CHECK(unc.size() == 2 && unc[0].path == L"\\\\srv\\share" && unc[1].label == L"x");

    CHECK(FormatSize(0) == L"0 bytes");
    CHECK(FormatSize(1) == L"1 byte");
    CHECK(FormatSize(1023) == L"1023 bytes");
    CHECK(FormatSize(1536) == L"1.5 KB");
    CHECK(FormatSize(5ull * 1024 * 1024) == L"5.0 MB");
    CHECK(FormatSize(500ull * 1024 * 1024) == L"500 MB");

    History h;
    CHECK(!h.CanBack() && !h.CanForward() && h.Current().empty());
    h.Visit(L"C:\\a");
    h.Visit(L"C:\\b");
    h.Visit(L"C:\\B");  // same place again: ignored
    h.Visit(L"C:\\c");
    CHECK(h.Current() == L"C:\\c" && h.CanBack() && !h.CanForward());
    CHECK(h.Back() == L"C:\\b" && h.Back() == L"C:\\a" && h.Back().empty());
    CHECK(h.CanForward() && h.Forward() == L"C:\\b");
    h.Visit(L"C:\\d");  // a new visit drops the forward entries
    CHECK(!h.CanForward() && h.Current() == L"C:\\d");

    std::vector<Entry> e(5);
    auto set = [](Entry& x, const wchar_t* n, bool dir, std::uint64_t size, std::uint64_t mod) {
        x.name = n;
        x.isDir = dir;
        x.size = size;
        x.modified = mod;
    };
    set(e[0], L"b10.txt", false, 30, 5);
    set(e[1], L"b2.txt", false, 10, 9);
    set(e[2], L"Zdir", true, 0, 1);
    set(e[3], L"adir", true, 0, 2);
    set(e[4], L"a.md", false, 20, 7);
    SortEntries(&e, SortKey::Name, true);
    CHECK(e[0].name == L"adir" && e[1].name == L"Zdir" && e[2].name == L"a.md" && e[3].name == L"b2.txt" && e[4].name == L"b10.txt");
    SortEntries(&e, SortKey::Name, false);
    CHECK(e[0].name == L"Zdir" && e[1].name == L"adir" && e[2].name == L"b10.txt");  // folders stay first
    SortEntries(&e, SortKey::Size, false);
    CHECK(e[2].name == L"b10.txt" && e[4].name == L"b2.txt");
    SortEntries(&e, SortKey::Modified, true);
    CHECK(e[2].name == L"b10.txt" && e[4].name == L"b2.txt");

    Entry f;
    f.name = L"note.TXT";
    f.path = L"C:\\x\\note.TXT";
    Entry g = f;
    g.name = g.path = L"other.txt";
    CHECK(IconKeyFor(f).key == IconKeyFor(g).key);  // ordinary files share one icon per extension
    Entry ex;
    ex.name = L"a.exe";
    ex.path = L"C:\\x\\a.exe";
    Entry ex2 = ex;
    ex2.path = L"C:\\x\\b\\a.exe";
    CHECK(IconKeyFor(ex).key != IconKeyFor(ex2).key);  // files with their own icon are keyed by path
    Entry d1;
    d1.isDir = true;
    d1.path = L"C:\\x\\one";
    Entry d2 = d1;
    d2.path = L"C:\\x\\two";
    CHECK(IconKeyFor(d1).key != IconKeyFor(d2).key);

    Prefs p;
    p.iconView = true;
    p.sort = SortKey::Size;
    p.ascending = false;
    p.showHidden = true;
    p.width = 1111;
    p.height = 700;
    Prefs q = PrefsFromJson(PrefsToJson(p));
    CHECK(q.iconView && q.sort == SortKey::Size && !q.ascending && q.showHidden && q.width == 1111 && q.height == 700);
    CHECK(!PrefsFromJson("garbage").iconView);

    velora::ui::TextEdit t;
    t.SetText(L"hello.txt");
    t.Select(0, 5);
    CHECK(t.SelectedText() == L"hello");
    t.Insert(L'X');
    CHECK(t.Text() == L"X.txt" && t.Caret() == 1);
    t.Backspace();
    CHECK(t.Text() == L".txt" && t.Caret() == 0);
    t.Backspace();  // nothing before the caret
    CHECK(t.Text() == L".txt");
    t.End(false);
    t.Left(false);
    t.Delete();
    CHECK(t.Text() == L".tx");
    t.SelectAll();
    t.InsertText(L"ab\ncd");  // a line break ends the paste
    CHECK(t.Text() == L"ab");
    CHECK(!t.Insert(L'\t') && t.Text() == L"ab");
    t.Home(true);
    CHECK(t.HasSelection() && t.SelectedText() == L"ab");
    t.Left(false);
    CHECK(!t.HasSelection() && t.Caret() == 0);
}

void Hashing() {
    const std::wstring d = Dir(L"hash");
    Write(JoinPath(d, L"abc.txt"), "abc");
    CHECK(Sha256Hex(JoinPath(d, L"abc.txt")) == L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha256Hex(JoinPath(d, L"missing.txt")).empty());
    Write(JoinPath(d, L"empty.txt"), "");
    CHECK(Sha256Hex(JoinPath(d, L"empty.txt")) == L"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

void CopyAndMove() {
    const std::wstring src = Dir(L"cm_src"), dst = Dir(L"cm_dst");
    const std::wstring big = JoinPath(src, L"big.bin"), tiny = JoinPath(src, L"tiny.txt");
    Write(big, Payload(1, 3 * 1024 * 1024 + 17));
    Write(tiny, Payload(2, 100));
    const std::wstring hBig = Sha256Hex(big), hTiny = Sha256Hex(tiny);
    CHECK(!hBig.empty() && !hTiny.empty());

    OpResult r = Copy({big, tiny}, dst, Quiet());
    CHECK(r.Ok());
    CHECK(Sha256Hex(JoinPath(dst, L"big.bin")) == hBig);
    CHECK(Sha256Hex(JoinPath(dst, L"tiny.txt")) == hTiny);
    CHECK(Sha256Hex(big) == hBig && Sha256Hex(tiny) == hTiny);  // copy leaves the source alone

    // A folder tree.
    const std::wstring tree = Dir(L"cm_tree");
    CreateDirectoryW(JoinPath(tree, L"sub").c_str(), nullptr);
    CreateDirectoryW(JoinPath(tree, L"sub\\deeper").c_str(), nullptr);
    Write(JoinPath(tree, L"a.txt"), Payload(3, 5000));
    Write(JoinPath(tree, L"sub\\b.bin"), Payload(4, 70000));
    Write(JoinPath(tree, L"sub\\deeper\\c.bin"), Payload(5, 1));
    const std::wstring hA = Sha256Hex(JoinPath(tree, L"a.txt")), hB = Sha256Hex(JoinPath(tree, L"sub\\b.bin")),
                       hC = Sha256Hex(JoinPath(tree, L"sub\\deeper\\c.bin"));
    const std::wstring dst2 = Dir(L"cm_dst2");
    r = Copy({tree}, dst2, Quiet());
    CHECK(r.Ok());
    CHECK(Sha256Hex(JoinPath(dst2, L"cm_tree\\a.txt")) == hA);
    CHECK(Sha256Hex(JoinPath(dst2, L"cm_tree\\sub\\b.bin")) == hB);
    CHECK(Sha256Hex(JoinPath(dst2, L"cm_tree\\sub\\deeper\\c.bin")) == hC);

    // Move: hashes survive, the source is gone.
    const std::wstring mv = Dir(L"cm_mv");
    r = Move({big, tiny}, mv, Quiet());
    CHECK(r.Ok());
    CHECK(Sha256Hex(JoinPath(mv, L"big.bin")) == hBig && Sha256Hex(JoinPath(mv, L"tiny.txt")) == hTiny);
    CHECK(!Exists(big) && !Exists(tiny));
    r = Move({tree}, mv, Quiet());
    CHECK(r.Ok() && !Exists(tree));
    CHECK(Sha256Hex(JoinPath(mv, L"cm_tree\\sub\\b.bin")) == hB);

    // Moving to the folder it is already in is a no-op, not an error; a missing source is an error; a folder cannot go into itself.
    r = Move({JoinPath(mv, L"big.bin")}, mv, Quiet());
    CHECK(r.Ok() && r.items.size() == 1 && r.items[0].skipped && Sha256Hex(JoinPath(mv, L"big.bin")) == hBig);
    r = Copy({JoinPath(mv, L"nope.txt")}, dst, Quiet());
    CHECK(!r.Ok() && r.Failed() == 1 && !r.Describe().empty());
    r = Copy({JoinPath(mv, L"cm_tree")}, JoinPath(mv, L"cm_tree\\sub"), Quiet());
    CHECK(!r.Ok());
    CHECK(Sha256Hex(JoinPath(mv, L"cm_tree\\sub\\b.bin")) == hB);
    // Copy next to the original: a numbered name, the original untouched.
    r = Copy({JoinPath(mv, L"big.bin")}, mv, Quiet());
    CHECK(r.Ok() && Sha256Hex(JoinPath(mv, L"big (2).bin")) == hBig && Sha256Hex(JoinPath(mv, L"big.bin")) == hBig);
}

void Conflicts() {
    const std::wstring src = Dir(L"cf_src"), dst = Dir(L"cf_dst");
    const std::wstring s = JoinPath(src, L"doc.txt"), d = JoinPath(dst, L"doc.txt");
    Write(s, Payload(10, 4096));
    Write(d, Payload(11, 2048));
    const std::wstring hs = Sha256Hex(s), hd = Sha256Hex(d);
    CHECK(hs != hd);

    OpResult r = Copy({s}, dst, Quiet(Conflict::Skip));
    CHECK(r.Ok() && r.items[0].skipped && Sha256Hex(d) == hd);
    r = Copy({s}, dst, Quiet(Conflict::Ask));  // no UI: "ask" must never overwrite
    CHECK(r.Ok() && r.items[0].skipped && Sha256Hex(d) == hd);
    r = Move({s}, dst, Quiet(Conflict::Ask));
    CHECK(r.Ok() && r.items[0].skipped && Sha256Hex(d) == hd && Sha256Hex(s) == hs);

    r = Copy({s}, dst, Quiet(Conflict::KeepBoth));
    CHECK(r.Ok() && Sha256Hex(d) == hd && Sha256Hex(JoinPath(dst, L"doc (2).txt")) == hs);
    r = Copy({s}, dst, Quiet(Conflict::KeepBoth));
    CHECK(r.Ok() && Sha256Hex(JoinPath(dst, L"doc (3).txt")) == hs && Sha256Hex(d) == hd);

    r = Move({s}, dst, Quiet(Conflict::KeepBoth));
    CHECK(r.Ok() && !Exists(s) && Sha256Hex(JoinPath(dst, L"doc (4).txt")) == hs);

    // Replace is explicit.
    Write(s, Payload(12, 3000));
    const std::wstring hs2 = Sha256Hex(s);
    r = Copy({s}, dst, Quiet(Conflict::Replace));
    CHECK(r.Ok() && Sha256Hex(d) == hs2 && Sha256Hex(s) == hs2);

    // A folder name clash is skipped as a whole.
    const std::wstring fsrc = Dir(L"cf_fsrc"), fdst = Dir(L"cf_fdst");
    CreateDirectoryW(JoinPath(fsrc, L"same").c_str(), nullptr);
    CreateDirectoryW(JoinPath(fdst, L"same").c_str(), nullptr);
    Write(JoinPath(fsrc, L"same\\new.txt"), "new");
    Write(JoinPath(fdst, L"same\\old.txt"), "old");
    r = Copy({JoinPath(fsrc, L"same")}, fdst, Quiet(Conflict::Skip));
    CHECK(r.Ok() && r.items[0].skipped && Exists(JoinPath(fdst, L"same\\old.txt")) && !Exists(JoinPath(fdst, L"same\\new.txt")));
}

void RenamingAndFolders() {
    const std::wstring d = Dir(L"rn");
    const std::wstring a = JoinPath(d, L"a.txt"), b = JoinPath(d, L"b.txt");
    Write(a, Payload(20, 300));
    Write(b, Payload(21, 300));
    const std::wstring ha = Sha256Hex(a), hb = Sha256Hex(b);
    OpResult r = Rename(a, L"b.txt", Quiet());  // an existing name is an error, never an overwrite
    CHECK(!r.Ok() && Sha256Hex(a) == ha && Sha256Hex(b) == hb);
    r = Rename(a, L"bad/name", Quiet());
    CHECK(!r.Ok() && Sha256Hex(a) == ha);
    r = Rename(a, L"", Quiet());
    CHECK(!r.Ok() && Sha256Hex(a) == ha);
    r = Rename(a, L"c.txt", Quiet());
    CHECK(r.Ok() && !Exists(a) && Sha256Hex(JoinPath(d, L"c.txt")) == ha);
    r = Rename(JoinPath(d, L"c.txt"), L"C.txt", Quiet());  // case-only change is a rename
    CHECK(r.Ok() && Sha256Hex(JoinPath(d, L"C.txt")) == ha);
    r = Rename(JoinPath(d, L"C.txt"), L"C.txt", Quiet());
    CHECK(r.Ok() && r.items[0].skipped);
    r = Rename(JoinPath(d, L"missing.txt"), L"x.txt", Quiet());
    CHECK(!r.Ok());

    std::wstring created;
    r = NewFolder(d, L"untitled folder", Quiet(), &created);
    CHECK(r.Ok() && created == JoinPath(d, L"untitled folder") && Exists(created));
    r = NewFolder(d, L"untitled folder", Quiet(), &created);
    CHECK(r.Ok() && created == JoinPath(d, L"untitled folder (2)") && Exists(created));
    r = NewFolder(JoinPath(d, L"nope"), L"x", Quiet(), &created);
    CHECK(!r.Ok() && created.empty());

    Listing l = ListDirectory(d, false);
    CHECK(l.error == 0 && l.entries.size() == 4);  // b.txt, C.txt, two folders
    CHECK(ListDirectory(JoinPath(d, L"nope"), false).error != 0);
}

void RecycleBin() {
    const std::wstring d = Dir(L"rb");
    const std::wstring f = JoinPath(d, L"gone.txt");
    Write(f, Payload(30, 1234));
    OpResult r = Recycle({f}, Quiet());
    CHECK(r.Ok() && !Exists(f));
    CHECK(r.items.size() == 1 && r.items[0].recycled);  // it went to the Recycle Bin, not away
    std::printf("INFO recycle: recycled=%d hr=0x%08lx\n", r.items.empty() ? -1 : r.items[0].recycled, static_cast<unsigned long>(r.hr));

    const std::wstring folder = Dir(L"rb_folder");
    Write(JoinPath(folder, L"in.txt"), Payload(31, 10));
    r = Recycle({folder}, Quiet());
    CHECK(r.Ok() && !Exists(folder) && r.items[0].recycled);

    r = Recycle({JoinPath(d, L"never-existed.txt")}, Quiet());
    CHECK(!r.Ok() && r.Failed() == 1);
}

void PermissionDenied() {
    const std::wstring src = Dir(L"pd_src"), locked = Dir(L"pd_locked");
    const std::wstring s = JoinPath(src, L"f.txt");
    Write(s, Payload(40, 777));
    const std::wstring hs = Sha256Hex(s);
    // Everyone (S-1-1-0): deny create file / create subfolder in the target folder.
    RunHidden(L"icacls.exe \"" + locked + L"\" /deny *S-1-1-0:(OI)(CI)(WD,AD)");
    OpResult r = Copy({s}, locked, Quiet(Conflict::KeepBoth));
    std::printf("INFO permission: copy hr=0x%08lx items=%zu first=0x%08lx\n", static_cast<unsigned long>(r.hr), r.items.size(),
                r.items.empty() ? 0ul : static_cast<unsigned long>(r.items[0].hr));
    const bool copied = Exists(JoinPath(locked, L"f.txt"));
    CHECK(!copied);                 // nothing may appear in a folder we may not write to
    CHECK(!r.Ok());                 // and the failure must be reported, not swallowed
    CHECK(Sha256Hex(s) == hs);
    r = Move({s}, locked, Quiet(Conflict::KeepBoth));
    CHECK(!r.Ok() && Exists(s) && Sha256Hex(s) == hs && !Exists(JoinPath(locked, L"f.txt")));  // a failed move keeps the source
    RunHidden(L"icacls.exe \"" + locked + L"\" /remove:d *S-1-1-0");
    r = Copy({s}, locked, Quiet());
    CHECK(r.Ok() && Sha256Hex(JoinPath(locked, L"f.txt")) == hs);  // allowed again
}

void LockedFile() {
    const std::wstring src = Dir(L"lk_src"), dst = Dir(L"lk_dst");
    const std::wstring s = JoinPath(src, L"busy.bin");
    Write(s, Payload(50, 200000));
    const std::wstring hs = Sha256Hex(s);
    HANDLE lock = CreateFileW(s.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);  // exclusive
    CHECK(lock != INVALID_HANDLE_VALUE);
    OpResult r = Copy({s}, dst, Quiet());
    std::printf("INFO locked: copy ok=%d hr=0x%08lx first=0x%08lx\n", r.Ok(), static_cast<unsigned long>(r.hr),
                r.items.empty() ? 0ul : static_cast<unsigned long>(r.items[0].hr));
    CHECK(!r.Ok());
    const std::wstring copy = JoinPath(dst, L"busy.bin");
    CHECK(!Exists(copy));  // no half-copied file left behind
    r = Move({s}, dst, Quiet());
    CHECK(!r.Ok() && Exists(s));
    r = Recycle({s}, Quiet());
    CHECK(!r.Ok() && Exists(s));
    CloseHandle(lock);
    CHECK(Sha256Hex(s) == hs);  // the locked file was never altered
    r = Copy({s}, dst, Quiet());
    CHECK(r.Ok() && Sha256Hex(copy) == hs);
}

void LongPaths() {
    // A directory chain whose full path is over 300 characters, created with the \\?\ prefix.
    std::wstring deep = L"\\\\?\\" + g_root + L"\\lp";
    CreateDirectoryW(deep.c_str(), nullptr);
    for (int i = 0; deep.size() < 330; ++i) {
        deep += L"\\" + std::wstring(40, static_cast<wchar_t>(L'a' + i % 20));
        CreateDirectoryW(deep.c_str(), nullptr);
    }
    const std::wstring longDir = deep.substr(4);  // without the prefix
    CHECK(longDir.size() > 300);
    const std::wstring longFile = JoinPath(longDir, L"long-file.bin");
    Write(longFile, Payload(60, 12345));
    const std::wstring hs = Sha256Hex(longFile);
    CHECK(Exists(longFile) && !hs.empty());

    // Out of a long path and into one. The shell may or may not manage; either way nothing may be lost or damaged.
    const std::wstring dst = Dir(L"lp_dst");
    OpResult r = Copy({longFile}, dst, Quiet());
    std::printf("INFO long path: copy out ok=%d hr=0x%08lx\n", r.Ok(), static_cast<unsigned long>(r.hr));
    CHECK(Sha256Hex(longFile) == hs);
    if (r.Ok()) CHECK(Sha256Hex(JoinPath(dst, L"long-file.bin")) == hs);
    else CHECK(!Exists(JoinPath(dst, L"long-file.bin")) || Sha256Hex(JoinPath(dst, L"long-file.bin")) == hs);

    const std::wstring plain = JoinPath(dst, L"plain.bin");
    Write(plain, Payload(61, 999));
    const std::wstring hp = Sha256Hex(plain);
    r = Copy({plain}, longDir, Quiet());
    std::printf("INFO long path: copy in ok=%d hr=0x%08lx\n", r.Ok(), static_cast<unsigned long>(r.hr));
    CHECK(Sha256Hex(plain) == hp);
    if (r.Ok()) CHECK(Sha256Hex(JoinPath(longDir, L"plain.bin")) == hp);
    else CHECK(!Exists(JoinPath(longDir, L"plain.bin")) || Sha256Hex(JoinPath(longDir, L"plain.bin")) == hp);

    // Listing a long directory works (Win32 with the \\?\ prefix).
    Listing l = ListDirectory(longDir, false);
    CHECK(l.error == 0 && !l.entries.empty());
}

void DataObjects() {
    const std::wstring src = Dir(L"do_src"), dst = Dir(L"do_dst"), dst2 = Dir(L"do_dst2");
    const std::wstring a = JoinPath(src, L"one.bin"), b = JoinPath(src, L"two.txt");
    Write(a, Payload(70, 50000));
    Write(b, Payload(71, 10));
    const std::wstring ha = Sha256Hex(a), hb = Sha256Hex(b);

    auto data = MakeDataObject({a, b});
    CHECK(data != nullptr);
    if (!data) return;
    std::vector<std::wstring> paths = PathsFromDataObject(data.Get());
    CHECK(paths.size() == 2);
    CHECK(SetPreferredDropEffect(data.Get(), DROPEFFECT_MOVE));
    CHECK(GetPreferredDropEffect(data.Get()) == DROPEFFECT_MOVE);

    // Drop / paste of a copy.
    OpResult r = Transfer(data.Get(), dst, false, Quiet());
    CHECK(r.Ok() && Sha256Hex(JoinPath(dst, L"one.bin")) == ha && Sha256Hex(JoinPath(dst, L"two.txt")) == hb);
    CHECK(Sha256Hex(a) == ha && Sha256Hex(b) == hb);
    // Drop / paste of a cut (move).
    r = Transfer(data.Get(), dst2, true, Quiet());
    CHECK(r.Ok() && !Exists(a) && !Exists(b));
    CHECK(Sha256Hex(JoinPath(dst2, L"one.bin")) == ha && Sha256Hex(JoinPath(dst2, L"two.txt")) == hb);

    CHECK(MakeDataObject({}) == nullptr);
    CHECK(MakeDataObject({JoinPath(src, L"does-not-exist.txt")}) == nullptr);
}

}  // namespace

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    OleInitialize(nullptr);

    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    wchar_t longTmp[MAX_PATH] = {};
    GetLongPathNameW(tmp, longTmp, MAX_PATH);
    g_root = JoinPath(longTmp, L"velora-files-test-" + std::to_wstring(GetCurrentProcessId()));
    std::wstring r = g_root;
    while (r.size() > 3 && r.back() == L'\\') r.pop_back();
    g_root = r;
    CreateDirectoryW(g_root.c_str(), nullptr);

    PureHelpers();
    Hashing();
    CopyAndMove();
    Conflicts();
    RenamingAndFolders();
    RecycleBin();
    PermissionDenied();
    LockedFile();
    LongPaths();
    DataObjects();

    // Cleanup: drop leftover deny entries, then remove the tree (the test tree is ours; not through the Recycle Bin).
    RunHidden(L"icacls.exe \"" + g_root + L"\" /reset /T /C /Q");
    std::error_code ec;
    fs::remove_all(fs::path(L"\\\\?\\" + g_root), ec);
    if (ec) std::printf("INFO cleanup: could not remove %ls (%s)\n", g_root.c_str(), ec.message().c_str());

    OleUninitialize();
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
