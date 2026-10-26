# LumaNudge for Windows

LumaNudge is a small native Windows utility that adjusts external monitors when the source brightness changes, for laptops with external displays and desktop setups with multiple monitors. It targets x64 and ARM64 Windows 10/11 and uses DDC/CI brightness control (VCP `0x10`). It applies the same **change in brightness** to each external monitor, preserving their brightness differences until a monitor reaches its minimum or maximum.

[简体中文](README.md)

**[Download LumaNudge.exe](../../raw/refs/heads/main/LumaNudge.exe)** · [Build guide](docs/BUILD.md) · [Compatibility and troubleshooting](docs/COMPATIBILITY.md)

> Status: working prototype. The x64 build has been tested on Windows hardware. The ARM64 build has been cross compiled but not tested on an ARM64 PC.

## How it works

- On a laptop, Windows WMI supplies the built in panel brightness and brightness change events.
- If no built in brightness source is available, the primary DDC/CI monitor becomes the source.
- When the source changes, the utility reads each target monitor's current brightness, applies the same percentage point change, clamps the result to `0–100%`, and sends it over DDC/CI.
- For example, if the source moves from `40%` to `45%`, external monitors at `20%` and `60%` move to `25%` and `65%`.
- Startup and reconnection establish a new baseline without writing brightness. The next source change triggers the next write.

**All readable external monitors are targets.** This version does not filter by manufacturer, model, or serial number.

## Getting started

1. Download **[LumaNudge.exe](../../raw/refs/heads/main/LumaNudge.exe)** from this repository. Alternatively, open the [file page](LumaNudge.exe) and select **Download raw file**. For a private repository, sign in to a GitHub account with access first.
2. Run `LumaNudge.exe` on Windows. The universal launcher selects the matching native payload for x64 or ARM64; Python and .NET are not required. Choose `5` to inspect the detected monitors, then choose `1` to install and start the scheduled task for your user account. Administrator rights are not required.
3. Open **Brightness Sync (DDC-CI)** from the Start menu to pause (`2`), resume (`3`), uninstall (`4`), or list monitors (`5`).

> The downloadable file is named `LumaNudge.exe`. The current application interface, Start menu shortcut, and internal installation path still use the **Brightness Sync / BrightnessSync** name.

Command-line options:

| Option | Action |
| --- | --- |
| `--menu` | Open the interactive menu; the default when double-clicked |
| `--list` | List monitors and their brightness reading status |
| `--install` | Install and start the background task for the current user |
| `--stop` | Pause the background task |
| `--start` | Resume the background task |
| `--status` | Show task status |
| `--uninstall` | Remove the task, shortcut, and installed files |

The internal `--run` option is used by Task Scheduler. The installed executable is `%USERPROFILE%\.BrightnessSync\BrightnessSync.exe`. To upgrade, pause the old version, wait for its background process to exit, then install the new version.

To install or pause from the command line, open PowerShell in the EXE's directory:

```powershell
.\LumaNudge.exe --install
.\LumaNudge.exe --stop
```

## Building from source

Prepare Windows, PowerShell 5.1 or newer, and Zig, then follow the [build guide](docs/BUILD.md):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Zig 'C:\path\to\zig.exe'
```

The current script creates x64, ARM64, and universal executables in `dist/`. The universal output is still named `BrightnessSync-native-universal.exe`; it can be renamed to `LumaNudge.exe` for distribution. Git ignores `dist/`; the repository root contains the downloadable `LumaNudge.exe`.

## Reconnection

When targets are disconnected, a sleeping background process listens for display configuration and device changes. It retries once a second for a short period after a change, then falls back to a lightweight check every five seconds. Reconnecting a display only resumes monitoring; brightness changes on the next source change.

## Requirements and limitations

- The monitor and connection path must support DDC/CI, and the feature may need to be enabled in the monitor's OSD. Some docks and adapters block it.
- The reported brightness value is a monitor setting, not measured luminance in nits.
- There is no model filter. Each external target receives the same change relative to its own current brightness.
- The ARM64 payload has not been tested on ARM64 hardware. The universal launcher is a 32-bit x86 program, which Windows can run on x64 and ARM64 systems before it starts the matching native payload.
- The executables are unsigned. Sign them and validate your target hardware before product distribution.
- This is a user mode utility. It does not contain a monitor driver, Windows Update distribution package, or vendor specific DDC/CI commands.

There is no network communication or telemetry in the source. See the [architecture](docs/ARCHITECTURE.md), [build guide](docs/BUILD.md), [compatibility guide](docs/COMPATIBILITY.md), [test plan](docs/TESTING.md), and [release checklist](docs/RELEASING.md).

## License

[MIT License](LICENSE) © 2026 Shiping.
