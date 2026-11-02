// Files for Search: the Windows Search index through its OLE DB provider (Search.CollatorDSO). Owner: C.
//
// Queries run on a worker thread of their own (an index can take long to answer; the panel never waits for it). When Windows Search
// is disabled, stopped or does not answer, the searcher says so and the panel shows apps only.
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/win.h"

namespace velora::search {

struct FileHit {
    std::wstring name, path, type;  // display name, full path, "Directory" or ".ext"
};

enum class IndexState {
    Ready,        // the provider answered
    Disabled,     // the Windows Search service is disabled
    Unavailable,  // the service is enabled but the provider could not be reached or the query failed
};
// The sentence the panel shows when files cannot be searched ("" for Ready).
std::wstring IndexStateMessage(IndexState s);
// Reads the service configuration (WSearch) without touching the index: Disabled when its start type is "disabled", else Ready.
IndexState ProbeServiceConfig();

// Runs one statement against the index on the calling thread (COM must be initialised). `out` receives at most `limit` hits.
// Used by the worker and by the tests.
IndexState RunFileQuery(const std::wstring& sql, size_t limit, std::vector<FileHit>* out, long* hresult = nullptr);

struct QueryResult {
    unsigned id = 0;
    IndexState state = IndexState::Ready;
    std::vector<FileHit> hits;
};

class FileSearcher {
public:
    // `notify` receives `message` (wParam = request id) when a result is ready; fetch it with Take().
    // `runner` replaces the real index query (tests simulate a disabled or silent Windows Search with it).
    using Runner = std::function<IndexState(const std::wstring& sql, std::vector<FileHit>* out)>;
    FileSearcher(HWND notify, UINT message, Runner runner = {});
    ~FileSearcher();  // returns at once; a query in flight finishes in the background and is discarded
    FileSearcher(const FileSearcher&) = delete;
    FileSearcher& operator=(const FileSearcher&) = delete;

    // Replaces any request that has not started yet. The answer carries `id`.
    void Request(unsigned id, const std::wstring& sql);
    // The newest finished result, if any (UI thread).
    bool Take(QueryResult* out);

private:
    struct State;
    std::shared_ptr<State> state_;
};

}  // namespace velora::search
