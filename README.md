# LumaNudge for Windows

[简体中文](#简体中文) | [English](#english)

## 简体中文

LumaNudge 是一个让外接显示器跟随 Windows 亮度变化的轻量级原生程序，适用于连接外屏的笔记本和多显示器桌面环境。面向 x64 和 ARM64 Windows 10/11，使用显示器的 DDC/CI 亮度控制（VCP `0x10`）。程序按**亮度变化量**调整各外屏，在未触及亮度上下限时保留每台外屏原有的亮度差。

**[下载 LumaNudge.exe](../../raw/refs/heads/main/LumaNudge.exe)** · [构建说明](docs/BUILD.md) · [兼容性与排查](docs/COMPATIBILITY.md)

> 项目现状：可运行的实验版。x64 已在 Windows 实机测试；ARM64 已交叉编译，尚未在 ARM64 电脑上实机验证。

### 工作方式

- 笔记本：使用 Windows WMI 读取内屏亮度，并监听亮度变化事件。
- 没有可用内屏亮度时：使用可读取 DDC/CI 亮度的主显示器作为来源。
- 来源亮度变化时，先读取**每台**目标外屏的当前亮度，再加上相同的百分点变化量；结果限制在 `0–100%`。
- 例如内屏 `40% → 45%`，两台外屏原本为 `20%` 和 `60%`，调整后分别为 `25%` 和 `65%`。
- 首次启动或外屏重新连接时，只建立亮度基准。要等来源亮度下一次变化，才写入外屏。来源亮度不变时，不发送 DDC/CI 亮度写入命令。

当前版本会控制**所有能读取亮度的外屏**，没有品牌、型号或序列号筛选。请先确认你愿意让这些外屏都参与同步。

### 安装和使用

1. 下载仓库中的 **[LumaNudge.exe](../../raw/refs/heads/main/LumaNudge.exe)**。也可打开[文件页面](LumaNudge.exe)，点击 **Download raw file** 下载。
2. 在 Windows 上双击 `LumaNudge.exe`。通用启动器会选择 x64 或 ARM64 原生程序，无需安装 Python 或 .NET。输入 `5` 查看可读取的显示器；确认目标后输入 `1` 安装并开启。安装当前用户的任务计划，无需管理员权限。
3. 以后从开始菜单打开 **Brightness Sync (DDC-CI)**，可选择 `2` 暂停、`3` 继续、`4` 卸载、`5` 查看显示器。

> 对外下载文件名为 `LumaNudge.exe`。当前程序界面、开始菜单快捷方式和内部安装路径仍使用 **Brightness Sync / BrightnessSync** 名称。

命令行参数：

| 参数 | 功能 |
| --- | --- |
| `--menu` | 打开交互菜单，双击时默认执行 |
| `--list` | 列出显示器及其亮度读取状态 |
| `--install` | 安装并开启当前用户的后台任务 |
| `--stop` | 暂停后台任务 |
| `--start` | 恢复后台任务 |
| `--status` | 查看任务状态 |
| `--uninstall` | 移除任务、快捷方式和安装文件 |

后台任务使用内部参数 `--run`。安装文件位于 `%USERPROFILE%\.BrightnessSync\BrightnessSync.exe`。升级时先暂停旧版，等后台进程退出，再运行新版选择安装。

在 EXE 所在目录打开 PowerShell，也可通过命令行安装或暂停：

```powershell
.\LumaNudge.exe --install
.\LumaNudge.exe --stop
```

### 从源码构建

准备 Windows、PowerShell 5.1 或更新版本及 Zig，按[构建说明](docs/BUILD.md)执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Zig 'C:\path\to\zig.exe'
```

当前脚本在 `dist/` 下生成 x64、ARM64 和通用 EXE；通用文件名仍为 `BrightnessSync-native-universal.exe`，分发时可重命名为 `LumaNudge.exe`。`dist/` 不纳入 Git，仓库根目录保留供下载的 `LumaNudge.exe`。

### 热插拔与资源占用

外屏移除后，程序停止同步，保留一个休眠的后台进程等待显示器变化通知。重连时优先由系统消息唤醒；连接建立期间短时间每秒重试，之后每 5 秒做一次轻量兜底检查。它不会在重连时直接覆盖外屏亮度。

### 适用条件与限制

- 外屏和连接链路必须支持 DDC/CI；显示器 OSD 中可能需要开启该功能。某些转接器、扩展坞或显示器不支持。
- DDC/CI 返回的是显示器报告的亮度设置值，不是实测亮度（nits）。
- 当前版本没有型号筛选；多台外屏会分别按自己的当前亮度应用变化量。
- ARM64 版尚无实机验证；通用启动器是 32 位 x86 程序，可通过 Windows 的兼容支持在 x64 和 ARM64 系统上运行，然后启动相应架构的原生后台程序。
- 仓库中的 `LumaNudge.exe` 已使用本地自签名证书签名，并附带 DigiCert RFC 3161 时间戳。签名仅覆盖外层通用启动器；内嵌的 x64 / ARM64 程序仍未单独签名。自行构建的文件也不会自动签名。
- 自签名证书不会被其他电脑默认信任，也不能消除 SmartScreen 提示。正式公开分发时，应使用受信任的代码签名方案并完成目标机型测试。
- 这是用户态工具，不包含显示器驱动、Windows Update 自动分发或厂家专用 DDC/CI 命令。

源码不包含网络通信或遥测功能。技术细节见[设计说明](docs/ARCHITECTURE.md)，构建方法见[构建文档](docs/BUILD.md)，兼容性和排查方法见[兼容性文档](docs/COMPATIBILITY.md)。发布前请按[测试方案](docs/TESTING.md)验证，并参考[GitHub 发布清单](docs/RELEASING.md)。

### 签名与文件校验

签名者名称为 `LumaNudge (Self-Signed)`，这是本地自签名标识，并非认证机构验证过的发布者身份。[公开证书](certs/LumaNudge-self-signed.cer)可用于检查签名；私钥和密码仅保存在本机，不纳入 Git。DigiCert 提供的是时间戳服务，不是本项目的代码签名证书。

下载后，可在 Windows PowerShell 中检查签名信息和 SHA-256，并与仓库中的 [SHA256SUMS](SHA256SUMS) 对照：

```powershell
Get-AuthenticodeSignature .\LumaNudge.exe | Format-List Status, StatusMessage, SignerCertificate, TimeStamperCertificate
Get-FileHash .\LumaNudge.exe -Algorithm SHA256
```

在尚未信任该自签名证书的电脑上，签名状态可能显示为不受信任。这不等同于文件未签名。当前签名及时间戳已使用 `osslsigncode` 验证，验证时显式信任了上述公开证书；尚未在 Windows 上复测签名后的程序。

### 许可证

[MIT License](LICENSE) © 2026 Shiping。

---

## English

LumaNudge is a small native Windows utility that adjusts external monitors when the source brightness changes, for laptops with external displays and desktop setups with multiple monitors. It targets x64 and ARM64 Windows 10/11 and uses DDC/CI brightness control (VCP `0x10`). It applies the same **change in brightness** to each external monitor, preserving their brightness differences until a monitor reaches its minimum or maximum.

**[Download LumaNudge.exe](../../raw/refs/heads/main/LumaNudge.exe)** · [Build guide](docs/BUILD.md) · [Compatibility and troubleshooting](docs/COMPATIBILITY.md)

> Status: working prototype. The x64 build has been tested on Windows hardware. The ARM64 build has been cross compiled but not tested on an ARM64 PC.

### How it works

- On a laptop, Windows WMI supplies the built in panel brightness and brightness change events.
- If no built in brightness source is available, the primary DDC/CI monitor becomes the source.
- When the source changes, the utility reads each target monitor's current brightness, applies the same percentage point change, clamps the result to `0–100%`, and sends it over DDC/CI.
- For example, if the source moves from `40%` to `45%`, external monitors at `20%` and `60%` move to `25%` and `65%`.
- Startup and reconnection establish a new baseline without writing brightness. The next source change triggers the next write.

**All readable external monitors are targets.** This version does not filter by manufacturer, model, or serial number.

### Getting started

1. Download **[LumaNudge.exe](../../raw/refs/heads/main/LumaNudge.exe)** from this repository. Alternatively, open the [file page](LumaNudge.exe) and select **Download raw file**.
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

### Building from source

Prepare Windows, PowerShell 5.1 or newer, and Zig, then follow the [build guide](docs/BUILD.md):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Zig 'C:\path\to\zig.exe'
```

The current script creates x64, ARM64, and universal executables in `dist/`. The universal output is still named `BrightnessSync-native-universal.exe`; it can be renamed to `LumaNudge.exe` for distribution. Git ignores `dist/`; the repository root contains the downloadable `LumaNudge.exe`.

### Reconnection

When targets are disconnected, a sleeping background process listens for display configuration and device changes. It retries once a second for a short period after a change, then falls back to a lightweight check every five seconds. Reconnecting a display only resumes monitoring; brightness changes on the next source change.

### Requirements and limitations

- The monitor and connection path must support DDC/CI, and the feature may need to be enabled in the monitor's OSD. Some docks and adapters block it.
- The reported brightness value is a monitor setting, not measured luminance in nits.
- There is no model filter. Each external target receives the same change relative to its own current brightness.
- The ARM64 payload has not been tested on ARM64 hardware. The universal launcher is a 32-bit x86 program, which Windows can run on x64 and ARM64 systems before it starts the matching native payload.
- The repository's `LumaNudge.exe` has a local self-signed signature and a DigiCert RFC 3161 timestamp. Only the outer universal launcher is signed; the embedded x64 / ARM64 programs remain individually unsigned. Local builds are not signed automatically.
- Other computers do not trust this self-signed certificate by default, and it does not remove SmartScreen warnings. Use a trusted code signing solution and validate target hardware for production distribution.
- This is a user mode utility. It does not contain a monitor driver, Windows Update distribution package, or vendor specific DDC/CI commands.

There is no network communication or telemetry in the source. See the [architecture](docs/ARCHITECTURE.md), [build guide](docs/BUILD.md), [compatibility guide](docs/COMPATIBILITY.md), [test plan](docs/TESTING.md), and [release checklist](docs/RELEASING.md).

### Signature and file verification

The signer is `LumaNudge (Self-Signed)`, a local signing identity rather than a publisher identity verified by a certificate authority. The [public certificate](certs/LumaNudge-self-signed.cer) is available for signature inspection; the private key and password remain local and are excluded from Git. DigiCert provides the timestamp service, not this project's code signing certificate.

After downloading, inspect the signature and SHA-256 in Windows PowerShell, then compare the hash with [SHA256SUMS](SHA256SUMS):

```powershell
Get-AuthenticodeSignature .\LumaNudge.exe | Format-List Status, StatusMessage, SignerCertificate, TimeStamperCertificate
Get-FileHash .\LumaNudge.exe -Algorithm SHA256
```

A computer that does not trust this self-signed certificate may report an untrusted signature. This does not mean the file is unsigned. The signature and timestamp were verified with `osslsigncode`, explicitly trusting the public certificate above; the signed application has not yet been retested on Windows.

### License

[MIT License](LICENSE) © 2026 Shiping.
