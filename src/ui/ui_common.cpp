#include "ui/ui_common.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "core/log.h"
#include "core/text.h"

#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "version.lib")

namespace velora::ui {

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kPersonalize[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

DWORD RegDword(const wchar_t* subKey, const wchar_t* name, DWORD def) {
    DWORD v = def, size = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, subKey, name, RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS) return def;
    return v;
}

}  // namespace

tokens::Theme SystemTheme() {
    return RegDword(kPersonalize, L"SystemUsesLightTheme", 1) ? tokens::Theme::Light : tokens::Theme::Dark;
}

bool TransparencyEnabled() { return RegDword(kPersonalize, L"EnableTransparency", 1) != 0; }

D2D1_COLOR_F Rgba(tokens::Color c, float alphaMul) {
    float a = ((c.argb >> 24) & 0xFF) / 255.0f * alphaMul;
    return D2D1::ColorF(((c.argb >> 16) & 0xFF) / 255.0f, ((c.argb >> 8) & 0xFF) / 255.0f, (c.argb & 0xFF) / 255.0f, a);
}

IDWriteFactory* DWrite() {
    static ComPtr<IDWriteFactory> factory = [] {
        ComPtr<IDWriteFactory> f;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(f.GetAddressOf()));
        return f;
    }();
    return factory.Get();
}

IDWriteTextFormat* Format(float sizeDip, DWRITE_FONT_WEIGHT weight) {
    static std::map<std::pair<int, int>, ComPtr<IDWriteTextFormat>> cache;
    auto key = std::make_pair(static_cast<int>(sizeDip * 10), static_cast<int>(weight));
    if (auto it = cache.find(key); it != cache.end()) return it->second.Get();
    IDWriteFactory* f = DWrite();
    if (!f) return nullptr;
    ComPtr<IDWriteTextFormat> fmt;
    for (const wchar_t* family : {L"Segoe UI Variable Text", L"Segoe UI"}) {
        if (SUCCEEDED(f->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          sizeDip, L"", &fmt)))
            break;
    }
    if (!fmt) return nullptr;
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    ComPtr<IDWriteInlineObject> ellipsis;
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    if (SUCCEEDED(f->CreateEllipsisTrimmingSign(fmt.Get(), &ellipsis))) fmt->SetTrimming(&trim, ellipsis.Get());
    return cache.emplace(key, std::move(fmt)).first->second.Get();
}

float MeasureWidth(const std::wstring& text, IDWriteTextFormat* fmt) {
    if (text.empty() || !fmt || !DWrite()) return 0;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(DWrite()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), fmt, 4096, 64, &layout))) return 0;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

void DrawLabel(ID2D1DeviceContext* dc, ID2D1Brush* brush, const std::wstring& text, const D2D1_RECT_F& rect,
               IDWriteTextFormat* fmt, DWRITE_TEXT_ALIGNMENT align) {
    if (!dc || !brush || !fmt || text.empty()) return;
    fmt->SetTextAlignment(align);
    dc->DrawText(text.c_str(), static_cast<UINT32>(text.size()), fmt, rect, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

std::wstring StemOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos || dot == 0 ? name : name.substr(0, dot);
}

std::wstring ProcessImagePath(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    wchar_t buf[MAX_PATH * 2];
    DWORD n = static_cast<DWORD>(std::size(buf));
    std::wstring out;
    if (QueryFullProcessImageNameW(h, 0, buf, &n)) out.assign(buf, n);
    CloseHandle(h);
    return out;
}

std::wstring FileDescription(const std::wstring& exePath) {
    if (exePath.empty()) return {};
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(exePath.c_str(), &dummy);
    if (!size) return {};
    std::vector<char> data(size);
    if (!GetFileVersionInfoW(exePath.c_str(), 0, size, data.data())) return {};
    struct LangCp {
        WORD lang, cp;
    }* tr = nullptr;
    UINT len = 0;
    std::vector<std::wstring> keys;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&tr), &len) &&
        len >= sizeof(LangCp)) {
        wchar_t k[64];
        swprintf_s(k, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr->lang, tr->cp);
        keys.push_back(k);
    }
    keys.push_back(L"\\StringFileInfo\\040904b0\\FileDescription");
    for (const auto& k : keys) {
        wchar_t* val = nullptr;
        if (VerQueryValueW(data.data(), k.c_str(), reinterpret_cast<void**>(&val), &len) && val && len > 1) return val;
    }
    return {};
}

// ---- icons ---------------------------------------------------------------------------

