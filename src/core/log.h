// Tiny append-only file log: <DataDir>\logs\<name>.log, rotated at 1 MB.
// Owner: A. Never throws; logging failures are silent by design.
#pragma once

#include <string>
#include <string_view>

namespace velora::log {

enum class Level { Info, Warn, Error };

// Call once per process; later calls are ignored.
void Init(std::string_view processName);

void Write(Level level, std::string_view message);
// What a log line may contain: control characters (CR, LF, ESC, ...) become spaces so that text from outside (a path, a window
// class, a query) can never forge or split a log line.
std::string Sanitize(std::string_view message);

inline void Info(std::string_view m) { Write(Level::Info, m); }
inline void Warn(std::string_view m) { Write(Level::Warn, m); }
inline void Error(std::string_view m) { Write(Level::Error, m); }

}  // namespace velora::log
