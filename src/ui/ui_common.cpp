#include "ui/ui_common.h"

#include <commctrl.h>
#include <commoncontrols.h>
#include <dwrite_3.h>
#include <shellapi.h>
#include <psapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "core/log.h"
#include "core/text.h"
#include "ui/icon_tile.h"
#include "ui/resource_ids.h"

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

namespace {

// Inter (SIL OFL 1.1, assets/fonts) is linked into the executable as an RCDATA resource (src/shell.rc) and exposed to
// DirectWrite as an in-memory font collection: nothing is installed on the machine. Without the resource (tests, other
// executables) or without DirectWrite 1.3 the collection stays null and Format() uses Segoe UI.
struct BundledFont {
    ComPtr<IDWriteFontCollection> collection;
    std::wstring family;
};

const BundledFont& Bundled() {
    static const BundledFont font = [] {
        BundledFont r;
        HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_INTER_FONT), RT_RCDATA);
        HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
        const void* data = mem ? LockResource(mem) : nullptr;
        DWORD size = res ? SizeofResource(nullptr, res) : 0;
        if (!data || !size || !DWrite()) return r;
        ComPtr<IDWriteFactory5> f5;
        if (FAILED(DWrite()->QueryInterface(IID_PPV_ARGS(&f5)))) return r;
        ComPtr<IDWriteInMemoryFontFileLoader> loader;
        if (FAILED(f5->CreateInMemoryFontFileLoader(&loader)) || FAILED(f5->RegisterFontFileLoader(loader.Get()))) return r;
        ComPtr<IDWriteFontFile> file;
        ComPtr<IDWriteFontSetBuilder1> builder;
        ComPtr<IDWriteFontSet> set;
        ComPtr<IDWriteFontCollection1> coll;
        if (FAILED(loader->CreateInMemoryFontFileReference(f5.Get(), data, size, nullptr, &file)) ||
            FAILED(f5->CreateFontSetBuilder(&builder)) || FAILED(builder->AddFontFile(file.Get())) ||
            FAILED(builder->CreateFontSet(&set)) || FAILED(f5->CreateFontCollectionFromFontSet(set.Get(), &coll)) ||
            coll->GetFontFamilyCount() == 0) {
            log::Warn("ui: the bundled font could not be loaded; using Segoe UI");
            return r;
        }
        ComPtr<IDWriteFontFamily> family;
        ComPtr<IDWriteLocalizedStrings> names;
        wchar_t buf[128] = {};
        if (FAILED(coll->GetFontFamily(0, &family)) || FAILED(family->GetFamilyNames(&names)) || FAILED(names->GetString(0, buf, 128)))
            return r;
        r.family = buf;
        r.collection = coll;
        return r;
    }();
    return font;
}

}  // namespace

const std::wstring& BundledFontFamily() { return Bundled().family; }

IDWriteTextFormat* Format(float sizeDip, DWRITE_FONT_WEIGHT weight) {
    static std::map<std::pair<int, int>, ComPtr<IDWriteTextFormat>> cache;
    auto key = std::make_pair(static_cast<int>(sizeDip * 10), static_cast<int>(weight));
    if (auto it = cache.find(key); it != cache.end()) return it->second.Get();
    IDWriteFactory* f = DWrite();
    if (!f) return nullptr;
    ComPtr<IDWriteTextFormat> fmt;
    const BundledFont& bundled = Bundled();
    if (bundled.collection &&
        FAILED(f->CreateTextFormat(bundled.family.c_str(), bundled.collection.Get(), weight, DWRITE_FONT_STYLE_NORMAL,
                                   DWRITE_FONT_STRETCH_NORMAL, sizeDip, L"", &fmt)))
        fmt.Reset();
    if (!fmt) {
        for (const wchar_t* family : {L"Segoe UI Variable Text", L"Segoe UI"}) {
            if (SUCCEEDED(f->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                              sizeDip, L"", &fmt)))
                break;
        }
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

int IconTierFor(float devicePx) {
    for (int tier : {64, 96, 128, 192, 256})
        if (devicePx <= static_cast<float>(tier) + 0.5f) return tier;
    return 256;
}

namespace {

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

/// Scales premultiplied pixels to exactly px x px with WIC's high-quality cubic filter (it pre-filters when shrinking).
bool ScaleTo(Pixels* p, int px) {
    if (p->width == px && p->height == px) return true;
    if (p->width <= 0 || p->height <= 0 || px <= 0) return false;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmap> src;
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateBitmapFromMemory(static_cast<UINT>(p->width), static_cast<UINT>(p->height), GUID_WICPixelFormat32bppPBGRA,
                                           static_cast<UINT>(p->width) * 4, static_cast<UINT>(p->bgra.size()), p->bgra.data(), &src)) ||
        FAILED(wic->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(src.Get(), static_cast<UINT>(px), static_cast<UINT>(px),
                                 px < p->width ? WICBitmapInterpolationModeFant : WICBitmapInterpolationModeHighQualityCubic)))
        return false;
    Pixels out;
    out.width = out.height = px;
    out.bgra.assign(static_cast<size_t>(px) * px * 4, 0);
    if (FAILED(scaler->CopyPixels(nullptr, static_cast<UINT>(px) * 4, static_cast<UINT>(out.bgra.size()), out.bgra.data()))) return false;
    *p = std::move(out);
    return true;
}

