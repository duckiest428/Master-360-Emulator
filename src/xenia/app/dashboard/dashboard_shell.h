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

#include <cstdint>
#include <filesystem>
#include <string>

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
// Gamertag of the profile the shell has signed in. Xenia signs the same
// profile in (creating it on first use) so achievements count towards it.
inline constexpr char kShellEnvGamertag[] = "XENIA_DASHBOARD_GAMERTAG";
// Process ID of the shell, which draws the Xbox Guide over the title.
inline constexpr char kShellEnvShellPid[] = "XENIA_DASHBOARD_SHELL_PID";

// The shell's Xbox Guide, shown over the title. Xenia keeps the Guide button
// and the title's pause; the shell draws the Guide and reports back through
// named events (Local\XeniaDashboard.<xenia pid>.<name>):
//   ToggleGuide  (auto-reset, set by Xenia) - open or close the Guide;
//   GuideVisible (manual-reset, set by the shell) - the Guide is on screen;
//   Quit         (auto-reset, set by the shell) - Xbox Home: close the title;
//   SignOut      (auto-reset, set by the shell) - sign the profile out.
class ShellGuideLink {
 public:
  ShellGuideLink() = default;
  ~ShellGuideLink();
  ShellGuideLink(const ShellGuideLink&) = delete;
  ShellGuideLink& operator=(const ShellGuideLink&) = delete;

  // Connects to the shell named by XENIA_DASHBOARD_SHELL_PID.
  bool Open();
  // True while the shell process is running.
  bool IsAvailable() const;
  void ToggleGuide();
  bool IsGuideVisible() const;
  // Each returns true once per request.
  bool TakeQuitRequest();
  bool TakeSignOutRequest();

 private:
  uint32_t shell_pid_ = 0;
  void* shell_process_ = nullptr;
  void* toggle_event_ = nullptr;
  void* visible_event_ = nullptr;
  void* quit_event_ = nullptr;
  void* sign_out_event_ = nullptr;
};

// Stores the game library folder in the config (defined with the cvar).
void SetLibraryPath(const std::filesystem::path& path);

// True when this process was started by the shell to run a title.
bool IsLaunchedFromShell();

// The shell executable, or empty if none is installed.
std::filesystem::path FindShellExecutable();

// A file or folder that ships with the shell, or empty if it doesn't exist.
std::filesystem::path FindShellAsset(const std::filesystem::path& relative);

// Gamertag passed by the shell, or empty.
std::string ShellGamertag();

// Path of the exported library JSON under the given cache root.
std::filesystem::path LibraryJsonPath(const std::filesystem::path& cache_root);

// Path of the signed-in profile's summary (gamertag, gamerscore, per-title
// achievement progress) that Xenia keeps up to date for the shell.
std::filesystem::path ProfileJsonPath(const std::filesystem::path& cache_root);

// Called early during startup, before any window exists. Returns true when
// this process should exit immediately because the shell has taken over (or
// a library export requested by the shell finished).
bool HandOffToShellIfNeeded(const std::filesystem::path& cache_root);

}  // namespace dashboard
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_DASHBOARD_DASHBOARD_SHELL_H_
