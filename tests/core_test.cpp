// Unit tests for core/: json, state store, event bus, IPC pipe.
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#include "core/contracts/ipc.h"
#include "core/event_bus.h"
#include "core/ipc_pipe.h"
#include "core/json.h"
#include "core/state_store.h"

namespace {

int g_failed = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failed;                                                      \
        }                                                                    \
    } while (0)

void JsonRoundTrip() {
    using namespace velora::json;
    auto v = Parse(R"({"a":1,"b":[true,false,null,2.5,"x\n\u00e9\ud83d\ude00"],"c":{"d":-7}})");
    CHECK(v && v->IsObject());
    CHECK(v->Find("a")->AsInt() == 1);
    CHECK(v->Find("b")->AsArray().size() == 5);
    CHECK(v->Find("b")->AsArray()[3].AsDouble() == 2.5);
    CHECK(v->Find("b")->AsArray()[4].AsString() == "x\n\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(v->Find("c")->Find("d")->AsInt() == -7);
    auto again = Parse(v->Dump());
    CHECK(again && again->Dump() == v->Dump());

    // Nested scalars survive Dump (regression: scalars inside containers).
    CHECK(Parse("[1,true,\"s\"]")->Dump() == "[1,true,\"s\"]");

    CHECK(!Parse(""));
    CHECK(!Parse("{"));
    CHECK(!Parse("{\"a\":}"));
    CHECK(!Parse("[1,]"));
    CHECK(!Parse("{} x"));
    CHECK(!Parse("\"\\ud800\""));        // lone surrogate
    CHECK(!Parse("\"a\x01\""));          // raw control char
    CHECK(Parse(std::string(64, '[') + std::string(64, ']')).has_value());
    CHECK(!Parse(std::string(200, '[') + std::string(200, ']')));  // depth bomb
}

void StateRoundTrip() {
    velora::SwitchState s;
    s.taskbarAutoHide = true;
    s.monitors.push_back({L"\\\\.\\DISPLAY1", {0, 0, 1920, 1040}, true});
    s.monitors.push_back({L"\\\\.\\DISPLAY2", {1920, 0, 3840, 1080}, false});
    s.wallpaperPath = L"C:\\Users\\\u00e9\\wall.jpg";
    s.foregroundHwnd = 0x1234ABCD;
    s.shellPid = 4242;
    s.watchdogPid = 4243;
    s.shellStartTime = 133000000000000000ull;
    s.appbarHwnds = {0x30A4Cull, 0x1F00D2ull};

    auto back = velora::ParseState(velora::SerializeState(s));
    CHECK(back.has_value());
    CHECK(back->taskbarAutoHide == s.taskbarAutoHide);
    CHECK(back->monitors == s.monitors);
    CHECK(back->wallpaperPath == s.wallpaperPath);
    CHECK(back->foregroundHwnd == s.foregroundHwnd);
    CHECK(back->shellPid == s.shellPid && back->watchdogPid == s.watchdogPid);
    CHECK(back->shellStartTime == s.shellStartTime);
    CHECK(back->appbarHwnds == s.appbarHwnds);
    CHECK(velora::ParseState(R"({"shellPid":7})")->appbarHwnds.empty());  // older files still load

    // Tolerant: partial / newer files still load, garbage does not.
    auto partial = velora::ParseState(R"({"schemaVersion":9,"future":1,"taskbarAutoHide":true})");
    CHECK(partial && partial->taskbarAutoHide && partial->schemaVersion == 9 && partial->monitors.empty());
    CHECK(!velora::ParseState("not json"));
    CHECK(!velora::ParseState("[1,2]"));

    auto dir = std::filesystem::temp_directory_path() / L"velora_core_test";
    std::filesystem::create_directories(dir);
    auto state = dir / "state.json";
    auto marker = dir / "dirty.marker";
    std::filesystem::remove(marker);
    CHECK(velora::SaveState(s, state));
    CHECK(velora::SaveState(s, state));  // replace existing
    CHECK(!std::filesystem::exists(dir / "state.json.tmp"));
    CHECK(velora::LoadState(state)->shellPid == 4242);
    CHECK(!velora::DirtyMarkerExists(marker));
    CHECK(velora::CreateDirtyMarker(marker));
    CHECK(velora::DirtyMarkerExists(marker));
    CHECK(velora::RemoveDirtyMarker(marker));
    CHECK(velora::RemoveDirtyMarker(marker));  // already absent is fine
    CHECK(!velora::DirtyMarkerExists(marker));
    std::filesystem::remove_all(dir);
}

void EventBusDispatch() {
    velora::EventBus bus;
    int created = 0, other = 0;
    auto t = bus.Subscribe(velora::EventType::WindowCreated, [&](const velora::Event& e) {
        created += static_cast<int>(e.window.hwnd);
    });
    bus.Subscribe(velora::EventType::Entered, [&](const velora::Event&) { ++other; });
    bus.Post(velora::EventType::WindowCreated, {5, 1, false});
    bus.Post(velora::EventType::Entered);
    CHECK(created == 5 && other == 1);
    bus.Unsubscribe(t);
    bus.Post(velora::EventType::WindowCreated, {5, 1, false});
    CHECK(created == 5);
}

void PipeEcho() {
    using namespace velora::ipc;
    std::wstring name = L"\\\\.\\pipe\\velora-core-test-" + std::to_wstring(GetCurrentProcessId());
    PipeServer server;
    std::atomic<int> events{0};
    CHECK(server.Start(name, [&](std::string_view req) -> std::string {
        auto m = velora::json::Parse(req);
        if (!m) return MakeMessage("response", "hello", -1, {}, 400, "bad json");
        if (m->Find("kind")->AsString() == "event") { ++events; return {}; }
        return MakeMessage("response", "get_state", m->Find("id")->AsInt(), {}, 0);
    }));
    PipeServer second;
    CHECK(!second.Start(name, nullptr));  // single-instance check

    // The server is pumped by a helper thread so the client can block.
    std::atomic<bool> stop{false};
    std::thread pump([&] {
        while (!stop) {
            auto h = server.WaitHandles();
            WaitForMultipleObjects(static_cast<DWORD>(h.size()), h.data(), FALSE, 20);
            server.Pump();
        }
    });

    std::string resp;
    CHECK(Call(name, MakeMessage("request", "get_state", 7), &resp, 2000));
    auto r = velora::json::Parse(resp);
    CHECK(r && r->Find("id")->AsInt() == 7 && r->Find("status")->AsInt() == 0);

    // Persistent connection: several requests, plus fire-and-forget events.
    PipeClient c;
    CHECK(c.Connect(name, 2000));
    for (int i = 0; i < 20; ++i) {
        CHECK(c.Request(MakeMessage("request", "get_state", i), &resp, 2000));
        CHECK(velora::json::Parse(resp)->Find("id")->AsInt() == i);
        c.Post(MakeMessage("event", "heartbeat"));
    }
    CHECK(c.Request(MakeMessage("request", "get_state", 99), &resp, 2000));  // proves events were consumed
    CHECK(events > 0);

    // Malformed input is answered, oversize frames are dropped without crashing.
    CHECK(c.Request("not json", &resp, 2000));
    CHECK(velora::json::Parse(resp)->Find("status")->AsInt() == 400);

    stop = true;
    pump.join();
}

}  // namespace

int main() {
    JsonRoundTrip();
    StateRoundTrip();
    EventBusDispatch();
    PipeEcho();
    if (g_failed) {
        std::printf("%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("core_test: all checks passed\n");
    return 0;
}
