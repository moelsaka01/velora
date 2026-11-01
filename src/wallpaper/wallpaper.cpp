#include "wallpaper/wallpaper.h"

#include <shobjidl_core.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

#include "core/json.h"
#include "core/log.h"
#include "core/runtime.h"
#include "core/text.h"
#include "core/win.h"

namespace velora::wallpaper {

using Microsoft::WRL::ComPtr;

namespace {

struct Rgb {
    float r, g, b;
};

struct Blob {
    float cx, cy, radius, strength;
    Rgb color;
};

float Smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// Cheap integer hash -> [0,1).
float Hash(int x, int y, int salt) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u + static_cast<std::uint32_t>(salt) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFF) / 16777216.0f;
}

std::filesystem::path BackupPath() {
    auto dir = DataDir();
    return dir.empty() ? std::filesystem::path{} : dir / L"wallpaper_backup.json";
}

std::string ReadAll(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool IsOurs(const std::wstring& path) {
    if (path.empty()) return false;
    for (const auto& d : WallpaperDirs()) {
        std::wstring dir = d.wstring();
        if (!dir.empty() && _wcsnicmp(path.c_str(), dir.c_str(), dir.size()) == 0) return true;
    }
    return false;
}

// COM for the calling thread; the watchdog's recovery path may not have initialised it.
struct ComScope {
    HRESULT hr;
    ComScope() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
    bool Usable() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};

std::wstring TakeString(PWSTR p) {
    std::wstring s = p ? p : L"";
    CoTaskMemFree(p);
    return s;
}

struct MonitorInfo {
    std::wstring id;
    RECT rect{};
};

bool Monitors(IDesktopWallpaper* dw, std::vector<MonitorInfo>* out) {
    UINT n = 0;
    if (FAILED(dw->GetMonitorDevicePathCount(&n))) return false;
    for (UINT i = 0; i < n; ++i) {
        PWSTR id = nullptr;
        if (FAILED(dw->GetMonitorDevicePathAt(i, &id))) continue;
        MonitorInfo m;
        m.id = TakeString(id);
        if (FAILED(dw->GetMonitorRECT(m.id.c_str(), &m.rect))) continue;  // disconnected / no rect
        out->push_back(std::move(m));
    }
    return !out->empty();
}

}  // namespace

Image Generate(int width, int height, bool dark, int variant) {
    Image img;
    img.width = std::max(1, width);
    img.height = std::max(1, height);
    img.bgr.resize(static_cast<size_t>(img.width) * img.height * 3);

    const Rgb top = dark ? Rgb{10, 14, 36} : Rgb{226, 232, 250};
    const Rgb bottom = dark ? Rgb{26, 10, 50} : Rgb{242, 226, 248};
    // Three soft colour fields; the variant rotates their positions.
    const float a = 0.9f * static_cast<float>(variant % 4);
    auto rot = [&](float x, float y) {
        float cx = x - 0.5f, cy = y - 0.5f;
        return std::pair{0.5f + cx * std::cos(a) - cy * std::sin(a), 0.5f + cx * std::sin(a) + cy * std::cos(a)};
    };
    auto [x0, y0] = rot(0.20f, 0.30f);
    auto [x1, y1] = rot(0.82f, 0.22f);
    auto [x2, y2] = rot(0.62f, 0.92f);
    const Blob blobs[3] = {
        {x0, y0, 0.62f, 0.60f, dark ? Rgb{60, 92, 225} : Rgb{150, 182, 255}},
        {x1, y1, 0.52f, 0.55f, dark ? Rgb{150, 62, 205} : Rgb{232, 172, 242}},
        {x2, y2, 0.58f, 0.50f, dark ? Rgb{22, 172, 192} : Rgb{160, 232, 236}},
    };
    const float minEdge = static_cast<float>(std::min(img.width, img.height));
    const float aspect = static_cast<float>(img.width) / static_cast<float>(img.height);

    for (int y = 0; y < img.height; ++y) {
        float v = (y + 0.5f) / img.height;
        for (int x = 0; x < img.width; ++x) {
            float u = (x + 0.5f) / img.width;
            Rgb c{top.r + (bottom.r - top.r) * v, top.g + (bottom.g - top.g) * v, top.b + (bottom.b - top.b) * v};
            for (const Blob& b : blobs) {
                float dx = (u - b.cx) * aspect, dy = v - b.cy;
                float d = std::sqrt(dx * dx + dy * dy) / (b.radius * (minEdge > 0 ? 1.0f : 1.0f));
                float f = b.strength * (1.0f - Smooth(d));
                c.r += (b.color.r - c.r) * f;
                c.g += (b.color.g - c.g) * f;
                c.b += (b.color.b - c.b) * f;
            }
            // Triangular dither (+-1 level) hides 8-bit banding in the smooth gradient.
            float n = Hash(x, y, 1) - Hash(x, y, 2);
            auto q = [&](float ch) { return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(ch + n)), 0, 255)); };
            size_t i = (static_cast<size_t>(y) * img.width + x) * 3;
            img.bgr[i + 0] = q(c.b);
            img.bgr[i + 1] = q(c.g);
            img.bgr[i + 2] = q(c.r);
        }
    }
    return img;
}

