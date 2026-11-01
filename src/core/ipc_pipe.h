// Named-pipe transport for IPC v1 (framing: little-endian u32 length + UTF-8
// JSON, see contracts/ipc.h). Owner: A.
//
// PipeServer is single-threaded and non-blocking: add WaitHandles() to the
// caller's MsgWaitForMultipleObjects set and call Pump() when anything
// signals. A hung owner thread therefore also stops answering, which is what
// the watchdog's heartbeat check relies on.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/json.h"
#include "core/win.h"

namespace velora::ipc {

class PipeServer {
public:
    // Returns the response frame body, or "" for events that get no response.
    using Handler = std::function<std::string(std::string_view request)>;

    PipeServer() = default;
    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;
    ~PipeServer() { Stop(); }

    // Fails (false) if the pipe name is already served: that is the
    // single-instance check for the shell and the watchdog.
    bool Start(const std::wstring& name, Handler handler, int instances = 3);
    void Stop();

    std::vector<HANDLE> WaitHandles() const;
    void Pump();

private:
    struct Instance {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        HANDLE event = nullptr;
        OVERLAPPED ov{};
        enum class State { Connecting, ReadHeader, ReadBody, Writing } state = State::Connecting;
        std::string buf;
        size_t have = 0;  // bytes of `buf` already transferred
    };

    bool CreateInstance(Instance& inst, bool first);
    void BeginConnect(Instance& inst);
    void BeginRead(Instance& inst, size_t total);
    void BeginWrite(Instance& inst, std::string frame);
    void Reset(Instance& inst);
    void Advance(Instance& inst);

    std::wstring name_;
    Handler handler_;
    std::vector<Instance*> instances_;
    void* sd_ = nullptr;  // PSECURITY_DESCRIPTOR owned via LocalFree
};

// Blocking client with timeouts, plus a non-blocking Post() for heartbeats.
class PipeClient {
public:
    PipeClient() = default;
    PipeClient(const PipeClient&) = delete;
    PipeClient& operator=(const PipeClient&) = delete;
    ~PipeClient() { Close(); }

    bool Connect(const std::wstring& name, DWORD timeoutMs);
    // One request, one response. On timeout the connection is closed.
    bool Request(std::string_view request, std::string* response, DWORD timeoutMs);
    // Fire-and-forget frame; never blocks. False if a previous Post is still
    // unfinished or the connection is gone.
    bool Post(std::string_view message);
    bool IsConnected() const { return pipe_ != INVALID_HANDLE_VALUE; }
    void Close();

private:
    bool WaitIo(OVERLAPPED& ov, DWORD deadlineTick, DWORD* transferred);
    bool WriteAll(std::string_view frame, DWORD deadlineTick);
    bool ReadExact(void* dst, DWORD n, DWORD deadlineTick);

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE event_ = nullptr;
    // Post() state: the buffer must outlive the pending write.
    OVERLAPPED postOv_{};
    HANDLE postEvent_ = nullptr;
    std::string postBuf_;
    bool postPending_ = false;
};

// Convenience: connect, send one request, read one response.
bool Call(const std::wstring& pipeName, std::string_view request, std::string* response, DWORD timeoutMs);

// Builds {"v":1,"kind":...,"type":...,["id":..],["status":..],["error":..],["payload":..]}.
// id < 0 and status < 0 omit those fields; a null payload is omitted.
std::string MakeMessage(const char* kind, const char* type, std::int64_t id = -1,
                        const json::Value& payload = {}, int status = -1,
                        const std::string& error = {});

}  // namespace velora::ipc
