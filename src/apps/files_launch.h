// Starting velora-files.exe (an on-demand app) from the shell: the open_files IPC request, Search's "reveal in Files" and the
// Dock. Owner: C.
#pragma once

#include <string>

namespace velora::apps {

// The command line for velora-files.exe: `--select "<select>"` when `select` is set (Files opens its folder and highlights it),
// else the folder `path` (nothing = the user's home). Arguments are quoted so a trailing backslash ("C:\") survives.
std::wstring FilesCommandLine(const std::wstring& exe, const std::wstring& path, const std::wstring& select);

// Starts <folder of this exe>\velora-files.exe. False (with `error`) when it is missing or cannot be started. A running Files
// window takes the request over (a second launch forwards it), so calling this twice never opens two windows.
bool LaunchFilesApp(const std::wstring& path, const std::wstring& select, std::wstring* error = nullptr);

}  // namespace velora::apps