bool WriteBmp(const std::filesystem::path& path, const Image& img) {
    if (img.width <= 0 || img.height <= 0 || img.bgr.size() != static_cast<size_t>(img.width) * img.height * 3) return false;
    const size_t rowBytes = static_cast<size_t>(img.width) * 3;
    const size_t stride = (rowBytes + 3) & ~size_t{3};
    const std::uint32_t dataSize = static_cast<std::uint32_t>(stride * img.height);
    BITMAPFILEHEADER fh{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + dataSize;
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof ih;
    ih.biWidth = img.width;
    ih.biHeight = img.height;  // bottom-up
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = dataSize;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto tmp = path;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(&fh), sizeof fh);
        f.write(reinterpret_cast<const char*>(&ih), sizeof ih);
        std::vector<std::uint8_t> row(stride, 0);
        for (int y = img.height - 1; y >= 0; --y) {
            std::memcpy(row.data(), &img.bgr[static_cast<size_t>(y) * rowBytes], rowBytes);
            f.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(stride));
        }
        if (!f) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
    return !ec;
}

double AverageLuma(const Image& img) {
    if (img.bgr.empty()) return 0;
    double sum = 0;
    for (size_t i = 0; i + 2 < img.bgr.size(); i += 3) sum += 0.114 * img.bgr[i] + 0.587 * img.bgr[i + 1] + 0.299 * img.bgr[i + 2];
    return sum / (static_cast<double>(img.bgr.size()) / 3.0);
}

// Candidate image directories, best first. The first is the data dir (%LOCALAPPDATA%\Velora\wallpapers); the
// second is the profile root, used when Explorer cannot read the first: %LOCALAPPDATA% is redirected into the
// package's private store when the shell is started from a packaged host (e.g. the Claude desktop app), where
// Explorer cannot see the file (SetWallpaper -> 0x80070002).
std::vector<std::filesystem::path> WallpaperDirs() {
    // Test profile only: the tests point the cache at a temp directory.
    if (CurrentProfile().IsTest()) {
        wchar_t over[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"VELORA_WALLPAPER_DIR", over, MAX_PATH) && over[0]) return {std::filesystem::path(over)};
    }
    std::vector<std::filesystem::path> dirs;
    auto dir = DataDir();
    if (!dir.empty()) dirs.push_back(dir / L"wallpapers");
    wchar_t profile[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) && profile[0])
        dirs.push_back(std::filesystem::path(profile) / L".velora" / L"wallpapers");
    return dirs;
}

std::filesystem::path WallpaperDir() {
    auto d = WallpaperDirs();
    return d.empty() ? std::filesystem::path{} : d.front();
}

bool Enabled() {
    auto dir = DataDir();
    if (dir.empty()) return false;
    auto doc = json::Parse(ReadAll(dir / L"settings.json"));
    const auto* w = doc && doc->IsObject() ? doc->Find("wallpaper") : nullptr;
    const auto* e = w ? w->Find("enabled") : nullptr;
    return e ? e->AsBool(true) : true;
}

int ApplyVelora(bool dark) {
    ComScope com;
    if (!com.Usable()) return 0;
    ComPtr<IDesktopWallpaper> dw;
    if (FAILED(CoCreateInstance(__uuidof(DesktopWallpaper), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dw)))) {
        log::Warn("wallpaper: IDesktopWallpaper unavailable");
        return 0;
    }
    std::vector<MonitorInfo> monitors;
    if (!Monitors(dw.Get(), &monitors)) return 0;

    // Backup first, once: never overwrite the original with one of our own images (e.g. after a crash).
    auto backup = BackupPath();
    if (!backup.empty() && !std::filesystem::exists(backup)) {
        json::Array arr;
        for (const auto& m : monitors) {
            PWSTR cur = nullptr;
            std::wstring path;
            if (SUCCEEDED(dw->GetWallpaper(m.id.c_str(), &cur))) path = TakeString(cur);
            json::Value o;
            o.Set("id", WideToUtf8(m.id));
            o.Set("path", WideToUtf8(IsOurs(path) ? L"" : path));
            arr.push_back(std::move(o));
        }
        DESKTOP_WALLPAPER_POSITION pos = DWPOS_FILL;
        dw->GetPosition(&pos);
        json::Value root;
        root.Set("monitors", std::move(arr));
        root.Set("position", static_cast<int>(pos));
        std::ofstream f(backup, std::ios::binary | std::ios::trunc);
        f << root.Dump();
        if (!f) {
            log::Error("wallpaper: cannot write the backup; leaving the wallpaper alone");
            return 0;
        }
    }

    int changed = 0, index = 0;
    for (const auto& m : monitors) {
        int w = std::min<int>(kMaxEdge, std::max<LONG>(1, m.rect.right - m.rect.left));
        int h = std::min<int>(kMaxEdge, std::max<LONG>(1, m.rect.bottom - m.rect.top));
        // Keep the aspect ratio when the monitor is larger than the cap.
        LONG mw = m.rect.right - m.rect.left, mh = m.rect.bottom - m.rect.top;
        if (mw > kMaxEdge || mh > kMaxEdge) {
            double k = static_cast<double>(kMaxEdge) / std::max(mw, mh);
            w = std::max(1, static_cast<int>(mw * k));
            h = std::max(1, static_cast<int>(mh * k));
        }
        std::wstring name = L"velora-" + std::to_wstring(w) + L"x" + std::to_wstring(h) + (dark ? L"-dark" : L"-light") + L"-v" +
                            std::to_wstring(index % 4) + L"-1.bmp";
        HRESULT hr = E_FAIL;
        bool written = false;
        bool first = true;
        for (const auto& d : WallpaperDirs()) {
            auto file = d / name;
            if (!std::filesystem::exists(file) && !WriteBmp(file, Generate(w, h, dark, index))) {
                log::Warn("wallpaper: cannot write " + WideToUtf8(file.wstring()));
                continue;
            }
            written = true;
            dw->SetPosition(DWPOS_FILL);
            hr = dw->SetWallpaper(m.id.c_str(), file.c_str());
            if (SUCCEEDED(hr)) {
                log::Info("wallpaper: using " + WideToUtf8(file.wstring()) + (first ? "" : " (fallback)"));
                break;
            }
            log::Warn("wallpaper: Explorer rejected " + WideToUtf8(file.wstring()) + "; trying the next directory");
            first = false;
        }
        if (!written) continue;
        if (SUCCEEDED(hr)) ++changed;
        else log::Warn("wallpaper: SetWallpaper failed, hr=0x" + [&] { char b[16]; sprintf_s(b, "%08lX", static_cast<unsigned long>(hr)); return std::string(b); }());
        ++index;
    }
    log::Info("wallpaper: applied to " + std::to_string(changed) + " of " + std::to_string(monitors.size()) + " monitor(s)");
    return changed;
}

