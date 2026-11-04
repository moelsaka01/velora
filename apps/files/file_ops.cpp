#include "files/file_ops.h"

#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl/implements.h>

#include <algorithm>
#include <cwctype>
#include <map>

#include "core/log.h"
#include "core/text.h"

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace velora::files {

using Microsoft::WRL::ComPtr;

namespace {

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

bool EqualsNoCase(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

// Win32 file APIs accept long paths only with the \\?\ prefix (the shell's own parsing handles what it can).
std::wstring Long(const std::wstring& p) {
    if (p.size() < 248 || p.rfind(L"\\\\?\\", 0) == 0) return p;
    if (p.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + p.substr(2);
    return L"\\\\?\\" + p;
}

bool PathExists(const std::wstring& p) {
    const std::wstring lp = Long(p);
    if (GetFileAttributesW(lp.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    const DWORD e = GetLastError();
    return !(e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND || e == ERROR_INVALID_NAME || e == ERROR_BAD_NETPATH);
}

bool IsDirectory(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(Long(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

ComPtr<IShellItem> Item(const std::wstring& path) {
    ComPtr<IShellItem> it;
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&it)))) return it;
    const std::wstring lp = Long(path);
    if (lp != path && SUCCEEDED(SHCreateItemFromParsingName(lp.c_str(), nullptr, IID_PPV_ARGS(&it)))) return it;
    return nullptr;
}

std::wstring PathOf(IShellItem* item) {
    if (!item) return {};
    PWSTR s = nullptr;
    std::wstring out;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &s)) && s) out = s;
    CoTaskMemFree(s);
    return out;
}

// Records what the shell did to each item. All methods return S_OK: the operation is never steered from here.
class Sink : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IFileOperationProgressSink> {
public:
    explicit Sink(OpResult* r) : res_(r) {}

    STDMETHODIMP StartOperations() override { return S_OK; }
    STDMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
    STDMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP PostRenameItem(DWORD, IShellItem* src, LPCWSTR, HRESULT hr, IShellItem* created) override {
        Record(src, hr, created, false);
        return S_OK;
    }
    STDMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP PostMoveItem(DWORD, IShellItem* src, IShellItem*, LPCWSTR, HRESULT hr, IShellItem* created) override {
        Record(src, hr, created, false);
        return S_OK;
    }
    STDMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP PostCopyItem(DWORD, IShellItem* src, IShellItem*, LPCWSTR, HRESULT hr, IShellItem* created) override {
        Record(src, hr, created, false);
        return S_OK;
    }
    STDMETHODIMP PreDeleteItem(DWORD, IShellItem*) override { return S_OK; }
    STDMETHODIMP PostDeleteItem(DWORD, IShellItem* src, HRESULT hr, IShellItem* created) override {
        Record(src, hr, created, true);
        return S_OK;
    }
    STDMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT hr, IShellItem* created) override {
        if (!res_->items.empty()) {
            res_->items.back().hr = hr;
            res_->items.back().dest = PathOf(created);
        }
        return S_OK;
    }
    STDMETHODIMP UpdateProgress(UINT, UINT) override { return S_OK; }
    STDMETHODIMP ResetTimer() override { return S_OK; }
    STDMETHODIMP PauseTimer() override { return S_OK; }
    STDMETHODIMP ResumeTimer() override { return S_OK; }

private:
    void Record(IShellItem* src, HRESULT hr, IShellItem* created, bool isDelete) {
        const std::wstring p = PathOf(src);
        for (ItemResult& it : res_->items) {
            if (it.skipped || !EqualsNoCase(it.source, p)) continue;
            it.hr = hr;
            if (SUCCEEDED(hr)) {
                if (isDelete) it.recycled = created != nullptr;
                else if (created) it.dest = PathOf(created);
            }
            return;
        }
    }
    OpResult* res_;
};

struct Op {
    ComPtr<IFileOperation> fo;
    ComPtr<Sink> sink;
    OpResult res;
};

// Creates the IFileOperation with the flags every operation shares.
bool Begin(Op* op, const OpOptions& o) {
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op->fo));
    if (FAILED(hr)) {
        op->res.hr = hr;
        return false;
    }
    DWORD flags = FOF_ALLOWUNDO | FOFX_ADDUNDORECORD;
    if (!o.ui) flags |= FOF_SILENT | FOF_NOERRORUI;
    if (o.conflict == Conflict::Replace) flags |= FOF_NOCONFIRMATION;  // never set for a delete (see Recycle)
    op->fo->SetOperationFlags(flags);
    if (o.owner) op->fo->SetOwnerWindow(o.owner);
    op->sink = Microsoft::WRL::Make<Sink>(&op->res);
    return true;
}

