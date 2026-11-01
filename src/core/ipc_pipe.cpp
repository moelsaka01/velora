#include "core/ipc_pipe.h"

#include <sddl.h>

#include "core/contracts/ipc.h"
#include "core/runtime.h"

namespace velora::ipc {

namespace {

std::string Frame(std::string_view body) {
    std::string f(4, '\0');
    auto n = static_cast<std::uint32_t>(body.size());
    f[0] = static_cast<char>(n & 0xFF);
    f[1] = static_cast<char>((n >> 8) & 0xFF);
    f[2] = static_cast<char>((n >> 16) & 0xFF);
    f[3] = static_cast<char>((n >> 24) & 0xFF);
    f.append(body);
    return f;
}

std::uint32_t DecodeLength(const std::string& h) {
    return static_cast<std::uint8_t>(h[0]) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(h[1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(h[2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(h[3])) << 24);
}

// Makes an OVERLAPPED look completed so Pump() handles a synchronous result
// (ERROR_PIPE_CONNECTED) or a failed start through the normal path.
void FakeComplete(OVERLAPPED& ov, HANDLE event, bool success) {
    ov.Internal = success ? 0 : static_cast<ULONG_PTR>(0xC0000001);  // STATUS_SUCCESS / STATUS_UNSUCCESSFUL
    ov.InternalHigh = 0;
    SetEvent(event);
}

}  // namespace

// ---------------------------------------------------------------- server

bool PipeServer::Start(const std::wstring& name, Handler handler, int instances) {
    Stop();
    name_ = name;
    handler_ = std::move(handler);

    // Only the current user may connect (the default DACL also admits others).
    std::wstring sddl = L"D:P(A;;GA;;;" + CurrentUserSid() + L")";
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return false;
    sd_ = sd;

    for (int i = 0; i < instances; ++i) {
        auto* inst = new Instance();
        if (!CreateInstance(*inst, /*first=*/i == 0)) {
            delete inst;
            Stop();
            return false;
        }
        instances_.push_back(inst);
    }
    for (auto* inst : instances_) BeginConnect(*inst);
    return true;
}

bool PipeServer::CreateInstance(Instance& inst, bool first) {
    SECURITY_ATTRIBUTES sa{sizeof sa, sd_, FALSE};
    DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
    inst.pipe = CreateNamedPipeW(name_.c_str(), openMode,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                 PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, &sa);
    if (inst.pipe == INVALID_HANDLE_VALUE) return false;
    inst.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!inst.event) return false;
    inst.ov = OVERLAPPED{};
    inst.ov.hEvent = inst.event;
    return true;
}

void PipeServer::Stop() {
    for (auto* inst : instances_) {
        if (inst->pipe != INVALID_HANDLE_VALUE) {
            CancelIoEx(inst->pipe, nullptr);
            CloseHandle(inst->pipe);
        }
        if (inst->event) CloseHandle(inst->event);
        delete inst;
    }
    instances_.clear();
    if (sd_) {
        LocalFree(sd_);
        sd_ = nullptr;
    }
}

std::vector<HANDLE> PipeServer::WaitHandles() const {
    std::vector<HANDLE> h;
    for (auto* inst : instances_) h.push_back(inst->event);
    return h;
}

void PipeServer::BeginConnect(Instance& inst) {
    inst.state = Instance::State::Connecting;
    inst.buf.clear();
    inst.have = 0;
    ResetEvent(inst.event);
    inst.ov = OVERLAPPED{};
    inst.ov.hEvent = inst.event;
    if (ConnectNamedPipe(inst.pipe, &inst.ov)) {
        FakeComplete(inst.ov, inst.event, true);
        return;
    }
    DWORD err = GetLastError();
    if (err == ERROR_PIPE_CONNECTED) FakeComplete(inst.ov, inst.event, true);
    else if (err != ERROR_IO_PENDING) FakeComplete(inst.ov, inst.event, false);
}

void PipeServer::BeginRead(Instance& inst, size_t total) {
    inst.buf.resize(total);
    ResetEvent(inst.event);
    DWORD keep = static_cast<DWORD>(inst.have);
    inst.ov = OVERLAPPED{};
    inst.ov.hEvent = inst.event;
    if (!ReadFile(inst.pipe, inst.buf.data() + keep, static_cast<DWORD>(total - keep), nullptr, &inst.ov)) {
        if (GetLastError() != ERROR_IO_PENDING) FakeComplete(inst.ov, inst.event, false);
    }
}

void PipeServer::BeginWrite(Instance& inst, std::string frame) {
    inst.state = Instance::State::Writing;
    inst.buf = std::move(frame);
    inst.have = 0;
    ResetEvent(inst.event);
    inst.ov = OVERLAPPED{};
    inst.ov.hEvent = inst.event;
    if (!WriteFile(inst.pipe, inst.buf.data(), static_cast<DWORD>(inst.buf.size()), nullptr, &inst.ov)) {
        if (GetLastError() != ERROR_IO_PENDING) FakeComplete(inst.ov, inst.event, false);
    }
}

void PipeServer::Reset(Instance& inst) {
    CancelIoEx(inst.pipe, nullptr);
    DisconnectNamedPipe(inst.pipe);
    BeginConnect(inst);
}

void PipeServer::Pump() {
    for (auto* inst : instances_) {
        // Bounded: each pass either leaves an I/O pending or finishes a state.
        for (int guard = 0; guard < 16 && HasOverlappedIoCompleted(&inst->ov); ++guard) Advance(*inst);
    }
}

void PipeServer::Advance(Instance& inst) {
    DWORD n = 0;
    bool ok = GetOverlappedResult(inst.pipe, &inst.ov, &n, FALSE) != FALSE;

    switch (inst.state) {
        case Instance::State::Connecting:
            if (!ok) { Reset(inst); return; }
            inst.state = Instance::State::ReadHeader;
            inst.buf.clear();
            inst.have = 0;
            BeginRead(inst, 4);
            return;

        case Instance::State::ReadHeader:
        case Instance::State::ReadBody: {
            if (!ok || n == 0) { Reset(inst); return; }
            inst.have += n;
            if (inst.have < inst.buf.size()) { BeginRead(inst, inst.buf.size()); return; }  // short read
            if (inst.state == Instance::State::ReadHeader) {
                std::uint32_t len = DecodeLength(inst.buf);
                if (len == 0 || len > kMaxMessageBytes) { Reset(inst); return; }  // protocol violation
                inst.state = Instance::State::ReadBody;
                inst.have = 0;
                BeginRead(inst, len);
                return;
            }
            std::string request = std::move(inst.buf);
            std::string response = handler_ ? handler_(request) : std::string();
            if (response.size() > kMaxMessageBytes) response.clear();
            if (response.empty()) {
                inst.state = Instance::State::ReadHeader;
                inst.have = 0;
                BeginRead(inst, 4);
            } else {
                BeginWrite(inst, Frame(response));
            }
            return;
        }

        case Instance::State::Writing:
            if (!ok) { Reset(inst); return; }
            inst.have += n;
            if (inst.have < inst.buf.size()) {
                // Rare partial write: send the remainder.
                std::string rest = inst.buf.substr(inst.have);
                BeginWrite(inst, std::move(rest));
                return;
            }
            inst.state = Instance::State::ReadHeader;
            inst.have = 0;
            BeginRead(inst, 4);
            return;
    }
}

// ---------------------------------------------------------------- client

bool PipeClient::Connect(const std::wstring& name, DWORD timeoutMs) {
    Close();
    DWORD start = GetTickCount();
    for (;;) {
        pipe_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe_ != INVALID_HANDLE_VALUE) break;
        DWORD err = GetLastError();
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= timeoutMs) return false;
        if (err == ERROR_PIPE_BUSY) WaitNamedPipeW(name.c_str(), std::min<DWORD>(timeoutMs - elapsed, 50));
        else if (err == ERROR_FILE_NOT_FOUND) Sleep(10);
        else return false;
    }
    event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    postEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event_ || !postEvent_) { Close(); return false; }
    return true;
}

