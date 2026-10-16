/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_DASHBOARD_DASHBOARD_SHELL_H_
#define XENIA_APP_DASHBOARD_DASHBOARD_SHELL_H_

#include <filesystem>

namespace xe {
namespace app {
namespace dashboard {

// Integration with an external dashboard front end (DashX360 by ZivvoZ,
// https://github.com/ZivvoZ/dashx360). When present, Xenia hands the home
// screen to it: started without a title, Xenia scans the game library,
// exports it as JSON, starts the shell and exits. The shell launches games
// back through Xenia with XENIA_DASHBOARD_SHELL=1 set, in which case leaving
// the title (Xbox Home in the Guide, or the title exiting) just closes Xenia
// so the shell comes back.

// Environment variables shared with the shell.
inline constexpr char kShellEnvLaunchedFromShell[] = "XENIA_DASHBOARD_SHELL";
inline constexpr char kShellEnvExportOnly[] = "XENIA_DASHBOARD_EXPORT_ONLY";
inline constexpr char kShellEnvXeniaExe[] = "XENIA_EXE";
inline constexpr char kShellEnvLibraryJson[] = "XENIA_LIBRARY_JSON";

// Stores the game library folder in the config (defined with the cvar).
void SetLibraryPath(const std::filesystem::path& path);

// True when this process was started by the shell to run a title.
bool IsLaunchedFromShell();

// The shell executable, or empty if none is installed.
std::filesystem::path FindShellExecutable();

// Path of the exported library JSON under the given cache root.
std::filesystem::path LibraryJsonPath(const std::filesystem::path& cache_root);

// Called early during startup, before any window exists. Returns true when
// this process should exit immediately because the shell has taken over (or
// a library export requested by the shell finished).
bool HandOffToShellIfNeeded(const std::filesystem::path& cache_root);

}  // namespace dashboard
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_DASHBOARD_DASHBOARD_SHELL_H_