void Perform(Op* op) {
    DWORD cookie = 0;
    op->fo->Advise(op->sink.Get(), &cookie);
    HRESULT hr = op->fo->PerformOperations();
    BOOL aborted = FALSE;
    op->fo->GetAnyOperationsAborted(&aborted);
    op->fo->Unadvise(cookie);
    op->res.hr = hr;
    op->res.aborted = aborted || hr == HRESULT_FROM_WIN32(ERROR_CANCELLED);
    for (ItemResult& it : op->res.items) {
        if (it.hr == E_PENDING) it.hr = op->res.aborted ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : (SUCCEEDED(hr) ? S_OK : hr);
    }
}

bool IsUnder(const std::wstring& path, const std::wstring& folder) {
    std::wstring a = Lower(path), b = Lower(folder);
    while (!b.empty() && (b.back() == L'\\' || b.back() == L'/')) b.pop_back();
    return a.size() > b.size() && a.compare(0, b.size(), b) == 0 && (a[b.size()] == L'\\' || a[b.size()] == L'/');
}

OpResult TransferPaths(bool move, const std::vector<std::wstring>& sources, const std::wstring& destDir, const OpOptions& o) {
    Op op;
    if (!Begin(&op, o)) return op.res;
    ComPtr<IShellItem> dest = Item(destDir);
    if (!dest || !IsDirectory(destDir)) {
        op.res.hr = HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
        return op.res;
    }
    bool any = false;
    for (const std::wstring& src : sources) {
        ItemResult ir;
        ir.source = src;
        const std::wstring leaf = LeafOf(src);
        const bool dir = IsDirectory(src);
        const bool sameFolder = EqualsNoCase(ParentOf(src), destDir) || EqualsNoCase(ParentOf(src) + L"\\", destDir);
        if (!PathExists(src)) {
            ir.hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            op.res.items.push_back(ir);
            continue;
        }
        if (dir && (EqualsNoCase(src, destDir) || IsUnder(destDir, src))) {  // a folder cannot go into itself
            ir.hr = HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
            op.res.items.push_back(ir);
            continue;
        }
        if (sameFolder && move) {  // moving into the folder it is already in: nothing to do
            ir.skipped = true;
            ir.dest = src;
            op.res.items.push_back(ir);
            continue;
        }
        const std::wstring target = JoinPath(destDir, leaf);
        std::wstring newName;
        if (PathExists(target)) {
            if (sameFolder) {  // copy next to the original: a numbered name, as Explorer does (never a conflict dialog)
                if (o.conflict == Conflict::Skip) {
                    ir.skipped = true;
                    op.res.items.push_back(ir);
                    continue;
                }
                newName = UniqueName(leaf, dir, [&](const std::wstring& n) { return PathExists(JoinPath(destDir, n)); });
            } else {
                switch (o.conflict) {
                    case Conflict::KeepBoth:
                        newName = UniqueName(leaf, dir, [&](const std::wstring& n) { return PathExists(JoinPath(destDir, n)); });
                        break;
                    case Conflict::Ask:
                        if (o.ui) break;  // the OS conflict dialog decides
                        [[fallthrough]];
                    case Conflict::Skip:
                        ir.skipped = true;
                        ir.dest = target;
                        op.res.items.push_back(ir);
                        continue;
                    case Conflict::Replace:
                        break;
                }
            }
        }
        ComPtr<IShellItem> item = Item(src);
        if (!item) {
            ir.hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            op.res.items.push_back(ir);
            continue;
        }
        ir.hr = E_PENDING;
        ir.dest = newName.empty() ? target : JoinPath(destDir, newName);
        HRESULT hr = move ? op.fo->MoveItem(item.Get(), dest.Get(), newName.empty() ? nullptr : newName.c_str(), op.sink.Get())
                          : op.fo->CopyItem(item.Get(), dest.Get(), newName.empty() ? nullptr : newName.c_str(), op.sink.Get());
        if (FAILED(hr)) ir.hr = hr;
        else any = true;
        op.res.items.push_back(ir);
    }
    if (any) Perform(&op);
    return op.res;
}

}  // namespace