void PipeClient::Close() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipe_, nullptr);
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
    if (event_) { CloseHandle(event_); event_ = nullptr; }
    if (postEvent_) { CloseHandle(postEvent_); postEvent_ = nullptr; }
    postPending_ = false;
}

bool PipeClient::WaitIo(OVERLAPPED& ov, DWORD deadlineTick, DWORD* transferred) {
    LONG remaining = static_cast<LONG>(deadlineTick - GetTickCount());
    DWORD wait = remaining > 0 ? static_cast<DWORD>(remaining) : 0;
    if (WaitForSingleObject(ov.hEvent, wait) != WAIT_OBJECT_0) {
        CancelIoEx(pipe_, &ov);
        GetOverlappedResult(pipe_, &ov, transferred, TRUE);  // reap the cancelled op
        return false;
    }
    return GetOverlappedResult(pipe_, &ov, transferred, FALSE) != FALSE;
}

bool PipeClient::WriteAll(std::string_view frame, DWORD deadlineTick) {
    size_t sent = 0;
    while (sent < frame.size()) {
        OVERLAPPED ov{};
        ov.hEvent = event_;
        ResetEvent(event_);
        DWORD n = 0;
        if (!WriteFile(pipe_, frame.data() + sent, static_cast<DWORD>(frame.size() - sent), nullptr, &ov) &&
            GetLastError() != ERROR_IO_PENDING)
            return false;
        if (!WaitIo(ov, deadlineTick, &n) || n == 0) return false;
        sent += n;
    }
    return true;
}