namespace {

constexpr int kIconPx = 96;

// A usable icon covers at least a few percent of its canvas; an all-transparent bitmap counts as a miss
// so the next step of the chain gets a chance (some shell handlers "succeed" with an empty image).
bool HasContent(const Pixels& p) {
    size_t opaque = 0, total = static_cast<size_t>(p.width) * p.height;
    for (size_t i = 3; i < p.bgra.size(); i += 4)
        if (p.bgra[i] > 16) ++opaque;
    return total > 0 && opaque * 50 >= total;
}

bool HbitmapToPixels(HBITMAP bmp, Pixels* out) {
    BITMAP info{};
    if (!GetObjectW(bmp, sizeof info, &info) || info.bmWidth <= 0 || info.bmHeight <= 0) return false;
    out->width = info.bmWidth;
    out->height = info.bmHeight;
    out->bgra.assign(static_cast<size_t>(out->width) * out->height * 4, 0);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = out->width;
    bi.bmiHeader.biHeight = -out->height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(nullptr);
    int got = GetDIBits(dc, bmp, 0, out->height, out->bgra.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    return got != 0;
}

// Renders an HICON on black and on white; the difference gives the alpha, the black render the
// premultiplied colour. Works for 32-bit alpha icons and for old mask-only icons alike.
bool HiconToPixels(HICON icon, int px, Pixels* out) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = px;
    bi.bmiHeader.biHeight = -px;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    std::vector<std::uint8_t> render[2];
    bool ok = true;
    for (int pass = 0; pass < 2 && ok; ++pass) {
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dib || !bits) {
            ok = false;
            break;
        }
        size_t bytes = static_cast<size_t>(px) * px * 4;
        memset(bits, pass == 0 ? 0x00 : 0xFF, bytes);
        HGDIOBJ old = SelectObject(dc, dib);
        ok = DrawIconEx(dc, 0, 0, icon, px, px, 0, nullptr, DI_NORMAL) != FALSE;
        GdiFlush();
        render[pass].assign(static_cast<std::uint8_t*>(bits), static_cast<std::uint8_t*>(bits) + bytes);
        SelectObject(dc, old);
        DeleteObject(dib);
    }
    DeleteDC(dc);
    if (!ok) return false;
    out->width = out->height = px;
    out->bgra.resize(render[0].size());
    for (size_t i = 0; i < render[0].size(); i += 4) {
        int diff = (render[1][i] - render[0][i] + render[1][i + 1] - render[0][i + 1] + render[1][i + 2] - render[0][i + 2]) / 3;
        int a = std::clamp(255 - diff, 0, 255);
        for (int c = 0; c < 3; ++c) out->bgra[i + c] = static_cast<std::uint8_t>(std::min<int>(render[0][i + c], a));
        out->bgra[i + 3] = static_cast<std::uint8_t>(a);
    }
    return true;
}

// Step: shell image factory in icon mode (what Explorer shows), at the needed size.
bool FromShellFactory(const std::wstring& parsingName, Pixels* out) {
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(parsingName.c_str(), nullptr, IID_PPV_ARGS(&item)))) return false;
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(item.As(&factory))) return false;
    HBITMAP bmp = nullptr;
    if (FAILED(factory->GetImage({kIconPx, kIconPx}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bmp)) || !bmp) return false;
    Pixels px;
    bool ok = HbitmapToPixels(bmp, &px) && HasContent(px);
    DeleteObject(bmp);
    if (ok) *out = std::move(px);
    return ok;
}

// Step: IExtractIcon (through SHDefExtractIcon) on a file + icon index, at the needed size.
bool FromExtractIcon(const std::wstring& file, int index, Pixels* out) {
    if (file.empty()) return false;
    std::wstring path(MAX_PATH * 2, L'\0');
    DWORD n = ExpandEnvironmentStringsW(file.c_str(), path.data(), static_cast<DWORD>(path.size()));
    if (n == 0 || n > path.size()) return false;
    path.resize(n - 1);
    HICON large = nullptr;
    if (FAILED(SHDefExtractIconW(path.c_str(), index, 0, &large, nullptr, MAKELONG(kIconPx, 0))) || !large) return false;
    Pixels px;
    bool ok = HiconToPixels(large, kIconPx, &px) && HasContent(px);
    DestroyIcon(large);
    if (ok) *out = std::move(px);
    return ok;
}

bool IsLink(const std::wstring& s) { return s.size() > 4 && Lower(s.substr(s.size() - 4)) == L".lnk"; }

}  // namespace

