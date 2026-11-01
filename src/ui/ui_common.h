// Shared helpers for the shell UI modules (menu bar, Dock, apps grid). Owner: B.
// Theme, DirectWrite text, DPI math and an asynchronous icon store.
#pragma once

#include <d2d1_3.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <memory>
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
// Cached text format (Segoe UI Variable, falls back to Segoe UI), sizes in DIPs.
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
};

// Asynchronous icon loading (one worker thread, started on the first Request). The pixels
// of an icon live here once; each window turns them into its own D2D bitmap.
class IconStore {
public:
    static IconStore& Get();
    void SetNotifyWindow(HWND hwnd);
    // Returns null until the icon is loaded. `source` is a shell parsing name
    // ("C:\\x.exe" or "shell:AppsFolder\\<aumid>").
    std::shared_ptr<const Pixels> Find(const std::wstring& source);
    void Request(const std::wstring& source);
    // Main thread: runs the registered listeners after kMsgIconsLoaded.
    void Dispatch();
    int AddListener(std::function<void()> fn);
    void RemoveListener(int token);
    void Shutdown();

private:
    IconStore();
    ~IconStore();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Turns loaded pixels into a bitmap on `dc`; null on failure.
Microsoft::WRL::ComPtr<ID2D1Bitmap> MakeBitmap(ID2D1DeviceContext* dc, const Pixels& p);

}  // namespace velora::ui