bool PipeClient::ReadExact(void* dst, DWORD want, DWORD deadlineTick) {
    DWORD got = 0;
    while (got < want) {
        OVERLAPPED ov{};
        ov.hEvent = event_;
        ResetEvent(event_);
        DWORD n = 0;
        if (!ReadFile(pipe_, static_cast<char*>(dst) + got, want - got, nullptr, &ov) &&
            GetLastError() != ERROR_IO_PENDING)
            return false;
        if (!WaitIo(ov, deadlineTick, &n) || n == 0) return false;
        got += n;
    }
    return true;
}

bool PipeClient::Request(std::string_view request, std::string* response, DWORD timeoutMs) {
    if (!IsConnected() || request.empty() || request.size() > kMaxMessageBytes) return false;
    DWORD deadline = GetTickCount() + timeoutMs;
    bool ok = WriteAll(Frame(request), deadline);
    if (ok && response) {
        char header[4];
        ok = ReadExact(header, 4, deadline);
        if (ok) {
            std::uint32_t len = DecodeLength(std::string(header, 4));
            ok = len > 0 && len <= kMaxMessageBytes;
            if (ok) {
                response->assign(len, '\0');
                ok = ReadExact(response->data(), len, deadline);
            }
        }
    }
    if (!ok) Close();
    return ok;
}

bool PipeClient::Post(std::string_view message) {
    if (!IsConnected() || message.empty() || message.size() > kMaxMessageBytes) return false;
    if (postPending_) {
        DWORD n = 0;
        if (!GetOverlappedResult(pipe_, &postOv_, &n, FALSE)) {
            if (GetLastError() == ERROR_IO_INCOMPLETE) return false;  // still in flight: skip this one
            Close();
            return false;
        }
        postPending_ = false;
    }
    postBuf_ = Frame(message);
    postOv_ = OVERLAPPED{};
    postOv_.hEvent = postEvent_;
    ResetEvent(postEvent_);
    if (!WriteFile(pipe_, postBuf_.data(), static_cast<DWORD>(postBuf_.size()), nullptr, &postOv_)) {
        if (GetLastError() != ERROR_IO_PENDING) { Close(); return false; }
    }
    postPending_ = true;
    return true;
}

bool Call(const std::wstring& pipeName, std::string_view request, std::string* response, DWORD timeoutMs) {
    PipeClient c;
    DWORD start = GetTickCount();
    if (!c.Connect(pipeName, timeoutMs)) return false;
    DWORD left = timeoutMs > (GetTickCount() - start) ? timeoutMs - (GetTickCount() - start) : 1;
    return c.Request(request, response, left);
}

std::string MakeMessage(const char* kind, const char* type, std::int64_t id, const json::Value& payload, int status,
                        const std::string& error) {
    json::Value m;
    m.Set("v", static_cast<int>(kProtocolVersion));
    m.Set("kind", kind);
    if (id >= 0) m.Set("id", id);
    m.Set("type", type);
    if (status >= 0) m.Set("status", status);
    if (!error.empty()) m.Set("error", error);
    if (!payload.IsNull()) m.Set("payload", payload);
    return m.Dump();
}

}  // namespace velora::ipc