bool CurrentFor(int left, int top, int right, int bottom, std::wstring* path, int* position) {
    ComScope com;
    if (!com.Usable()) return false;
    ComPtr<IDesktopWallpaper> dw;
    if (FAILED(CoCreateInstance(__uuidof(DesktopWallpaper), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dw)))) return false;
    std::vector<MonitorInfo> monitors;
    if (!Monitors(dw.Get(), &monitors)) return false;
    for (const auto& m : monitors) {
        if (m.rect.left != left || m.rect.top != top || m.rect.right != right || m.rect.bottom != bottom) continue;
        PWSTR cur = nullptr;
        if (FAILED(dw->GetWallpaper(m.id.c_str(), &cur))) return false;
        *path = TakeString(cur);
        DESKTOP_WALLPAPER_POSITION pos = DWPOS_FILL;
        dw->GetPosition(&pos);
        *position = static_cast<int>(pos);
        return !path->empty();
    }
    return false;
}

int RestoreOriginal() {
    auto backup = BackupPath();
    if (backup.empty() || !std::filesystem::exists(backup)) return 0;
    auto doc = json::Parse(ReadAll(backup));
    if (!doc || !doc->IsObject()) {
        std::error_code ec;
        std::filesystem::remove(backup, ec);  // unreadable: nothing trustworthy to restore
        return 0;
    }
    ComScope com;
    if (!com.Usable()) return 0;
    ComPtr<IDesktopWallpaper> dw;
    if (FAILED(CoCreateInstance(__uuidof(DesktopWallpaper), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dw)))) {
        log::Warn("wallpaper restore: IDesktopWallpaper unavailable; backup kept");
        return 0;
    }
    int restored = 0;
    const auto* arr = doc->Find("monitors");
    if (arr && arr->IsArray()) {
        for (const auto& o : arr->AsArray()) {
            const auto* id = o.Find("id");
            const auto* path = o.Find("path");
            if (!id || !id->IsString()) continue;
            std::wstring monitor = Utf8ToWide(id->AsString());
            std::wstring original = path && path->IsString() ? Utf8ToWide(path->AsString()) : L"";
            PWSTR cur = nullptr;
            std::wstring current;
            if (FAILED(dw->GetWallpaper(monitor.c_str(), &cur))) continue;  // monitor is gone
            current = TakeString(cur);
            if (!IsOurs(current)) continue;  // the user chose something else meanwhile: leave it
            if (original.empty()) {
                // The original was a solid colour / unreadable: no path to put back. Use the shell's own
                // empty wallpaper, which shows the desktop background colour.
                SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<wchar_t*>(L""), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            } else if (SUCCEEDED(dw->SetWallpaper(monitor.c_str(), original.c_str()))) {
                ++restored;
            }
        }
    }
    if (const auto* pos = doc->Find("position")) dw->SetPosition(static_cast<DESKTOP_WALLPAPER_POSITION>(pos->AsInt(DWPOS_FILL)));
    std::error_code ec;
    std::filesystem::remove(backup, ec);
    log::Info("wallpaper: restored " + std::to_string(restored) + " monitor(s)");
    return restored;
}

}  // namespace velora::wallpaper
