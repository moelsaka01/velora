#include "apple/apple_options.h"

#include <shlobj.h>

#include "core/settings_file.h"
#include "core/win.h"

namespace velora::apple {

namespace {

bool Exists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

Options LoadOptions() {
    Options o;
    o.enabled = SettingBool("apple.enabled", false);
    o.icloudFolders = SettingBool("apple.icloudFolders", true);
    o.appStoreWeb = SettingBool("apple.appStoreWeb", true);
    return o;
}

std::vector<Folder> ICloudFoldersIn(const std::wstring& profile, const std::wstring& pictures) {
    std::vector<Folder> out;
    if (!profile.empty() && Exists(profile + L"\\iCloudDrive")) out.push_back({L"iCloud Drive", profile + L"\\iCloudDrive"});
    for (const std::wstring& dir : {profile + L"\\iCloud Photos", pictures + L"\\iCloud Photos"}) {
        if (dir.size() > 14 && Exists(dir)) {
            out.push_back({L"iCloud Photos", dir});
            break;
        }
    }
    return out;
}

std::vector<Folder> ICloudFolders(const Options& options) {
    if (!options.enabled || !options.icloudFolders) return {};
    std::wstring profile, pictures;
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_DEFAULT, nullptr, &p)) && p) profile = p;
    CoTaskMemFree(p);
    p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_DEFAULT, nullptr, &p)) && p) pictures = p;
    CoTaskMemFree(p);
    return ICloudFoldersIn(profile, pictures);
}

}  // namespace velora::apple