// ---- result ----
bool OpResult::Ok() const {
    if (FAILED(hr) || aborted) return false;
    return Failed() == 0;
}

int OpResult::Failed() const {
    int n = 0;
    for (const ItemResult& it : items)
        if (!it.skipped && FAILED(it.hr)) ++n;
    return n;
}

std::wstring OpResult::Describe() const {
    auto text = [](HRESULT h) {
        wchar_t buf[256] = {};
        const DWORD code = HRESULT_FACILITY(h) == FACILITY_WIN32 ? HRESULT_CODE(h) : static_cast<DWORD>(h);
        DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, buf,
                                 static_cast<DWORD>(std::size(buf)), nullptr);
        std::wstring s(buf, n);
        while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back();
        return s.empty() ? L"The operation failed (0x" + std::to_wstring(static_cast<unsigned>(h)) + L")." : s;
    };
    if (aborted) return L"The operation was cancelled.";
    for (const ItemResult& it : items)
        if (!it.skipped && FAILED(it.hr)) return LeafOf(it.source) + L": " + text(it.hr);
    if (FAILED(hr)) return text(hr);
    return {};
}

// ---- pure helpers ----
std::wstring ValidateFileName(const std::wstring& name) {
    if (name.empty()) return L"The name can't be empty.";
    if (name.size() > 255) return L"The name is too long (255 characters at most).";
    for (wchar_t c : name) {
        if (c < 32 || wcschr(L"\\/:*?\"<>|", c))
            return L"A name can't contain any of these characters: \\ / : * ? \" < > |";
    }
    if (name == L"." || name == L"..") return L"That name is reserved.";
    if (name.back() == L' ' || name.back() == L'.') return L"A name can't end with a space or a period.";
    std::wstring stem = Lower(name.substr(0, name.find(L'.')));
    while (!stem.empty() && stem.back() == L' ') stem.pop_back();
    static const wchar_t* const reserved[] = {L"con", L"prn", L"aux", L"nul"};
    for (const wchar_t* r : reserved)
        if (stem == r) return L"That name is reserved by Windows.";
    if (stem.size() == 4 && (stem.rfind(L"com", 0) == 0 || stem.rfind(L"lpt", 0) == 0) && stem[3] >= L'1' && stem[3] <= L'9')
        return L"That name is reserved by Windows.";
    return {};
}

std::wstring UniqueName(const std::wstring& name, bool isDir, const std::function<bool(const std::wstring&)>& exists) {
    if (!exists(name)) return name;
    std::wstring stem = name, ext;
    if (!isDir) {
        const size_t dot = name.rfind(L'.');
        if (dot != std::wstring::npos && dot > 0) {
            stem = name.substr(0, dot);
            ext = name.substr(dot);
        }
    }
    for (int i = 2; i < 10000; ++i) {
        std::wstring candidate = stem + L" (" + std::to_wstring(i) + L")" + ext;
        if (!exists(candidate)) return candidate;
    }
    return name;
}

std::wstring ParentOf(const std::wstring& path) {
    std::wstring p = path;
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    const size_t slash = p.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    if (slash == 2 && p[1] == L':') return slash + 1 >= p.size() ? std::wstring{} : p.substr(0, 3);  // "C:\x" -> "C:\"
    if (slash == 0) return {};
    return p.substr(0, slash);
}

std::wstring LeafOf(const std::wstring& path) {
    std::wstring p = path;
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    const size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? p : p.substr(slash + 1);
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& leaf) {
    if (dir.empty()) return leaf;
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + leaf;
    return dir + L"\\" + leaf;
}

bool SameVolume(const std::wstring& a, const std::wstring& b) {
    wchar_t va[MAX_PATH] = {}, vb[MAX_PATH] = {};
    if (GetVolumePathNameW(Long(a).c_str(), va, MAX_PATH) && GetVolumePathNameW(Long(b).c_str(), vb, MAX_PATH))
        return EqualsNoCase(va, vb);
    return a.size() >= 2 && b.size() >= 2 && std::towlower(a[0]) == std::towlower(b[0]) && a[1] == L':' && b[1] == L':';
}

