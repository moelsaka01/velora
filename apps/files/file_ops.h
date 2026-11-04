// File operations of Velora Files (CLAUDE.md sec. 5 files G1): copy / move / rename / new folder / delete-to-Recycle-Bin,
// all through IFileOperation, plus the shell data objects used for the clipboard and drag and drop. Owner: C.
//
// Safety rules (sec. 11, item 3):
//  * Delete only ever recycles (FOF_ALLOWUNDO + FOFX_RECYCLEONDELETE). There is no permanent-delete entry point, and
//    FOF_NOCONFIRMATION is never set on a delete, so when the shell cannot recycle an item it asks the user itself.
//  * Name conflicts are resolved by an explicit policy. `Ask` lets the OS show its own conflict dialog; without UI
//    (tests, automation) `Ask` degrades to `Skip`, never to overwriting. `Replace` is only used when the caller chose it.
//  * Permissions, elevation prompts and locked files are the OS's business: the per-item HRESULTs are reported.
//  * Sources are never touched before the OS reports success for that item (the shell moves/copies; we only pre-check).
#pragma once

#include "core/win.h"

#include <objidl.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace velora::files {

enum class Conflict {
    Ask,       // OS conflict dialog (UI) / skip (no UI)
    KeepBoth,  // destination gets "name (2).ext"
    Skip,      // existing destinations are left alone
    Replace,   // overwrite without asking (only when the caller was told to)
};

struct OpOptions {
    Conflict conflict = Conflict::Ask;
    bool ui = true;        // false: no OS dialogs (tests, automation)
    HWND owner = nullptr;  // parent of the OS dialogs
};

struct ItemResult {
    std::wstring source;
    std::wstring dest;       // final path when known
    HRESULT hr = S_OK;
    bool skipped = false;    // left alone on purpose (conflict policy, same folder)
    bool recycled = false;   // delete: the item went to the Recycle Bin
};

struct OpResult {
    HRESULT hr = S_OK;       // PerformOperations / setup
    bool aborted = false;    // the user cancelled in the OS dialog
    std::vector<ItemResult> items;

    bool Ok() const;
    int Failed() const;
    // A user-readable sentence for the first failure ("" when everything worked or was skipped).
    std::wstring Describe() const;
};

// ---- pure helpers (unit-tested) ----
// "" when `name` is usable as a single file name, else the reason.
std::wstring ValidateFileName(const std::wstring& name);
// "name (2).ext", "name (3).ext" ... : the first candidate for which `exists` is false. `name` itself when free.
std::wstring UniqueName(const std::wstring& name, bool isDir, const std::function<bool(const std::wstring&)>& exists);
std::wstring ParentOf(const std::wstring& path);   // "C:\a\b" -> "C:\a"; "C:\" -> ""
std::wstring LeafOf(const std::wstring& path);     // "C:\a\b" -> "b"
std::wstring JoinPath(const std::wstring& dir, const std::wstring& leaf);
bool SameVolume(const std::wstring& a, const std::wstring& b);
// DROPEFFECT for a drop of `sourcePath` into `destDir`: move on the same volume, copy otherwise; Ctrl forces copy, Shift forces
// move; 0 (none) for a drop into the source's own folder or into itself.
DWORD DropEffectFor(const std::wstring& sourcePath, const std::wstring& destDir, bool ctrl, bool shift);

// ---- operations ----
OpResult Copy(const std::vector<std::wstring>& sources, const std::wstring& destDir, const OpOptions& o);
OpResult Move(const std::vector<std::wstring>& sources, const std::wstring& destDir, const OpOptions& o);
OpResult Recycle(const std::vector<std::wstring>& paths, const OpOptions& o);
// `newName` is a leaf name; a sibling with that name (other than a case-only change) is an error, never an overwrite.
OpResult Rename(const std::wstring& path, const std::wstring& newName, const OpOptions& o);
// Creates "<name>" (made unique) in `parent`; `created` receives the final path.
OpResult NewFolder(const std::wstring& parent, const std::wstring& name, const OpOptions& o, std::wstring* created);

// ---- clipboard / drag and drop ----
// A shell data object for the paths (file group descriptor, IDList array, HDROP) - what Explorer puts on the clipboard.
Microsoft::WRL::ComPtr<IDataObject> MakeDataObject(const std::vector<std::wstring>& paths);
std::vector<std::wstring> PathsFromDataObject(IDataObject* data);
// Adds CFSTR_PREFERREDDROPEFFECT (DROPEFFECT_COPY / DROPEFFECT_MOVE) so a paste knows whether it was a cut.
bool SetPreferredDropEffect(IDataObject* data, DWORD effect);
DWORD GetPreferredDropEffect(IDataObject* data);  // 0 when absent
// Copy or move the items of a data object (paste, drop) into `destDir`.
OpResult Transfer(IDataObject* data, const std::wstring& destDir, bool move, const OpOptions& o);

// ---- integrity ----
// SHA-256 of a file as lower-case hex; "" on failure.
std::wstring Sha256Hex(const std::wstring& path);

}  // namespace velora::files