// Chain (first success wins; `how` names the step):
//   1. IShellItemImageFactory (icon only) on the source itself (.lnk, exe or AppsFolder item)
//   2. IExtractIcon from the shortcut's own icon location
//   3. the same two steps on the shortcut's / AppsFolder item's target executable
// Failure means the caller draws the letter tile, which is the last resort.
bool LoadIconPixels(const std::wstring& source, Pixels* out, std::string* how, bool skipShellFactory) {
    auto done = [&](const char* step) {
        if (how) *how = step;
        return true;
    };
    if (source.empty()) return false;
    if (!skipShellFactory && FromShellFactory(source, out)) return done("shell-item-image");

    std::wstring target;
    if (IsLink(source)) {
        ComPtr<IShellLinkW> link;
        ComPtr<IPersistFile> file;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) &&
            SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Load(source.c_str(), STGM_READ))) {
            wchar_t loc[MAX_PATH * 2] = {};
            int idx = 0;
            if (SUCCEEDED(link->GetIconLocation(loc, static_cast<int>(std::size(loc)), &idx)) && loc[0] &&
                FromExtractIcon(loc, idx, out))
                return done("link-icon-location");
            wchar_t t[MAX_PATH * 2] = {};
            if (SUCCEEDED(link->GetPath(t, static_cast<int>(std::size(t)), nullptr, SLGP_UNCPRIORITY))) target = t;
        }
    } else if (source.rfind(L"shell:AppsFolder\\", 0) == 0) {
        ComPtr<IShellItem2> item;
        if (SUCCEEDED(SHCreateItemFromParsingName(source.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
            // PKEY_Link_TargetParsingPath
            static const PROPERTYKEY kTarget = {{0xB9B4B3FC, 0x2B51, 0x4A42, {0xB5, 0xD8, 0x32, 0x41, 0x46, 0xAF, 0xCF, 0x25}}, 2};
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetString(kTarget, &p)) && p) target = p;
            CoTaskMemFree(p);
        }
    } else {
        // A plain exe path: its first icon resource.
        if (FromExtractIcon(source, 0, out)) return done("exe-icon");
        return false;
    }
    if (!target.empty()) {
        if (!skipShellFactory && FromShellFactory(target, out)) return done("target-shell-item-image");
        if (FromExtractIcon(target, 0, out)) return done("target-exe-icon");
    }
    return false;
}

struct IconStore::Impl {
    std::mutex mutex;
    std::condition_variable cv;
    std::map<std::wstring, std::shared_ptr<const Pixels>> done;
    std::set<std::wstring> requested;
    std::map<std::wstring, std::pair<int, ULONGLONG>> failures;  // attempts, tick of the last one
    std::deque<std::wstring> queue;
    std::thread worker;
    bool stop = false;
    HWND notify = nullptr;
    std::map<int, std::function<void()>> listeners;
    int nextToken = 0;

    void Run() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        for (;;) {
            std::wstring src;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] { return stop || !queue.empty(); });
                if (stop) break;
                src = std::move(queue.front());
                queue.pop_front();
            }
            auto px = std::make_shared<Pixels>();
            std::string how;
            if (LoadIconPixels(src, px.get(), &how)) {
                log::Info("icons: " + WideToUtf8(src) + " via " + how);
            } else {
                log::Warn("icons: no icon found for " + WideToUtf8(src) + "; drawing the letter tile");
                px = nullptr;
                std::lock_guard lock(mutex);
                auto& f = failures[src];
                f = {f.first + 1, GetTickCount64()};
            }
            HWND n = nullptr;
            {
                std::lock_guard lock(mutex);
                if (px) done[src] = px;
                n = notify;
            }
            if (px && n) PostMessageW(n, kMsgIconsLoaded, 0, 0);
        }
        CoUninitialize();
    }
};

IconStore::IconStore() : impl_(std::make_unique<Impl>()) {}
IconStore::~IconStore() { Shutdown(); }

IconStore& IconStore::Get() {
    static IconStore* store = new IconStore();  // leaked on purpose: the worker must outlive static destruction
    return *store;
}

void IconStore::SetNotifyWindow(HWND hwnd) {
    std::lock_guard lock(impl_->mutex);
    impl_->notify = hwnd;
}

std::shared_ptr<const Pixels> IconStore::Find(const std::wstring& source) {
    std::lock_guard lock(impl_->mutex);
    auto it = impl_->done.find(source);
    return it == impl_->done.end() ? nullptr : it->second;
}

void IconStore::Request(const std::wstring& source) {
    if (source.empty()) return;
    std::lock_guard lock(impl_->mutex);
    if (impl_->stop) return;
    if (!impl_->requested.insert(source).second) {
        // A failed load is retried (the shell may not have been ready), at most 3 times, 5 s apart.
        auto f = impl_->failures.find(source);
        if (f == impl_->failures.end() || f->second.first >= 3 || GetTickCount64() - f->second.second < 5000 ||
            impl_->done.count(source))
            return;
        if (std::find(impl_->queue.begin(), impl_->queue.end(), source) != impl_->queue.end()) return;
    }
    impl_->queue.push_back(source);
    if (!impl_->worker.joinable()) impl_->worker = std::thread([this] { impl_->Run(); });
    impl_->cv.notify_one();
}

int IconStore::AddListener(std::function<void()> fn) {
    int t = ++impl_->nextToken;
    impl_->listeners[t] = std::move(fn);
    return t;
}

void IconStore::RemoveListener(int token) { impl_->listeners.erase(token); }

void IconStore::Dispatch() {
    auto copy = impl_->listeners;
    for (auto& [t, fn] : copy) fn();
}

void IconStore::Shutdown() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stop = true;
        impl_->notify = nullptr;
    }
    impl_->cv.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
}

ComPtr<ID2D1Bitmap> MakeBitmap(ID2D1DeviceContext* dc, const Pixels& p) {
    ComPtr<ID2D1Bitmap> bmp;
    D2D1_BITMAP_PROPERTIES props =
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(dc->CreateBitmap(D2D1::SizeU(p.width, p.height), p.bgra.data(), p.width * 4, props, &bmp))) return nullptr;
    return bmp;
}

}  // namespace velora::ui
