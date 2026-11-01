// Original Velora wallpaper, generated procedurally (no bundled or third-party artwork), applied per monitor
// through the documented IDesktopWallpaper interface and restored on exit and on crash recovery. Owner: B.
//
// Reversibility: before the first change the original wallpaper of every monitor is saved to
// %LOCALAPPDATA%\Velora\wallpaper_backup.json. Restore puts back only the monitors whose current wallpaper is
// still one of ours (a wallpaper the user picked meanwhile is left alone) and then deletes the backup.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace velora::wallpaper {

struct Image {
    int width = 0, height = 0;
    std::vector<std::uint8_t> bgr;  // top-down, 3 bytes per pixel, rows not padded
};

// Pure and deterministic: the same arguments give the same pixels. `variant` shifts the composition so each
// monitor looks related but not identical.
Image Generate(int width, int height, bool dark, int variant);
// 24-bit BMP (bottom-up, rows padded to 4 bytes). Returns false on I/O failure.
bool WriteBmp(const std::filesystem::path& path, const Image& image);
// Average luma 0..255 (used by tests and to pick text colours later).
double AverageLuma(const Image& image);

// Longest edge Velora generates; Windows scales the smooth gradient to the real resolution.
inline constexpr int kMaxEdge = 2560;

// ---- system side (not unit-tested: it changes the user's desktop) ----
// "wallpaper.enabled" in settings.json, default true.
bool Enabled();
// Generate (cached by size/theme) and apply to every monitor. Saves the backup first. Returns the number of
// monitors changed; safe to call again.
int ApplyVelora(bool dark);
// Undo ApplyVelora. Idempotent; called from normal exit and from crash recovery.
int RestoreOriginal();
// Directory holding the generated images.
std::filesystem::path WallpaperDir();  // first candidate
std::vector<std::filesystem::path> WallpaperDirs();  // candidates, best first

}  // namespace velora::wallpaper
