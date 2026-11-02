// Shared helpers for the shell UI modules (menu bar, Dock, apps grid). Owner: B.
// Theme, DirectWrite text, DPI math and an asynchronous icon store.
#pragma once

#include <d2d1_3.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/win.h"
#include "render/tokens.h"

namespace velora::ui {

// Window message posted to the owner window when new icons arrived (the main thread
// then calls IconStore::Dispatch()).
inline constexpr UINT kMsgIconsLoaded = WM_APP + 41;

// SystemUsesLightTheme (taskbar/shell surfaces). Light when the value is missing.
tokens::Theme SystemTheme();
// "Transparency effects" system setting; false => draw opaque surfaces.
bool TransparencyEnabled();

inline float Scale(UINT dpi) { return static_cast<float>(dpi) / 96.0f; }

D2D1_COLOR_F Rgba(tokens::Color c, float alphaMul = 1.0f);

IDWriteFactory* DWrite();
// Family name of the bundled UI font (Inter), loaded from the executable's resources; empty when it is not available.
const std::wstring& BundledFontFamily();
// Cached text format (the bundled Inter; falls back to Segoe UI Variable / Segoe UI), sizes in DIPs.
IDWriteTextFormat* Format(float sizeDip, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);
float MeasureWidth(const std::wstring& text, IDWriteTextFormat* fmt);
// Single-line label inside `rect` (DIPs), vertically centred, trimmed with an ellipsis.
void DrawLabel(ID2D1DeviceContext* dc, ID2D1Brush* brush, const std::wstring& text, const D2D1_RECT_F& rect,
               IDWriteTextFormat* fmt, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);

// FileDescription of an executable ("" when unavailable).
std::wstring FileDescription(const std::wstring& exePath);
std::wstring ProcessImagePath(DWORD pid);
std::wstring Lower(std::wstring s);
std::wstring StemOf(const std::wstring& path);

struct Pixels {
    int width = 0, height = 0;
    std::vector<std::uint8_t> bgra;  // premultiplied, top-down
    bool lowRes = false;             // best native source was <= 48 px (enlarged by the shell); tiled smaller to stay sharp
};

// Icons are decoded at a "tier": the pixel edge of the image kept in memory. A tier is never smaller than the
// device pixels the icon is drawn at (that would be an upscale), and the set is small so the cache stays bounded.
inline constexpr int kIconTierDefault = 96;
int IconTierFor(float devicePx);  // 64, 96, 128, 192 or 256: the smallest tier >= devicePx

// True when `p` is the shell's generic application icon (the blank-window icon Explorer shows for an executable that
// carries no icon of its own), which a loader must treat as "no icon found here" and try the next source. Needs COM
// on the calling thread (the reference images come from the shell). Compared as 32 x 32 thumbnails, so any size works.
bool IsGenericAppIcon(const Pixels& p);
// Smallest mean per-byte difference (0..255) between `p` and the generic references; -1 when none could be built.
// Diagnostics and tests only.
double GenericIconDistance(const Pixels& p);

// Synchronous icon load for a shell parsing name (an exe path, a .lnk or "shell:AppsFolder\\<aumid>") through the
// fallback chain: shell image factory -> shortcut icon location -> target exe. A step that yields only the generic
// application icon counts as a miss and the chain goes on. The result is exactly tier x tier pixels. Needs COM on
// the calling thread. `how` (optional) receives the step that produced the pixels; `trace` (optional) receives
// every step tried and its outcome (ok / generic / miss). False means: draw the letter tile.
// `skipShellFactory` exists for tests: it forces the later steps of the chain.
bool LoadIconPixels(const std::wstring& source, Pixels* out, std::string* how = nullptr, bool skipShellFactory = false,
                    int tier = kIconTierDefault, std::string* trace = nullptr);
// Velora's own app icon, decoded from the embedded PNG resource (src/shell.rc) and scaled with a high-quality
// (Fant) filter to exactly `px` x `px` device pixels. False when the resource is absent (tests, other exes).
// Needs COM on the calling thread.
bool LoadVeloraIcon(int px, Pixels* out);

// Asynchronous icon loading (one worker thread, started on the first Request). The pixels
// of an icon live here once; each window turns them into its own D2D bitmap.
class IconStore {
public:
    static IconStore& Get();
    void SetNotifyWindow(HWND hwnd);
    // Returns null until the icon is loaded. `source` is a shell parsing name
    // ("C:\\x.exe" or "shell:AppsFolder\\<aumid>").
    // Pixels are kept per (source, tier); a tier comes from IconTierFor().
    std::shared_ptr<const Pixels> Find(const std::wstring& source, int tier = kIconTierDefault);
    void Request(const std::wstring& source, int tier = kIconTierDefault);
    // True once every attempt at this icon failed (the caller then draws DrawFallbackTile instead of a placeholder).
    bool Failed(const std::wstring& source, int tier = kIconTierDefault);
    // Main thread: runs the registered listeners after kMsgIconsLoaded.
    void Dispatch();
    int AddListener(std::function<void()> fn);
    void RemoveListener(int token);
    // Drops the decoded pixels (and request/failure records) of every source not in `keep`; they are decoded again on the
    // next Request. Returns the pixel bytes released. Main thread.
    size_t Trim(const std::set<std::wstring>& keep);
    void Shutdown();

private:
    IconStore();
    ~IconStore();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Memory give-back after a big transient surface (the Apps panel) closed: compacts the heaps and trims the working set.
// Logs private bytes / working set before and after so the effect is visible in the log.
void TrimProcessMemory(const char* reason);

// Turns loaded pixels into a bitmap on `dc`; null on failure.
Microsoft::WRL::ComPtr<ID2D1Bitmap> MakeBitmap(ID2D1DeviceContext* dc, const Pixels& p);
// The tile drawn when no icon could be found anywhere: a rounded square (corner radius 22.7 % of its size, the Dock
// icon radius of docs/MACOS_UI_SPEC.md) on the same neutral gradient as other icon tiles, with the first letter in a colour
// taken from the name.
void DrawFallbackTile(ID2D1DeviceContext* dc, const D2D1_RECT_F& rect, const std::wstring& name, float opacity = 1.0f);
// Draws an icon bitmap into `dest` (DIPs) with the high-quality cubic filter: when the bitmap is larger than the
// destination in device pixels, Direct2D pre-filters before it samples, so downscaled icons stay sharp.
void DrawIcon(ID2D1DeviceContext* dc, ID2D1Bitmap* bmp, const D2D1_RECT_F& dest, float opacity = 1.0f);

}  // namespace velora::ui
