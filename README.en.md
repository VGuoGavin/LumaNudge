# BrightnessSync for Windows

A small native Windows utility that adjusts external monitors when the source brightness changes. It supports x64 and ARM64 Windows 10/11 and uses DDC/CI brightness control (VCP `0x10`). Each external monitor keeps its own brightness offset.

[简体中文](README.md)

> Status: working prototype. The x64 build has been tested on Windows hardware. The ARM64 build has been cross compiled but not tested on an ARM64 PC.

## How it works

- On a laptop, Windows WMI supplies the built in panel brightness and brightness change events.
- If no built in brightness source is available, the primary DDC/CI monitor becomes the source.
- When the source changes, the utility reads each target monitor's current brightness, applies the same percentage point change, clamps the result to `0–100%`, and sends it over DDC/CI.
- For example, if the source moves from `40%` to `45%`, external monitors at `20%` and `60%` move to `25%` and `65%`.
- Startup and reconnection establish a new baseline without writing brightness. The next source change triggers the next write.

**All readable external monitors are targets.** This version does not filter by manufacturer, model, or serial number.

## Getting started

1. Download `BrightnessSync-native-universal.exe` from this repository's GitHub Releases, or [build from source](docs/BUILD.md). The single file selects the matching native payload for x64 or ARM64 Windows.
2. Run it and choose `5` to inspect the detected monitors. Then choose `1` to install and start. Administrator rights are not required.
3. Open **Brightness Sync (DDC-CI)** from the Start menu to pause (`2`), resume (`3`), uninstall (`4`), or list monitors (`5`).

CLI options: `--menu`, `--list`, `--install`, `--stop`, `--start`, `--status`, and `--uninstall`. The internal `--run` option is used by Task Scheduler. The installed executable is `%USERPROFILE%\.BrightnessSync\BrightnessSync.exe`. To upgrade, pause the old version, wait for its background process to exit, then install the new version.

## Reconnection

When targets are disconnected, a sleeping background process listens for display configuration and device changes. It retries once a second for a short period after a change, then falls back to a lightweight check every five seconds. Reconnecting a display only resumes monitoring; brightness changes on the next source change.

## Requirements and limitations

- The monitor and connection path must support DDC/CI, and the feature may need to be enabled in the monitor's OSD. Some docks and adapters block it.
- The reported brightness value is a monitor setting, not measured luminance in nits.
- The ARM64 payload has not been tested on ARM64 hardware. The universal launcher is a 32-bit x86 program, which Windows can run on x64 and ARM64 systems before it starts the matching native payload.
- The executables are unsigned. Sign them and validate your target hardware before product distribution.
- This is a user mode utility. It does not contain a monitor driver, Windows Update distribution package, or vendor specific DDC/CI commands.

There is no network communication or telemetry in the source. See the [architecture](docs/ARCHITECTURE.md), [build guide](docs/BUILD.md), [compatibility guide](docs/COMPATIBILITY.md), [test plan](docs/TESTING.md), and [release checklist](docs/RELEASING.md).

## License

[MIT License](LICENSE) © 2026 Shiping.
