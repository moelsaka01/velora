#include "core/contracts/events.h"
#include "core/contracts/ipc.h"
#include "core/contracts/module.h"
#include "core/contracts/state.h"

// VELORA_IPC_JSON_VERSION is read from ipc/v1.json by CMake.
static_assert(velora::ipc::kProtocolVersion == VELORA_IPC_JSON_VERSION,
              "ipc/v1.json and ipc.h disagree on the protocol version");
static_assert(velora::ipc::kHeartbeatTimeoutMs > velora::ipc::kHeartbeatIntervalMs);

int main() {
    velora::SwitchState s;
    return s.schemaVersion == velora::kStateSchemaVersion && s.monitors.empty() ? 0 : 1;
}
