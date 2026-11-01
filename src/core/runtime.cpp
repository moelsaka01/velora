#include "core/runtime.h"

#include "core/contracts/ipc.h"
#include "core/contracts/state.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <shlobj.h>

#include <system_error>

namespace velora {

namespace {

std::wstring GetEnv(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return {};
    std::wstring v(n, L'\0');
    n = GetEnvironmentVariableW(name, v.data(), n);
    v.resize(n);
    return v;
}

Profile MakeProfile() {
    Profile p;
    p.id = GetEnv(L"VELORA_TEST_PROFILE");
    if (!p.id.empty()) {
        p.trayClass = L"VeloraTest_Shell_TrayWnd_" + p.id;
        p.secondaryTrayClass = L"VeloraTest_Shell_SecondaryTrayWnd_" + p.id;
        p.taskbarCreatedMessage = L"VeloraTest_TaskbarCreated_" + p.id;
    }
    return p;
}

std::filesystem::path EnsureDir(std::filesystem::path p) {
    if (p.empty()) return p;
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

}  // namespace

const Profile& CurrentProfile() {
    static const Profile profile = MakeProfile();
    return profile;
}

std::filesystem::path DataDir() {
    if (CurrentProfile().IsTest()) {
        std::wstring dir = GetEnv(L"VELORA_DATA_DIR");
        if (dir.empty()) return {};  // a test profile must never fall back to the real directory
        return EnsureDir(dir);
    }
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw))) return {};
    std::filesystem::path p = std::filesystem::path(raw) / L"Velora";
    CoTaskMemFree(raw);
    return EnsureDir(p);
}

std::filesystem::path StatePath() {
    auto d = DataDir();
    return d.empty() ? d : d / kStateFileName;
}

std::filesystem::path DirtyMarkerPath() {
    auto d = DataDir();
    return d.empty() ? d : d / kDirtyMarkerName;
}

std::filesystem::path LogDir() {
    auto d = DataDir();
    return d.empty() ? d : EnsureDir(d / L"logs");
}

std::wstring CurrentUserSid() {
    static const std::wstring sid = [] {
        std::wstring result;
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return result;
        DWORD len = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &len);
        if (len != 0) {
            std::string buf(len, '\0');
            if (GetTokenInformation(token, TokenUser, buf.data(), len, &len)) {
                auto* user = reinterpret_cast<TOKEN_USER*>(buf.data());
                LPWSTR str = nullptr;
                if (ConvertSidToStringSidW(user->User.Sid, &str)) {
                    result = str;
                    LocalFree(str);
                }
            }
        }
        CloseHandle(token);
        return result;
    }();
    return sid;
}

namespace {
std::wstring PipeName(const wchar_t* prefix) {
    std::wstring name = std::wstring(prefix) + CurrentUserSid();
    if (CurrentProfile().IsTest()) name += L"-" + CurrentProfile().id;
    return name;
}
}  // namespace

std::wstring ShellPipeName() { return PipeName(ipc::kShellPipePrefix); }
std::wstring WatchdogPipeName() { return PipeName(ipc::kWatchdogPipePrefix); }

std::filesystem::path ExeDir() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) return {};
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

}  // namespace velora
