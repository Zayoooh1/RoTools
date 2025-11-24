# RoTools (MultiRoblox)

RoTools is a Windows desktop launcher that lets you run multiple Roblox accounts at once. It combines an account manager, a WebView2-based login dialog, automation helpers such as Auto Rejoin, and a self-update system connected to GitHub releases.

---

## Features

- **Multi-account launching** – store encrypted Roblox accounts locally and join any supported game with several profiles simultaneously.
- **Game selector** – combo box with predefined games (RCU, PS99, SAB, GAG, PvB); the app injects the hidden Place ID automatically.
- **Auto Rejoin timer** – configurable value (seconds/minutes/hours) saved between sessions via `JoinSettings.txt`.
- **Tooltip with instructions** – hover the info icon next to “PV Server Link” to see a screenshot tutorial.
- **WebView2 login dialog** – embedded Chromium window that captures `.ROBLOSECURITY` cookies and updates the account database.
- **Automatic updates** – checks `https://api.github.com/repos/Zayo/RoTools/releases/latest`, downloads new `.exe` builds, and restarts through a temporary BAT script.
- **Update Settings** – dialog with Everyday / Weekly / Monthly / Never frequency options.

---

## Requirements

- Windows 10/11 (x64).
- Visual Studio 2022 + CMake ≥ 3.20 (for building).
- Microsoft Edge WebView2 Runtime.
- Internet access for GitHub API when checking for updates.

---

## Repository Anatomy

| Path                      | Description                                                        |
|---------------------------|--------------------------------------------------------------------|
| `main.c`                  | Entry point, mutex handling, subsystem init/shutdown.              |
| `gui.c`, `gui.h`          | Win32 window, control layout, command processing.                  |
| `account_*`               | Account dialogs/manager/storage (DPAPI persistence).               |
| `account_webview.cpp`     | WebView2 environment + login/cookie interception.                  |
| `updater.c`, `updater.h`  | GitHub release polling, WinHTTP downloads, self-update logic.      |
| `resources.rc`            | Icon, dialogs (`IDD_ACCOUNT_DIALOG`, `IDD_UPDATE_SETTINGS`, etc.). |
| `assets/`                 | Icon, tooltip image, WebView2 diagnostic logs.                     |
| `external/webview2/`      | SDK headers + libs + WebView2Loader.dll (runtime installers ignored). |

`build/` and offline WebView2 runtime packages are ignored by Git; generate/install them locally.

---

## Building the Project

```powershell
cd C:\Users\Zayo\Desktop\RoTools
cmake -S . -B build
cmake --build build --config Release
```

Artifacts:
- `build\Release\MultiRoblox.exe`
- `build\Release\WebView2Loader.dll` (copied from `external\webview2\dll`)

### Installer (Inno Setup)

`Instalator.iss` copies:
- `MultiRoblox.exe`
- `WebView2Loader.dll`
- `assets\*`
- `external\WebView2Runtime\*` (offline runtime bundle)

---

## Using RoTools

1. **Launch** `MultiRoblox.exe`; the app enforces single instance and locks the Roblox mutex.
2. **Add accounts** via the “Add” button or WebView2 login. Data lives in `%LOCALAPPDATA%\RoTools\` (production) or `%LOCALAPPDATA%\RoTools_Dev\` (dev build) and is encrypted with DPAPI.
3. **Select accounts** using the checkbox column, choose a game from the combo box, optionally paste a private server link.
4. **Join** by pressing the “Join” button. Auto Rejoin restarts batches based on the configured interval if enabled.
5. **Update Settings** dialog lets you pick how often updates should be checked (Everyday / Weekly / Monthly / Never).
6. **Update prompts** show “New version available: vX.Y.Z”. Clicking **Update** triggers the download-and-restart flow; **Not now** simply closes the dialog and the next check occurs according to the schedule.

---

## Configuration & Data

| File / Location                             | Purpose                                                            |
|---------------------------------------------|--------------------------------------------------------------------|
| `%LOCALAPPDATA%\RoTools_\*/AccountData.dat` | Encrypted account database.                                        |
| `%LOCALAPPDATA%\RoTools_\*/WebView2\`       | User data for the embedded browser.                                |
| `JoinSettings.txt` (next to EXE)            | Saved game selection, PV link, Auto Rejoin toggle, timer, units.   |
| `UpdateSettings.txt` (next to EXE)          | `LastCheckTimestamp` + `Frequency`.                                |
| `DvlErrLog.txt`                             | Logging of errors (Win32, updater, WebView2 failures).             |

Delete `UpdateSettings.txt` to trigger an immediate check. Delete `JoinSettings.txt` to reset Auto Rejoin defaults.

---

## Automatic Update Mechanics

1. `Updater_BeginStartupCheck` spawns a background thread when the GUI is created.
2. The thread reads `UpdateSettings.txt`, applies the selected frequency (Everyday/Weekly/Monthly/Never), and compares timestamps.
3. If due, it calls GitHub Releases API, parses `tag_name` and `.exe` asset URLs, and compares versions against `APP_VERSION`.
4. The EXE is downloaded via WinHTTP to `%TEMP%\MultiRoblox_new.exe`; a BAT script replaces the running binary and relaunches the app.