// ---- generic application icon ---------------------------------------------------------
//
// For an executable that carries no icon the shell hands back its generic "blank window" icon as if it were a
// success. It is recognised by comparing 32 x 32 box-filtered thumbnails against the generic icon as the shell
// itself draws it, so a loader can go on to the next source instead of accepting it.

constexpr int kThumb = 32;
using Thumb = std::array<std::uint8_t, kThumb * kThumb * 4>;

Thumb MakeThumb(const Pixels& p) {
    Thumb t{};
    if (p.width <= 0 || p.height <= 0 || p.bgra.size() < static_cast<size_t>(p.width) * p.height * 4) return t;
    for (int dy = 0; dy < kThumb; ++dy) {
        int y0 = dy * p.height / kThumb, y1 = std::max(y0 + 1, (dy + 1) * p.height / kThumb);
        for (int dx = 0; dx < kThumb; ++dx) {
            int x0 = dx * p.width / kThumb, x1 = std::max(x0 + 1, (dx + 1) * p.width / kThumb);
            unsigned sum[4] = {0, 0, 0, 0}, n = 0;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x, ++n)
                    for (int c = 0; c < 4; ++c) sum[c] += p.bgra[(static_cast<size_t>(y) * p.width + x) * 4 + c];
            for (int c = 0; c < 4; ++c) t[(dy * kThumb + dx) * 4 + c] = static_cast<std::uint8_t>(sum[c] / n);
        }
    }
    return t;
}

double ThumbDistance(const Thumb& a, const Thumb& b) {
    unsigned long long d = 0;
    for (size_t i = 0; i < a.size(); ++i) d += static_cast<unsigned>(std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
    return static_cast<double>(d) / static_cast<double>(a.size());
}

const std::vector<Thumb>& GenericReferences() {
    static const std::vector<Thumb> refs = [] {
        std::vector<Thumb> v;
        auto add = [&](HICON h, int px) {
            Pixels pix;
            if (h && HiconToPixels(h, px, &pix)) v.push_back(MakeThumb(pix));
            if (h) DestroyIcon(h);
        };
        // 1. What Explorer shows for an .exe without an icon (the probe name only has to look like an executable).
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW(L"velora-probe.exe", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof sfi, SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES)) {
            for (int list : {SHIL_JUMBO, SHIL_EXTRALARGE}) {
                ComPtr<IImageList> il;
                HICON h = nullptr;
                if (SUCCEEDED(SHGetImageList(list, IID_PPV_ARGS(&il))) && SUCCEEDED(il->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &h)))
                    add(h, list == SHIL_JUMBO ? 256 : 48);
            }
        }
        // The generic *document* icon: what the shell shows for a shortcut whose icon file is missing.
        SHFILEINFOW doc{};
        if (SHGetFileInfoW(L"velora-probe.velora-unknown", FILE_ATTRIBUTE_NORMAL, &doc, sizeof doc, SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES)) {
            ComPtr<IImageList> il;
            HICON h = nullptr;
            if (SUCCEEDED(SHGetImageList(SHIL_JUMBO, IID_PPV_ARGS(&il))) && SUCCEEDED(il->GetIcon(doc.iIcon, ILD_TRANSPARENT, &h))) add(h, 256);
        }
        // 2. The standard application icon of user32.
        add(static_cast<HICON>(CopyImage(LoadIconW(nullptr, IDI_APPLICATION), IMAGE_ICON, 0, 0, 0)), 48);
        // 3. shell32's generic application icon.
        HICON big = nullptr;
        if (SUCCEEDED(SHDefExtractIconW(L"shell32.dll", 2, 0, &big, nullptr, MAKELONG(256, 0)))) add(big, 256);
        return v;
    }();
    return refs;
}

