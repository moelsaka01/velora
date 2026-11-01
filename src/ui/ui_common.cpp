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

std::shared_ptr<Pixels> LoadIcon(const std::wstring& source) {
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(source.c_str(), nullptr, IID_PPV_ARGS(&item)))) return nullptr;
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(item.As(&factory))) return nullptr;
    HBITMAP bmp = nullptr;
    if (FAILED(factory->GetImage({kIconPx, kIconPx}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bmp)) || !bmp) return nullptr;
    BITMAP info{};
    GetObjectW(bmp, sizeof info, &info);
    auto px = std::make_shared<Pixels>();
    px->width = info.bmWidth;
    px->height = info.bmHeight;
    px->bgra.resize(static_cast<size_t>(px->width) * px->height * 4);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = px->width;
    bi.bmiHeader.biHeight = -px->height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(nullptr);
    int got = GetDIBits(dc, bmp, 0, px->height, px->bgra.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    DeleteObject(bmp);
    if (!got) return nullptr;
    return px;
}

}  // namespace

struct IconStore::Impl {
    std::mutex mutex;
    std::condition_variable cv;
    std::map<std::wstring, std::shared_ptr<const Pixels>> done;
    std::set<std::wstring> requested;
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
            std::shared_ptr<Pixels> px = LoadIcon(src);
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
    if (impl_->stop || !impl_->requested.insert(source).second) return;
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
