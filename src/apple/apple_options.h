// Apple services settings and iCloud folders: no dependency on the app index, so Files can use it. Owner: C.
#pragma once

#include <string>
#include <vector>

namespace velora::apple {

struct Options {
    bool enabled = false;        // apple.enabled
    bool icloudFolders = true;   // apple.icloudFolders: iCloud Drive / Photos in the Files sidebar
    bool appStoreWeb = true;     // apple.appStoreWeb
};
Options LoadOptions();

// iCloud for Windows' local folders (read from disk only), if they exist: iCloud Drive and iCloud Photos.
struct Folder {
    std::wstring label, path;
};
std::vector<Folder> ICloudFolders(const Options& options);
// Same, with the user's profile folders given (tests).
std::vector<Folder> ICloudFoldersIn(const std::wstring& profile, const std::wstring& pictures);

}  // namespace velora::apple