constexpr double kGenericThreshold = 10.0;  // mean per-byte difference of the thumbnails (0..255)

}  // namespace

double GenericIconDistance(const Pixels& p) {
    const auto& refs = GenericReferences();
    if (refs.empty()) return -1;
    Thumb t = MakeThumb(p);
    double best = 1e9;
    for (const auto& r : refs) best = std::min(best, ThumbDistance(t, r));
    return best;
}

bool IsGenericAppIcon(const Pixels& p) {
    double d = GenericIconDistance(p);
    return d >= 0 && d <= kGenericThreshold;
}

namespace {

constexpr int kJumboPx = 256;  // SHIL_JUMBO edge: icons are fetched at this size and scaled down to the tier

enum class Got { Miss, Generic, Ok };

// A candidate counts when it has content and is not the generic icon; it is then brought to exactly tier x tier.
Got Judge(Pixels px, int tier, Pixels* out) {
    if (!HasContent(px)) return Got::Miss;
    if (IsGenericAppIcon(px)) return Got::Generic;
    if (!ScaleTo(&px, tier)) return Got::Miss;
    *out = std::move(px);
    return Got::Ok;
}

// Step: shell image factory in icon mode (what Explorer shows), at the needed size.
Got FromShellFactory(const std::wstring& parsingName, int tier, Pixels* out) {
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(parsingName.c_str(), nullptr, IID_PPV_ARGS(&item)))) return Got::Miss;
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(item.As(&factory))) return Got::Miss;
    HBITMAP bmp = nullptr;
    // Always ask for the largest size (the jumbo 256 px list): an icon whose best native size is <= 48 px is then enlarged
    // once by the shell, and everything smaller than that is a Fant downscale of it, which stays sharper than letting the
    // shell upscale straight to a small tier.
    const int fetch = std::max(tier, kJumboPx);
    if (FAILED(factory->GetImage({fetch, fetch}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bmp)) || !bmp) return Got::Miss;
    Pixels px;
    bool have = HbitmapToPixels(bmp, &px);
    DeleteObject(bmp);
    return have ? Judge(std::move(px), tier, out) : Got::Miss;
}

// Step: IExtractIcon (through SHDefExtractIcon) on a file + icon index, at the needed size.
Got FromExtractIcon(const std::wstring& file, int index, int tier, Pixels* out) {
    if (file.empty()) return Got::Miss;
    std::wstring path(MAX_PATH * 2, L'\0');
    DWORD n = ExpandEnvironmentStringsW(file.c_str(), path.data(), static_cast<DWORD>(path.size()));
    if (n == 0 || n > path.size()) return Got::Miss;
    path.resize(n - 1);
    HICON large = nullptr;
    if (FAILED(SHDefExtractIconW(path.c_str(), index, 0, &large, nullptr, MAKELONG(std::max(tier, kJumboPx), 0))) || !large) return Got::Miss;
    Pixels px;
    bool have = HiconToPixels(large, std::max(tier, kJumboPx), &px);
    DestroyIcon(large);
    return have ? Judge(std::move(px), tier, out) : Got::Miss;
}

bool IsLink(const std::wstring& s) { return s.size() > 4 && Lower(s.substr(s.size() - 4)) == L".lnk"; }

}  // namespace

// Chain (first success wins; `how` names the step). A step whose icon is the generic application icon is a miss:
//   1. IShellItemImageFactory (icon only) on the source itself (.lnk, exe or AppsFolder item)
//   2. IExtractIcon from the shortcut's own icon location
//   3. the same two steps on the shortcut's / AppsFolder item's target executable
// Failure means the caller draws the letter tile, which is the last resort.
bool LoadIconPixels(const std::wstring& source, Pixels* out, std::string* how, bool skipShellFactory, int tier,
                    std::string* trace) {
    auto tried = [&](const char* step, Got g) {
        if (trace) {
            if (!trace->empty()) *trace += ", ";
            *trace += step;
            *trace += g == Got::Ok ? ":ok" : g == Got::Generic ? ":generic" : ":miss";
        }
        if (g == Got::Ok && how) *how = step;
        return g == Got::Ok;
    };
    if (source.empty()) return false;
    tier = std::clamp(tier, 16, 512);
    if (!skipShellFactory && tried("shell-item-image", FromShellFactory(source, tier, out))) return true;

    std::wstring target;
    if (IsLink(source)) {
        ComPtr<IShellLinkW> link;
        ComPtr<IPersistFile> file;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) &&
            SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Load(source.c_str(), STGM_READ))) {
            wchar_t loc[MAX_PATH * 2] = {};
            int idx = 0;
            if (SUCCEEDED(link->GetIconLocation(loc, static_cast<int>(std::size(loc)), &idx)) && loc[0] &&
                tried("link-icon-location", FromExtractIcon(loc, idx, tier, out)))
                return true;
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
        return tried("exe-icon", FromExtractIcon(source, 0, tier, out));
    }
    if (!target.empty()) {
        if (!skipShellFactory && tried("target-shell-item-image", FromShellFactory(target, tier, out))) return true;
        if (tried("target-exe-icon", FromExtractIcon(target, 0, tier, out))) return true;
    }
    return false;
}

