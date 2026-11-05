// Security properties that can be checked without a desktop (pure, deterministic):
//  * the JSON parser (settings.json, state.json, IPC) survives thousands of mutated and random inputs without crashing or hanging;
//  * the command line Velora builds for velora-files round-trips through CommandLineToArgvW for hostile paths (quotes, backslashes,
//    spaces, trailing backslashes): the arguments the child sees are exactly the ones intended, so a path can never add an argument;
//  * log lines cannot be forged or split by text that comes from outside;
//  * theming never touches elevated, game, anti-cheat or Velora's own windows (policy rules).
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "apps/files_launch.h"
#include "core/json.h"
#include "core/log.h"
#include "core/win.h"

#include <shellapi.h>
#include "theming/theme_policy.h"

using namespace velora;

static int g_failed = 0, g_checks = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        ++g_checks;                                                  \
        if (!(c)) {                                                  \
            ++g_failed;                                              \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                            \
    } while (0)

int main() {
    std::mt19937 rng(0xC0FFEE);  // fixed seed: the same inputs every run
    const auto t0 = std::chrono::steady_clock::now();

    // ---- JSON: mutate valid documents and feed raw random bytes ----
    const std::vector<std::string> seeds = {
        R"({"v":1,"kind":"request","type":"open_files","id":7,"payload":{"path":"C:\\x","select":"a"}})",
        R"({"dock":{"pinned":[{"name":"A","launch":"C:\\a.exe"}],"magnification":true},"theming":{"exclude":["a.exe"],"backdrop":"mica"}})",
        R"([1,2.5,-3e10,true,false,null,"s\u00e9\n",[[[]]],{"a":{"b":{"c":[]}}}])",
        R"({"schemaVersion":1,"monitors":[{"deviceName":"\\\\.\\DISPLAY1","workArea":{"left":0,"top":0,"right":1920,"bottom":1080}}]})",
    };
    int parsed = 0, rejected = 0;
    for (int iter = 0; iter < 60000; ++iter) {
        std::string s = seeds[rng() % seeds.size()];
        const int edits = 1 + static_cast<int>(rng() % 6);
        for (int e = 0; e < edits && !s.empty(); ++e) {
            const size_t pos = rng() % s.size();
            switch (rng() % 5) {
                case 0: s[pos] = static_cast<char>(rng()); break;                            // flip a byte
                case 1: s.erase(pos, 1 + rng() % 4); break;                                    // delete
                case 2: s.insert(pos, 1, "{}[]\":,\\-0e."[rng() % 12]); break;                 // structural insert
                case 3: s.insert(pos, std::string(1 + rng() % 8, '[')); break;                 // nesting
                default: s.resize(pos); break;                                                 // truncate
            }
        }
        auto v = json::Parse(s);
        v ? ++parsed : ++rejected;
        if (v) (void)v->Dump();  // whatever parsed must also serialise
    }
    for (int iter = 0; iter < 20000; ++iter) {
        std::string s(rng() % 200, '\0');
        for (char& c : s) c = static_cast<char>(rng());
        (void)json::Parse(s);
    }
    CHECK(parsed > 0 && rejected > 0);  // both outcomes happened: the mutations are neither all harmless nor all fatal
    CHECK(!json::Parse(std::string(100000, '[')));                       // depth bomb, no stack overflow
    CHECK(!json::Parse("{\"a\":" + std::string(5000, '1') + "x}"));      // garbage after a long number
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(elapsed < 60.0);  // no pathological (quadratic) behaviour
    std::printf("INFO json fuzz: %d parsed, %d rejected, %.1f s\n", parsed, rejected, elapsed);

    // ---- command line round trip with hostile arguments ----
    {
        const std::wstring exe = L"C:\\Program Files\\Velora\\velora-files.exe";
        const std::vector<std::wstring> hostile = {
            L"C:\\Users\\a b\\Docs", L"C:\\", L"C:\\dir\\", L"C:\\dir\\\\", L"D:\\a\"b", L"D:\\a\\\"b", L"\" --select \"x", L"C:\\x\" --evil \"y",
            L"\\\\server\\share\\", L"C:\\a\\\\\"", L"x y z", L"tab\there", L"C:\\\u00e9\u4e2d\\f", L"--select", L"-", L"/?",
        };
        for (const auto& path : hostile) {
            for (int variant = 0; variant < 2; ++variant) {
                const std::wstring cmd = variant == 0 ? apps::FilesCommandLine(exe, path, L"") : apps::FilesCommandLine(exe, L"", path);
                int argc = 0;
                LPWSTR* argv = CommandLineToArgvW(cmd.c_str(), &argc);
                const int want = variant == 0 ? 2 : 3;
                bool ok = argv && argc == want && std::wstring(argv[0]) == exe;
                if (ok && variant == 0) ok = std::wstring(argv[1]) == path;
                if (ok && variant == 1) ok = std::wstring(argv[1]) == L"--select" && std::wstring(argv[2]) == path;
                if (!ok) std::printf("FAIL command line for [%ls] -> %ls (argc %d)\n", path.c_str(), cmd.c_str(), argc);
                CHECK(ok);
                if (argv) LocalFree(argv);
            }
        }
        for (int iter = 0; iter < 3000; ++iter) {  // random strings of quotes, backslashes and spaces
            std::wstring path;
            const int n = 1 + static_cast<int>(rng() % 12);
            for (int i = 0; i < n; ++i) path += L"\\\" ab"[rng() % 5];
            const std::wstring cmd = apps::FilesCommandLine(exe, path, L"");
            int argc = 0;
            LPWSTR* argv = CommandLineToArgvW(cmd.c_str(), &argc);
            const bool ok = argv && argc == 2 && std::wstring(argv[1]) == path;
            if (!ok) {
                std::printf("FAIL random command line [%ls]\n", path.c_str());
                ++g_failed;
            }
            ++g_checks;
            if (argv) LocalFree(argv);
        }
    }

    // ---- log lines cannot be forged ----
    CHECK(log::Sanitize("a\r\n2026-11-05 [1] INFO fake line") == "a  2026-11-05 [1] INFO fake line");
    CHECK(log::Sanitize(std::string("x\x1b[31my\0z", 8)).find('\x1b') == std::string::npos);
    CHECK(log::Sanitize("plain \xC3\xA9 text") == "plain \xC3\xA9 text");  // UTF-8 stays

    // ---- theming policy: the dangerous classes are never touched ----
    {
        theming::Config c;
        theming::WindowFacts f;
        f.exeName = L"notepad.exe";
        f.className = L"Notepad";
        f.style = WS_OVERLAPPEDWINDOW;
        CHECK(theming::Evaluate(f, c) == theming::Skip::None);
        auto skip = [&](auto mutate) {
            theming::WindowFacts g = f;
            mutate(g);
            return theming::Evaluate(g, c);
        };
        CHECK(skip([](auto& g) { g.uncontrollable = true; }) == theming::Skip::Uncontrollable);
        CHECK(skip([](auto& g) { g.exeName = L"easyanticheat.exe"; }) == theming::Skip::AntiCheat);
        CHECK(skip([](auto& g) { g.exeName = L"vgc.exe"; }) == theming::Skip::AntiCheat);
        CHECK(skip([](auto& g) { g.exePath = L"d:\\steamapps\\common\\g\\g.exe"; }) == theming::Skip::Game);
        CHECK(skip([](auto& g) { g.exeName = L"velora-files.exe"; }) == theming::Skip::OwnWindow);
        CHECK(skip([](auto& g) { g.fullscreen = true; }) == theming::Skip::Fullscreen);
    }

    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
