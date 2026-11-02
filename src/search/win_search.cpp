#include "search/win_search.h"

#include <oledb.h>
#include <oledberr.h>
#include <wrl/client.h>

#include <algorithm>
#include <condition_variable>
#include <thread>

#include "core/log.h"
#include "core/text.h"

#pragma comment(lib, "oledb.lib")
#pragma comment(lib, "advapi32.lib")

namespace velora::search {

using Microsoft::WRL::ComPtr;

namespace {

// DBGUID_DEFAULT: the default command dialect (Windows Search SQL).
const GUID kDialectDefault = {0xc8b521fb, 0x5cf3, 0x11ce, {0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d}};
// CLSID_CollatorDataSource ("Search.CollatorDSO"), used when the ProgID lookup fails.
const GUID kCollatorDataSource = {0x9e175b8b, 0xf52a, 0x11d8, {0xb9, 0xa5, 0x50, 0x50, 0x54, 0x50, 0x30, 0x30}};

struct RowBuffer {
    DBSTATUS s0;
    LPWSTR v0;
    DBSTATUS s1;
    LPWSTR v1;
    DBSTATUS s2;
    LPWSTR v2;
};

std::wstring Text(DBSTATUS s, LPWSTR v) { return s == DBSTATUS_S_OK && v ? std::wstring(v) : std::wstring{}; }

}  // namespace

std::wstring IndexStateMessage(IndexState s) {
    switch (s) {
        case IndexState::Disabled: return L"Windows Search is turned off. Showing apps only.";
        case IndexState::Unavailable: return L"Windows Search isn't answering. Showing apps only.";
        default: return {};
    }
}

IndexState ProbeServiceConfig() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return IndexState::Ready;  // cannot tell: let the query decide
    IndexState state = IndexState::Ready;
    if (SC_HANDLE svc = OpenServiceW(scm, L"WSearch", SERVICE_QUERY_CONFIG)) {
        DWORD needed = 0;
        QueryServiceConfigW(svc, nullptr, 0, &needed);
        if (needed) {
            std::vector<BYTE> buf(needed);
            auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data());
            if (QueryServiceConfigW(svc, cfg, needed, &needed) && cfg->dwStartType == SERVICE_DISABLED) state = IndexState::Disabled;
        }
        CloseServiceHandle(svc);
    } else if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
        state = IndexState::Disabled;
    }
    CloseServiceHandle(scm);
    return state;
}

