# DashX360 integration

Xenia can use [DashX360](https://github.com/ZivvoZ/dashx360) as its home
screen. **DashX360 is created by ZivvoZ** — all credit for its dashboard,
interface, animations, sounds and artwork goes to ZivvoZ
([YouTube](https://youtube.com/@zivvoz), [Ko-fi](https://Ko-fi.com/zivvoz)).
It is not part of this repository and is not redistributed here.

`xenia-integration.patch` contains only the changes made to DashX360 for
Xenia:

- **Xbox 360 games** (`XeniaIntegration.cs`, `GameLaunchService.cs`,
  `GameCardViewModel.cs`): reads the game library Xenia exports, adds the games
  as "Xbox 360" games, launches them through `xenia_canary.exe` and brings
  DashX360 back to the front when the game closes.
- **Initial setup** (`X360SystemOverlay.cs`, `X360SetupScreens.cs`,
  `X360Ui.cs`): runs once, after the boot animation, like a new console:
  press (A), language, locale, content controls, network, applying settings,
  then profile creation (on-screen keyboard, gamer picture, optional Xbox LIVE
  sign-up screens). Settings > System > Initial Setup runs it again.
- **Network** (`WifiService.cs`): the network step and Network Settings list
  the PC's Wi-Fi networks through the Windows WLAN service, connect to them
  (creating a normal Windows Wi-Fi profile), and test the connection.
- **System Settings** (`X360SystemScreens.cs`): Settings > System opens the
  console's System Settings: Console Settings (display, audio, language and
  locale, clock, system info), Kinect, Storage, Network, Computers, TV, Xbox
  Live Vision and Initial Setup.
- **Profile**: the profile made during setup is the one Xenia signs in when it
  runs a game, so achievements unlocked in games add to its gamerscore, which
  DashX360 shows (social channel, top right).
- **Channels** (`RotatingTile.cs`, `SpotlightLibrary.cs`, `views/tabs/*.xaml`):
  the home, video and games tiles rotate through a library of about 100
  movies, TV shows, Xbox 360 games and albums from 2006-2014. Their artwork
  isn't shipped: DashX360 downloads each title's poster or cover from
  Wikipedia the first time it runs online and keeps it in
  `UserData\Spotlight`. The games channel's big tile shows your Xbox 360
  games and starts the one on screen. The music channel's sample albums are
  replaced by tiles for the songs in DashX360's music folder.
- **Xbox Guide over games** (`GuideWindow.*`, `GuideViewModel.cs`,
  `GuideStatusIndicator.cs`, `MainWindow.cs`): DashX360's own Guide is shown
  over a game running in Xenia, opened with the controller's Guide button (or
  Ctrl+G). The header shows the controller, its battery and the ring of
  light. While a game runs, Games & Apps lists Achievements (that game's, from
  Xenia), All Achievements, Awards, Recently Played, Game Marketplace and My
  Apps; X is Sign Out and Y is Xbox Home, both with a Yes/No confirmation.
  Gamerscore and achievements update on the dashboard as soon as Xenia
  records them. Ctrl+G also opens the Guide on the dashboard.

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
- DashX360 starts games with `XENIA_DASHBOARD_SHELL=1`, plus:
  - `XENIA_DASHBOARD_GAMERTAG`: Xenia signs in the profile with this gamertag,
    creating it the first time;
  - `XENIA_DASHBOARD_GAMERPIC`: a 64x64 PNG used as that profile's picture;
  - `XENIA_DASHBOARD_LANGUAGE`: the console language games use;
  - `XENIA_DASHBOARD_GUIDE_STYLE`: `metro` or `blades`, matching DashX360's
    Guide setting.
- While a game runs, Xenia keeps `cache_host/dashboard/profile.json` (gamertag,
  gamerscore and per-game achievement progress) up to date; DashX360 reads it
  when the game closes.
- Xenia's in-game Guide and its achievement and notification pop-ups use the
  console sounds and the Xbox orb that ship with DashX360
  (`Assets/Audio/Sounds`, `Assets/Misc/SignIn/xbox-orb.png`). Without DashX360
  they fall back to placeholder sounds and Xenia's own icons.
- In a game the Guide button, or Ctrl+G, is caught by Xenia, which asks
  DashX360 (`XENIA_DASHBOARD_SHELL_PID`) to show its Guide through named
  events (see `ShellGuideLink` in `src/xenia/app/dashboard/dashboard_shell.h`).
  Xenia pauses the game while the Guide is up. **Xbox Home** closes Xenia so
  DashX360 comes back. Without DashX360, Xenia's built-in Guide is used.
- Set `dashboard_shell_path` to use a different front end, or `dashboard =
  false` to disable all of this.