DWORD DropEffectFor(const std::wstring& sourcePath, const std::wstring& destDir, bool ctrl, bool shift) {
    if (EqualsNoCase(ParentOf(sourcePath), destDir) || EqualsNoCase(ParentOf(sourcePath) + L"\\", destDir)) return DROPEFFECT_NONE;
    if (EqualsNoCase(sourcePath, destDir) || IsUnder(destDir, sourcePath)) return DROPEFFECT_NONE;
    if (ctrl) return DROPEFFECT_COPY;
    if (shift) return DROPEFFECT_MOVE;
    return SameVolume(sourcePath, destDir) ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
}

// ---- operations ----
OpResult Copy(const std::vector<std::wstring>& sources, const std::wstring& destDir, const OpOptions& o) {
    return TransferPaths(false, sources, destDir, o);
}

OpResult Move(const std::vector<std::wstring>& sources, const std::wstring& destDir, const OpOptions& o) {
    return TransferPaths(true, sources, destDir, o);
}

OpResult Recycle(const std::vector<std::wstring>& paths, const OpOptions& o) {
    Op op;
    if (!Begin(&op, o)) return op.res;
    // Deleting always means "to the Recycle Bin". FOF_NOCONFIRMATION is never set here: if the shell cannot recycle an item it asks
    // the user whether to delete it permanently, so a permanent delete is always an explicit, OS-shown confirmation.
    DWORD flags = FOF_ALLOWUNDO | FOFX_ADDUNDORECORD | FOFX_RECYCLEONDELETE;
    if (!o.ui) flags |= FOF_SILENT | FOF_NOERRORUI;
    op.fo->SetOperationFlags(flags);
    bool any = false;
    for (const std::wstring& p : paths) {
        ItemResult ir;
        ir.source = p;
        ComPtr<IShellItem> item = PathExists(p) ? Item(p) : nullptr;
        if (!item) {
            ir.hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        } else {
            ir.hr = E_PENDING;
            HRESULT hr = op.fo->DeleteItem(item.Get(), op.sink.Get());
            if (FAILED(hr)) ir.hr = hr;
            else any = true;
        }
        op.res.items.push_back(ir);
    }
    if (any) Perform(&op);
    return op.res;
}

OpResult Rename(const std::wstring& path, const std::wstring& newName, const OpOptions& o) {
    Op op;
    ItemResult ir;
    ir.source = path;
    const std::wstring why = ValidateFileName(newName);
    if (!why.empty()) {
        ir.hr = HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
        op.res.items.push_back(ir);
        op.res.hr = ir.hr;
        return op.res;
    }
    const std::wstring target = JoinPath(ParentOf(path), newName);
    if (!EqualsNoCase(LeafOf(path), newName) && PathExists(target)) {
        ir.hr = HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);  // never an overwrite
        op.res.items.push_back(ir);
        op.res.hr = ir.hr;
        return op.res;
    }
    if (LeafOf(path) == newName) {  // unchanged
        ir.skipped = true;
        ir.dest = path;
        op.res.items.push_back(ir);
        return op.res;
    }
    if (!Begin(&op, o)) return op.res;
    ComPtr<IShellItem> item = PathExists(path) ? Item(path) : nullptr;
    if (!item) {
        ir.hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        op.res.items.push_back(ir);
        return op.res;
    }
    ir.hr = E_PENDING;
    ir.dest = target;
    HRESULT hr = op.fo->RenameItem(item.Get(), newName.c_str(), op.sink.Get());
    op.res.items.push_back(ir);
    if (FAILED(hr)) {
        op.res.items.back().hr = hr;
        return op.res;
    }
    Perform(&op);
    return op.res;
}

OpResult NewFolder(const std::wstring& parent, const std::wstring& name, const OpOptions& o, std::wstring* created) {
    Op op;
    if (created) created->clear();
    if (!Begin(&op, o)) return op.res;
    ComPtr<IShellItem> dest = IsDirectory(parent) ? Item(parent) : nullptr;
    ItemResult ir;
    ir.source = parent;
    if (!dest) {
        op.res.hr = HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
        return op.res;
    }
    const std::wstring finalName = UniqueName(name, true, [&](const std::wstring& n) { return PathExists(JoinPath(parent, n)); });
    ir.hr = E_PENDING;
    ir.dest = JoinPath(parent, finalName);
    op.res.items.push_back(ir);
    HRESULT hr = op.fo->NewItem(dest.Get(), FILE_ATTRIBUTE_DIRECTORY, finalName.c_str(), nullptr, op.sink.Get());
    if (FAILED(hr)) {
        op.res.items.back().hr = hr;
        return op.res;
    }
    Perform(&op);
    if (op.res.Ok() && created) *created = op.res.items.back().dest.empty() ? ir.dest : op.res.items.back().dest;
    return op.res;
}