IndexState RunFileQuery(const std::wstring& sql, size_t limit, std::vector<FileHit>* out, long* hresult) {
    auto fail = [&](HRESULT hr, const char* what) {
        if (hresult) *hresult = hr;
        char hex[16];
        snprintf(hex, sizeof hex, "%08lx", static_cast<unsigned long>(hr));
        log::Warn(std::string("search: ") + what + " failed, hr=0x" + hex);
        return IndexState::Unavailable;
    };
    if (hresult) *hresult = 0;
    CLSID clsid = kCollatorDataSource;
    CLSIDFromProgID(L"Search.CollatorDSO", &clsid);  // keeps the constant when the ProgID is unknown
    ComPtr<IDBInitialize> init;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&init));
    if (FAILED(hr)) return fail(hr, "create the data source");
    hr = init->Initialize();
    if (FAILED(hr)) return fail(hr, "initialise the data source");
    ComPtr<IDBCreateSession> sessionFactory;
    if (FAILED(hr = init.As(&sessionFactory))) return fail(hr, "session factory");
    ComPtr<IUnknown> sessionUnknown, commandUnknown, rowsetUnknown;
    ComPtr<IDBCreateCommand> commandFactory;
    if (FAILED(hr = sessionFactory->CreateSession(nullptr, IID_IDBCreateCommand, &sessionUnknown)) || FAILED(hr = sessionUnknown.As(&commandFactory)))
        return fail(hr, "create a session");
    ComPtr<ICommandText> command;
    if (FAILED(hr = commandFactory->CreateCommand(nullptr, IID_ICommandText, &commandUnknown)) || FAILED(hr = commandUnknown.As(&command)))
        return fail(hr, "create a command");
    if (FAILED(hr = command->SetCommandText(kDialectDefault, sql.c_str()))) return fail(hr, "set the command text");
    ComPtr<IRowset> rowset;
    if (FAILED(hr = command->Execute(nullptr, IID_IRowset, nullptr, nullptr, &rowsetUnknown)) || FAILED(hr = rowsetUnknown.As(&rowset)))
        return fail(hr, "execute the query");

    ComPtr<IAccessor> accessor;
    if (FAILED(hr = rowset.As(&accessor))) return fail(hr, "accessor");
    DBBINDING bindings[3] = {};
    const DBBYTEOFFSET valueOffsets[3] = {offsetof(RowBuffer, v0), offsetof(RowBuffer, v1), offsetof(RowBuffer, v2)};
    const DBBYTEOFFSET statusOffsets[3] = {offsetof(RowBuffer, s0), offsetof(RowBuffer, s1), offsetof(RowBuffer, s2)};
    for (int i = 0; i < 3; ++i) {
        bindings[i].iOrdinal = static_cast<DBORDINAL>(i + 1);
        bindings[i].obValue = valueOffsets[i];
        bindings[i].obStatus = statusOffsets[i];
        bindings[i].dwPart = DBPART_VALUE | DBPART_STATUS;
        bindings[i].dwMemOwner = DBMEMOWNER_PROVIDEROWNED;
        bindings[i].eParamIO = DBPARAMIO_NOTPARAM;
        bindings[i].wType = DBTYPE_WSTR | DBTYPE_BYREF;
    }
    HACCESSOR hAccessor = 0;
    if (FAILED(hr = accessor->CreateAccessor(DBACCESSOR_ROWDATA, 3, bindings, sizeof(RowBuffer), &hAccessor, nullptr))) return fail(hr, "create the accessor");

    size_t got = 0;
    while (got < limit) {
        DBCOUNTITEM fetched = 0;
        HROW rows[16];
        HROW* pRows = rows;
        hr = rowset->GetNextRows(DB_NULL_HCHAPTER, 0, static_cast<DBROWCOUNT>(std::min<size_t>(16, limit - got)), &fetched, &pRows);
        if (FAILED(hr) || fetched == 0) break;
        for (DBCOUNTITEM i = 0; i < fetched && got < limit; ++i) {
            RowBuffer buf{};
            if (SUCCEEDED(rowset->GetData(rows[i], hAccessor, &buf))) {
                FileHit h{Text(buf.s0, buf.v0), Text(buf.s1, buf.v1), Text(buf.s2, buf.v2)};
                if (!h.path.empty()) {
                    out->push_back(std::move(h));
                    ++got;
                }
            }
        }
        rowset->ReleaseRows(fetched, rows, nullptr, nullptr, nullptr);
        if (hr == DB_S_ENDOFROWSET) break;
    }
    accessor->ReleaseAccessor(hAccessor, nullptr);
    return IndexState::Ready;
}

// ---- worker ----
struct FileSearcher::State {
    std::mutex mutex;
    std::condition_variable cv;
    bool stop = false;
    bool hasRequest = false;
    unsigned requestId = 0;
    std::wstring sql;
    bool hasResult = false;
    QueryResult result;
    HWND notify = nullptr;
    UINT message = 0;
};

FileSearcher::FileSearcher(HWND notify, UINT message, Runner runner) : state_(std::make_shared<State>()) {
    state_->notify = notify;
    state_->message = message;
    std::shared_ptr<State> st = state_;
    std::thread([st, runner = std::move(runner)] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const IndexState service = runner ? IndexState::Ready : ProbeServiceConfig();
        for (;;) {
            unsigned id = 0;
            std::wstring sql;
            {
                std::unique_lock lock(st->mutex);
                st->cv.wait(lock, [&] { return st->stop || st->hasRequest; });
                if (st->stop) break;
                id = st->requestId;
                sql = std::move(st->sql);
                st->hasRequest = false;
            }
            QueryResult r;
            r.id = id;
            if (service == IndexState::Disabled) r.state = IndexState::Disabled;
            else if (runner) r.state = runner(sql, &r.hits);
            else r.state = RunFileQuery(sql, 40, &r.hits);
            HWND notify;
            {
                std::lock_guard lock(st->mutex);
                if (st->stop) break;
                st->result = std::move(r);
                st->hasResult = true;
                notify = st->notify;
            }
            PostMessageW(notify, st->message, id, 0);
        }
        CoUninitialize();
    }).detach();
}

FileSearcher::~FileSearcher() {
    {
        std::lock_guard lock(state_->mutex);
        state_->stop = true;
    }
    state_->cv.notify_all();
}

void FileSearcher::Request(unsigned id, const std::wstring& sql) {
    {
        std::lock_guard lock(state_->mutex);
        state_->requestId = id;
        state_->sql = sql;
        state_->hasRequest = true;
    }
    state_->cv.notify_all();
}

bool FileSearcher::Take(QueryResult* out) {
    std::lock_guard lock(state_->mutex);
    if (!state_->hasResult) return false;
    *out = std::move(state_->result);
    state_->hasResult = false;
    return true;
}

}  // namespace velora::search
