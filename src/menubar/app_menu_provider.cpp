#include "menubar/app_menu_provider.h"

#include <objbase.h>

#include <condition_variable>
#include <thread>

#include "core/log.h"

namespace velora::menubar {

namespace {
constexpr ULONGLONG kUiaCacheMs = 120000;  // a UI Automation menu bar rarely changes; a window that has none is not asked again for two minutes

struct Job {
    bool probe = false;  // a newer probe replaces queued older ones
    std::function<void()> run;
};
}  // namespace

struct AppMenuProvider::State {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Job> jobs;
    std::deque<MenuAnswer> answers;
    bool stop = false;
    bool probeChromium = false;
    unsigned nextId = 0;
    HWND notify = nullptr;
    UINT message = 0;
    // worker-only
    struct Cached {
        AppMenuBar bar;
        ULONGLONG at = 0;
    };
    std::map<HWND, Cached> uiaCache;

    void Post(MenuAnswer a) {
        HWND n;
        {
            std::lock_guard lock(mutex);
            if (stop) return;
            answers.push_back(std::move(a));
            n = notify;
        }
        PostMessageW(n, message, 0, 0);
    }
    void Push(Job j) {
        {
            std::lock_guard lock(mutex);
            if (j.probe) std::erase_if(jobs, [](const Job& x) { return x.probe; });
            jobs.push_back(std::move(j));
        }
        cv.notify_one();
    }
};

AppMenuProvider::AppMenuProvider(HWND notify, UINT message) : state_(std::make_shared<State>()) {
    state_->notify = notify;
    state_->message = message;
    std::shared_ptr<State> st = state_;
    std::thread([st] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (;;) {
            Job job;
            {
                std::unique_lock lock(st->mutex);
                st->cv.wait(lock, [&] { return st->stop || !st->jobs.empty(); });
                if (st->stop) break;
                job = std::move(st->jobs.front());
                st->jobs.pop_front();
            }
            try {
                job.run();
            } catch (...) {
                log::Warn("app menus: a request failed with an exception; ignored");
            }
        }
        CoUninitialize();
    }).detach();
}

AppMenuProvider::~AppMenuProvider() {
    {
        std::lock_guard lock(state_->mutex);
        state_->stop = true;
        state_->jobs.clear();
    }
    state_->cv.notify_all();
}

void AppMenuProvider::SetProbeChromium(bool on) {
    std::lock_guard lock(state_->mutex);
    state_->probeChromium = on;
}

unsigned AppMenuProvider::Probe(HWND hwnd) {
    std::shared_ptr<State> st = state_;
    unsigned id;
    {
        std::lock_guard lock(st->mutex);
        id = ++st->nextId;
    }
    st->Push({true, [st, hwnd, id] {
        MenuAnswer a;
        a.kind = MenuAnswer::Kind::Bar;
        a.id = id;
        a.hwnd = hwnd;
        AppMenuBar bar;
        if (ReadWin32Bar(hwnd, &bar)) {
            a.ok = true;
            a.bar = std::move(bar);
            st->Post(std::move(a));
            return;
        }
        wchar_t cls[128] = {};
        GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
        bool chromium;
        {
            std::lock_guard lock(st->mutex);
            chromium = st->probeChromium;
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId() && MayProbeUia(cls, chromium)) {
            const ULONGLONG now = GetTickCount64();
            auto it = st->uiaCache.find(hwnd);
            if (it != st->uiaCache.end() && now - it->second.at < kUiaCacheMs) {
                a.bar = it->second.bar;
                a.ok = a.bar.tier != MenuTier::None;
            } else {
                AppMenuBar uia;
                const bool found = ReadUiaBar(hwnd, &uia);
                if (st->uiaCache.size() > 64) st->uiaCache.clear();
                st->uiaCache[hwnd] = {found ? uia : AppMenuBar{}, now};
                a.ok = found;
                if (found) a.bar = std::move(uia);
            }
        }
        st->Post(std::move(a));
    }});
    return id;
}

unsigned AppMenuProvider::Expand(HWND hwnd, MenuTier tier, int titleIndex, std::vector<int> path) {
    std::shared_ptr<State> st = state_;
    unsigned id;
    {
        std::lock_guard lock(st->mutex);
        id = ++st->nextId;
    }
    st->Push({false, [st, hwnd, tier, titleIndex, path = std::move(path), id] {
        MenuAnswer a;
        a.kind = MenuAnswer::Kind::Items;
        a.id = id;
        a.hwnd = hwnd;
        a.titleIndex = titleIndex;
        a.path = path;
        a.ok = tier == MenuTier::Win32 ? ReadWin32Items(hwnd, titleIndex, &a.items) : tier == MenuTier::Uia ? ReadUiaItems(hwnd, titleIndex, path, &a.items) : false;
        st->Post(std::move(a));
    }});
    return id;
}

void AppMenuProvider::Invoke(HWND hwnd, MenuTier tier, int titleIndex, std::vector<int> path, unsigned id, std::wstring label) {
    state_->Push({false, [hwnd, tier, titleIndex, path = std::move(path), id, label = std::move(label)] {
        const bool ok = tier == MenuTier::Win32 ? InvokeWin32(hwnd, id) : tier == MenuTier::Uia ? InvokeUia(hwnd, titleIndex, path, label) : false;
        if (!ok) log::Warn("app menus: could not run the menu command (tier " + std::to_string(static_cast<int>(tier)) + ")");
    }});
}

void AppMenuProvider::InvokeTitle(HWND hwnd, MenuTier tier, unsigned id, int titleIndex) {
    state_->Push({false, [hwnd, tier, id, titleIndex] {
        const bool ok = tier == MenuTier::Win32 ? InvokeWin32(hwnd, id) : tier == MenuTier::Uia ? InvokeUiaTitle(hwnd, titleIndex) : false;
        if (!ok) log::Warn("app menus: could not run the menu bar command");
    }});
}

void AppMenuProvider::Forget(HWND hwnd) {
    std::shared_ptr<State> st = state_;
    st->Push({false, [st, hwnd] { st->uiaCache.erase(hwnd); }});
}

bool AppMenuProvider::Take(MenuAnswer* out) {
    std::lock_guard lock(state_->mutex);
    if (state_->answers.empty()) return false;
    *out = std::move(state_->answers.front());
    state_->answers.pop_front();
    return true;
}

}  // namespace velora::menubar
