# DashX360 integration

Xenia can use [DashX360](https://github.com/ZivvoZ/dashx360) as its home
screen. **DashX360 is created by ZivvoZ** — all credit for its dashboard,
interface, animations, sounds and artwork goes to ZivvoZ
([YouTube](https://youtube.com/@zivvoz), [Ko-fi](https://Ko-fi.com/zivvoz)).
It is not part of this repository and is not redistributed here.

`xenia-integration.patch` contains only the changes needed for DashX360 to work
with Xenia:

- `XeniaIntegration.cs` (new): reads the Xbox 360 game library that Xenia
  exports, adds those games to DashX360 as "Xbox 360" games, launches them
  through `xenia_canary.exe`, and brings DashX360 back to the front when the
  game closes.
- `GameLaunchService.cs`: launches Xbox 360 games through Xenia.
- `DashboardViewModel.cs`: merges Xenia's library on startup, adds the
  "Scan Xbox 360 Games" setting, and does not minimize DashX360 when starting
  an Xbox 360 game.
- `MainWindow.cs`: DashX360's own Guide stays closed while a Xenia game is
  running (Xenia shows its in-game Guide instead).
- `MainWindow.xaml`: the "Scan Xbox 360 Games" button in Settings.
- `GameCardViewModel.cs`: labels Xbox 360 games as "Xbox 360".

## Setup

1. Download the DashX360 source into `dashx360-main/` at the root of this
   repository (the folder is git-ignored).
2. Apply the patch from inside that folder:

   ```
   cd dashx360-main
   git apply --ignore-whitespace ..\tools\dashx360\xenia-integration.patch
   ```

3. Run `build.bat` from the repository root. It builds Xenia and, when the
   .NET 8 SDK is installed, builds DashX360 into a `DashX360` folder next to
   `xenia_canary.exe`.

## How it works

- Starting `xenia_canary.exe` without a game scans the folder set in
  `dashboard_library_path` (asking for it on first run), writes
  `cache_host/dashboard/library.json`, starts DashX360 and exits.
- DashX360 starts games with `XENIA_DASHBOARD_SHELL=1`. In that mode the game
  runs full screen and choosing **Xbox Home** in the Guide (or the game exiting
  to the dashboard) closes Xenia so DashX360 comes back.
- Set `dashboard_shell_path` to use a different front end, or `dashboard =
  false` to disable all of this.