// ---- clipboard / drag and drop ----
ComPtr<IDataObject> MakeDataObject(const std::vector<std::wstring>& paths) {
    std::vector<PIDLIST_ABSOLUTE> ids;
    for (const std::wstring& p : paths) {
        PIDLIST_ABSOLUTE id = nullptr;
        if (SUCCEEDED(SHParseDisplayName(p.c_str(), nullptr, &id, 0, nullptr)) && id) ids.push_back(id);
        else if (SUCCEEDED(SHParseDisplayName(Long(p).c_str(), nullptr, &id, 0, nullptr)) && id) ids.push_back(id);
    }
    ComPtr<IDataObject> data;
    if (!ids.empty()) {
        ComPtr<IShellItemArray> arr;
        if (SUCCEEDED(SHCreateShellItemArrayFromIDLists(static_cast<UINT>(ids.size()), const_cast<LPCITEMIDLIST*>(reinterpret_cast<const LPCITEMIDLIST*>(ids.data())), &arr)))
            arr->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data));
    }
    for (PIDLIST_ABSOLUTE id : ids) CoTaskMemFree(id);
    return data;
}

std::vector<std::wstring> PathsFromDataObject(IDataObject* data) {
    std::vector<std::wstring> out;
    if (!data) return out;
    ComPtr<IShellItemArray> arr;
    if (FAILED(SHCreateShellItemArrayFromDataObject(data, IID_PPV_ARGS(&arr)))) return out;
    DWORD n = 0;
    arr->GetCount(&n);
    for (DWORD i = 0; i < n; ++i) {
        ComPtr<IShellItem> it;
        if (SUCCEEDED(arr->GetItemAt(i, &it))) {
            std::wstring p = PathOf(it.Get());
            if (!p.empty()) out.push_back(p);
        }
    }
    return out;
}

bool SetPreferredDropEffect(IDataObject* data, DWORD effect) {
    if (!data) return false;
    FORMATETC fmt{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!h) return false;
    if (auto* p = static_cast<DWORD*>(GlobalLock(h))) {
        *p = effect;
        GlobalUnlock(h);
    }
    STGMEDIUM sm{};
    sm.tymed = TYMED_HGLOBAL;
    sm.hGlobal = h;
    if (FAILED(data->SetData(&fmt, &sm, TRUE))) {
        GlobalFree(h);
        return false;
    }
    return true;
}

DWORD GetPreferredDropEffect(IDataObject* data) {
    if (!data) return 0;
    FORMATETC fmt{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM sm{};
    if (FAILED(data->GetData(&fmt, &sm))) return 0;
    DWORD effect = 0;
    if (sm.tymed == TYMED_HGLOBAL && sm.hGlobal) {
        if (auto* p = static_cast<DWORD*>(GlobalLock(sm.hGlobal))) {
            effect = *p;
            GlobalUnlock(sm.hGlobal);
        }
    }
    ReleaseStgMedium(&sm);
    return effect;
}

OpResult Transfer(IDataObject* data, const std::wstring& destDir, bool move, const OpOptions& o) {
    std::vector<std::wstring> paths = PathsFromDataObject(data);
    if (paths.empty()) {
        OpResult r;
        r.hr = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        return r;
    }
    return TransferPaths(move, paths, destDir, o);
}

// ---- integrity ----
std::wstring Sha256Hex(const std::wstring& path) {
    HANDLE f = CreateFileW(Long(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::wstring out;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 && BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
        std::vector<UCHAR> buf(1 << 20);
        bool ok = true;
        for (;;) {
            DWORD n = 0;
            if (!ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr)) {
                ok = false;
                break;
            }
            if (n == 0) break;
            if (BCryptHashData(hash, buf.data(), n, 0) < 0) {
                ok = false;
                break;
            }
        }
        UCHAR digest[32] = {};
        if (ok && BCryptFinishHash(hash, digest, sizeof digest, 0) >= 0) {
            static const wchar_t* hex = L"0123456789abcdef";
            for (UCHAR b : digest) {
                out += hex[b >> 4];
                out += hex[b & 15];
            }
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return out;
}

}  // namespace velora::files