bool LoadVeloraIcon(int px, Pixels* out) {
    if (px <= 0 || px > 1024) return false;
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_VELORA_ICON), RT_RCDATA);
    if (!res) return false;
    HGLOBAL mem = LoadResource(nullptr, res);
    const void* data = mem ? LockResource(mem) : nullptr;
    DWORD size = SizeofResource(nullptr, res);
    if (!data || !size) return false;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)), size)) ||
        FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)))
        return false;
    // Premultiply at the source size first, then scale: scaling straight alpha bleeds dark edges.
    if (FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) ||
        FAILED(wic->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(conv.Get(), static_cast<UINT>(px), static_cast<UINT>(px), WICBitmapInterpolationModeHighQualityCubic)))
        return false;
    out->width = out->height = px;
    out->bgra.assign(static_cast<size_t>(px) * px * 4, 0);
    return SUCCEEDED(scaler->CopyPixels(nullptr, static_cast<UINT>(px) * 4, static_cast<UINT>(out->bgra.size()), out->bgra.data()));
}

struct IconStore::Impl {
    std::mutex mutex;
    std::condition_variable cv;
    using Key = std::pair<std::wstring, int>;  // source, tier
    std::map<Key, std::shared_ptr<const Pixels>> done;
    std::set<Key> requested;
    std::map<Key, std::pair<int, ULONGLONG>> failures;  // attempts, tick of the last one
    std::deque<Key> queue;
    std::thread worker;
    bool stop = false;
    HWND notify = nullptr;
    std::map<int, std::function<void()>> listeners;
    int nextToken = 0;

