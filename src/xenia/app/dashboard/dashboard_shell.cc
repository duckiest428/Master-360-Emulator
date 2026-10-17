/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/dashboard/dashboard_shell.h"

#include <cstdlib>
#include <string>

#include "xenia/app/dashboard/game_library.h"
#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/config.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/ui/file_picker.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

DEFINE_path(dashboard_shell_path, "",
            "Dashboard front end started when Xenia runs without a title. "
            "Empty: use DashX360\\DashX360.exe next to xenia_canary.exe if it "
            "exists, otherwise Xenia's built-in dashboard.",
            "Dashboard");

DECLARE_bool(dashboard);
DECLARE_path(dashboard_library_path);
DECLARE_path(target);

namespace xe {
namespace app {
namespace dashboard {

namespace {

bool EnvIsSet(const char* name) {
  const char* value = std::getenv(name);
  return value && value[0] && value[0] != '0';
}

bool StartShell(const std::filesystem::path& shell,
                const std::filesystem::path& library_json) {
  const std::filesystem::path xenia_exe = xe::filesystem::GetExecutablePath();
#if XE_PLATFORM_WIN32
  // Children inherit the environment: the shell finds Xenia and the library
  // through these, and passes XENIA_DASHBOARD_SHELL when launching titles.
  SetEnvironmentVariableW(L"XENIA_EXE", xenia_exe.wstring().c_str());
  SetEnvironmentVariableW(L"XENIA_LIBRARY_JSON",
                          library_json.wstring().c_str());
  std::wstring command_line = L"\"" + shell.wstring() + L"\"";
  const std::wstring working_dir = shell.parent_path().wstring();
  STARTUPINFOW startup_info = {};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info = {};
  if (!CreateProcessW(shell.c_str(), command_line.data(), nullptr, nullptr,
                      FALSE, 0, nullptr, working_dir.c_str(), &startup_info,
                      &process_info)) {
    XELOGE("Dashboard: unable to start shell {} (error {})",
           xe::path_to_utf8(shell), GetLastError());
    return false;
  }
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  return true;
#else
  (void)xenia_exe;
  (void)library_json;
  XELOGW("Dashboard: external shells are only supported on Windows");
  return false;
#endif
}

}  // namespace

bool IsLaunchedFromShell() { return EnvIsSet(kShellEnvLaunchedFromShell); }

std::filesystem::path FindShellExecutable() {
  std::error_code ec;
  if (!cvars::dashboard_shell_path.empty()) {
    return std::filesystem::is_regular_file(cvars::dashboard_shell_path, ec)
               ? cvars::dashboard_shell_path
               : std::filesystem::path();
  }
  const auto candidate =
      xe::filesystem::GetExecutableFolder() / "DashX360" / "DashX360.exe";
  return std::filesystem::is_regular_file(candidate, ec)
             ? candidate
             : std::filesystem::path();
}

std::filesystem::path FindShellAsset(const std::filesystem::path& relative) {
  const std::filesystem::path shell = FindShellExecutable();
  if (shell.empty()) {
    return {};
  }
  std::error_code ec;
  const auto path = shell.parent_path() / relative;
  return std::filesystem::exists(path, ec) ? path : std::filesystem::path();
}

std::string ShellGamertag() {
  const char* value = std::getenv(kShellEnvGamertag);
  return value ? std::string(value) : std::string();
}

std::filesystem::path LibraryJsonPath(const std::filesystem::path& cache_root) {
  return cache_root / "dashboard" / "library.json";
}

std::filesystem::path ProfileJsonPath(const std::filesystem::path& cache_root) {
  return cache_root / "dashboard" / "profile.json";
}

bool HandOffToShellIfNeeded(const std::filesystem::path& cache_root) {
  const bool export_only = EnvIsSet(kShellEnvExportOnly);
  if (!export_only) {
    if (!cvars::dashboard || IsLaunchedFromShell() || !cvars::target.empty()) {
      return false;
    }
    // A title asked to be relaunched: let Xenia boot it.
    std::error_code ec;
    if (std::filesystem::exists(
            std::string(kernel::xam::kXamModuleLoaderDataFileName), ec)) {
      return false;
    }
  }
  const std::filesystem::path shell = FindShellExecutable();
  if (!export_only && shell.empty()) {
    return false;
  }

  if (cvars::dashboard_library_path.empty() && !export_only) {
    // First run: ask for the game folder once, before the shell starts.
    auto picker = xe::ui::FilePicker::Create();
    picker->set_mode(ui::FilePicker::Mode::kOpen);
    picker->set_type(ui::FilePicker::Type::kDirectory);
    picker->set_multi_selection(false);
    picker->set_title("Select the folder that holds your Xbox 360 games");
    if (picker->Show(nullptr) && !picker->selected_files().empty()) {
      SetLibraryPath(picker->selected_files().front());
    }
  }

  const std::filesystem::path library_json = LibraryJsonPath(cache_root);
  GameLibrary library(cache_root / "dashboard");
  if (!cvars::dashboard_library_path.empty()) {
    library.StartScan(cvars::dashboard_library_path);
    library.WaitForScan();
  }
  if (!library.ExportJson(library_json)) {
    XELOGE("Dashboard: unable to write {}", xe::path_to_utf8(library_json));
  }
  if (export_only) {
    XELOGI("Dashboard: exported game library to {}",
           xe::path_to_utf8(library_json));
    return true;
  }
  if (!StartShell(shell, library_json)) {
    return false;
  }
  XELOGI("Dashboard: handed off to {}", xe::path_to_utf8(shell));
  return true;
}

}  // namespace dashboard
}  // namespace app
}  // namespace xe
