// state.json + dirty marker persistence (CLAUDE.md §4). Owner: A.
//
// Order matters: state.json is written (atomically) BEFORE the marker is
// created, so a marker always has a state file behind it. The marker is the
// single source of truth for "Velora Mode may be active / was not exited".
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "core/contracts/state.h"

namespace velora {

std::string SerializeState(const SwitchState& s);
// Tolerant: missing fields keep defaults; unknown fields are ignored (a newer
// schemaVersion still loads). Returns nullopt only for unparseable input.
std::optional<SwitchState> ParseState(std::string_view json);

// Atomic: temp file + flush + MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH).
bool SaveState(const SwitchState& s, const std::filesystem::path& path);
std::optional<SwitchState> LoadState(const std::filesystem::path& path);

bool CreateDirtyMarker(const std::filesystem::path& path);
bool DirtyMarkerExists(const std::filesystem::path& path);
bool RemoveDirtyMarker(const std::filesystem::path& path);  // true if absent afterwards

}  // namespace velora