    void Run() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        for (;;) {
            Key key;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] { return stop || !queue.empty(); });
                if (stop) break;
                key = std::move(queue.front());
                queue.pop_front();
            }
            auto px = std::make_shared<Pixels>();
            std::string how, trace;
            const std::string name = WideToUtf8(key.first) + " @" + std::to_string(key.second);
            if (LoadIconPixels(key.first, px.get(), &how, false, key.second, &trace)) {
                // macOS 27 look: anything that is not already a filled rounded square goes on a neutral tile.
                if (EnsureTile(px.get())) how += " +tile";
                log::Info("icons: " + name + " via " + how);
            } else {
                log::Warn("icons: no icon found for " + name + " (" + trace + "); drawing the letter tile");
                px = nullptr;
                std::lock_guard lock(mutex);
                auto& f = failures[key];
                f = {f.first + 1, GetTickCount64()};
            }
            HWND n = nullptr;
            {
                std::lock_guard lock(mutex);
                if (px) done[key] = px;
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

std::shared_ptr<const Pixels> IconStore::Find(const std::wstring& source, int tier) {
    std::lock_guard lock(impl_->mutex);
    auto it = impl_->done.find({source, tier});
    return it == impl_->done.end() ? nullptr : it->second;
}

void IconStore::Request(const std::wstring& source, int tier) {
    if (source.empty()) return;
    const Impl::Key key{source, tier};
    std::lock_guard lock(impl_->mutex);
    if (impl_->stop) return;
    if (!impl_->requested.insert(key).second) {
        // A failed load is retried (the shell may not have been ready), at most 3 times, 5 s apart.
        auto f = impl_->failures.find(key);
        if (f == impl_->failures.end() || f->second.first >= 3 || GetTickCount64() - f->second.second < 5000 ||
            impl_->done.count(key))
            return;
        if (std::find(impl_->queue.begin(), impl_->queue.end(), key) != impl_->queue.end()) return;
    }
    impl_->queue.push_back(key);
    if (!impl_->worker.joinable()) impl_->worker = std::thread([this] { impl_->Run(); });
    impl_->cv.notify_one();
}

bool IconStore::Failed(const std::wstring& source, int tier) {
    std::lock_guard lock(impl_->mutex);
    const Impl::Key key{source, tier};
    auto f = impl_->failures.find(key);
    return f != impl_->failures.end() && !impl_->done.count(key);
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

size_t IconStore::Trim(const std::set<std::wstring>& keep) {
    size_t bytes = 0;
    std::lock_guard lock(impl_->mutex);
    for (auto it = impl_->done.begin(); it != impl_->done.end();) {
        if (keep.count(it->first.first)) { ++it; continue; }
        bytes += it->second->bgra.size();
        impl_->requested.erase(it->first);
        it = impl_->done.erase(it);
    }
    for (auto it = impl_->failures.begin(); it != impl_->failures.end();)
        it = keep.count(it->first.first) ? std::next(it) : impl_->failures.erase(it);
    return bytes;
}

void TrimProcessMemory(const char* reason) {
    auto snap = [] {
        PROCESS_MEMORY_COUNTERS_EX m{sizeof m};
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof m);
        return m;
    };
    const auto before = snap();
    HeapCompact(GetProcessHeap(), 0);
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
    const auto after = snap();
    log::Info(std::string("memory trim (") + reason + "): private " + std::to_string(before.PrivateUsage >> 20) + " -> " +
              std::to_string(after.PrivateUsage >> 20) + " MB, working set " + std::to_string(before.WorkingSetSize >> 20) + " -> " +
              std::to_string(after.WorkingSetSize >> 20) + " MB");
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

void DrawFallbackTile(ID2D1DeviceContext* dc, const D2D1_RECT_F& rect, const std::wstring& name, float opacity) {
    if (!dc) return;
    // A stable hue from the name (FNV-1a), moderate saturation: tiles differ from each other and stay quiet.
    std::uint32_t h = 2166136261u;
    for (wchar_t c : Lower(name)) h = (h ^ static_cast<std::uint32_t>(c)) * 16777619u;
    const float hue = static_cast<float>(h % 360) / 60.0f, s = 0.50f, l = 0.42f;
    const float c = (1 - std::fabs(2 * l - 1)) * s, x = c * (1 - std::fabs(std::fmod(hue, 2.0f) - 1)), m = l - c / 2;
    float r = 0, g = 0, b = 0;
    switch (static_cast<int>(hue)) {
        case 0: r = c; g = x; break;
        case 1: r = x; g = c; break;
        case 2: g = c; b = x; break;
        case 3: g = x; b = c; break;
        case 4: r = x; b = c; break;
        default: r = c; b = x; break;
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(r + m, g + m, b + m, opacity), &brush))) return;
    const float size = rect.right - rect.left, radius = size * 0.227f;
    // Last resort, but still on the same neutral tile as every other icon; the name's hue colours the letter.
    {
        const D2D1_GRADIENT_STOP stops[2] = {{0.0f, D2D1::ColorF(0.996f, 0.996f, 1.0f, opacity)},
                                             {1.0f, D2D1::ColorF(0.835f, 0.840f, 0.862f, opacity)}};
        ComPtr<ID2D1GradientStopCollection> coll;
        ComPtr<ID2D1LinearGradientBrush> grad;
        if (SUCCEEDED(dc->CreateGradientStopCollection(stops, 2, &coll)) &&
            SUCCEEDED(dc->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, rect.top), D2D1::Point2F(0, rect.bottom)), coll.Get(), &grad)))
            dc->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), grad.Get());
        else
            dc->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get());
    }
    std::wstring letter = name.empty() ? L"?" : name.substr(0, 1);
    for (auto& ch : letter) ch = static_cast<wchar_t>(std::towupper(ch));
    brush->SetColor(D2D1::ColorF(r + m, g + m, b + m, opacity));
    DrawLabel(dc, brush.Get(), letter, rect, Format(size * 0.46f, DWRITE_FONT_WEIGHT_SEMI_BOLD), DWRITE_TEXT_ALIGNMENT_CENTER);
}

void DrawIcon(ID2D1DeviceContext* dc, ID2D1Bitmap* bmp, const D2D1_RECT_F& dest, float opacity) {
    if (!dc || !bmp) return;
    dc->DrawBitmap(bmp, &dest, opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
}

}  // namespace velora::ui
