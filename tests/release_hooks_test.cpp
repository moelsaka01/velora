// Proves the test-only hooks are compiled out of the release binaries.
//
//   release_hooks_test <release watchdog> <release shell> <testing watchdog> <testing shell>
//
// Each hook leaves a string behind (an env var name, a message type). The release
// binaries must contain none of them; the testing binaries must contain all of them,
// otherwise this check could pass for the wrong reason (e.g. strings stripped everywhere).
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::string ReadAll(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Contains(const std::string& haystack, const std::string& ascii) {
    if (haystack.find(ascii) != std::string::npos) return true;
    std::string wide;  // UTF-16LE, how the wchar_t literals are stored
    for (char c : ascii) {
        wide.push_back(c);
        wide.push_back('\0');
    }
    return haystack.find(wide) != std::string::npos;
}

struct Binary {
    const char* label;
    const char* path;
    std::vector<std::string> hooks;
    bool expectPresent;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::printf("usage: release_hooks_test <release watchdog> <release shell> <testing watchdog> <testing shell>\n");
        return 64;
    }
    const std::vector<std::string> watchdogHooks = {"VELORA_TEST_WATCHDOG_REJECT_HELLO_FILE", "VELORA_TEST_LOGIN_"};
    const std::vector<std::string> shellHooks = {"debug_hang"};
    const Binary binaries[] = {
        {"release watchdog", argv[1], watchdogHooks, false},
        {"release shell", argv[2], shellHooks, false},
        {"testing watchdog", argv[3], watchdogHooks, true},
        {"testing shell", argv[4], shellHooks, true},
    };

    int failed = 0;
    for (const Binary& b : binaries) {
        std::string data = ReadAll(b.path);
        if (data.size() < 1024) {
            std::printf("FAIL %s: cannot read %s\n", b.label, b.path);
            ++failed;
            continue;
        }
        for (const std::string& hook : b.hooks) {
            bool present = Contains(data, hook);
            bool ok = present == b.expectPresent;
            std::printf("%s %s: \"%s\" %s\n", ok ? "ok  " : "FAIL", b.label, hook.c_str(), present ? "present" : "absent");
            if (!ok) ++failed;
        }
    }
    std::printf("release_hooks_test: %s\n", failed ? "FAILED" : "all checks passed");
    return failed ? 1 : 0;
}
