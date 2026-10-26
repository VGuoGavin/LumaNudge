# LumaNudge for Windows

LumaNudge 是一个让外接显示器跟随 Windows 亮度变化的轻量级原生程序，适用于连接外屏的笔记本和多显示器桌面环境。面向 x64 和 ARM64 Windows 10/11，使用显示器的 DDC/CI 亮度控制（VCP `0x10`）。程序按**亮度变化量**调整各外屏，在未触及亮度上下限时保留每台外屏原有的亮度差。

[English README](README.en.md)

**[下载 LumaNudge.exe](../../raw/refs/heads/main/LumaNudge.exe)** · [构建说明](docs/BUILD.md) · [兼容性与排查](docs/COMPATIBILITY.md)

> 项目现状：可运行的实验版。x64 已在 Windows 实机测试；ARM64 已交叉编译，尚未在 ARM64 电脑上实机验证。

## 工作方式

- 笔记本：使用 Windows WMI 读取内屏亮度，并监听亮度变化事件。
- 没有可用内屏亮度时：使用可读取 DDC/CI 亮度的主显示器作为来源。
- 来源亮度变化时，先读取**每台**目标外屏的当前亮度，再加上相同的百分点变化量；结果限制在 `0–100%`。
- 例如内屏 `40% → 45%`，两台外屏原本为 `20%` 和 `60%`，调整后分别为 `25%` 和 `65%`。
- 首次启动或外屏重新连接时，只建立亮度基准。要等来源亮度下一次变化，才写入外屏。来源亮度不变时，不发送 DDC/CI 亮度写入命令。

当前版本会控制**所有能读取亮度的外屏**，没有品牌、型号或序列号筛选。请先确认你愿意让这些外屏都参与同步。

## 安装和使用

1. 下载仓库中的 **[LumaNudge.exe](../../raw/refs/heads/main/LumaNudge.exe)**。也可打开[文件页面](LumaNudge.exe)，点击 **Download raw file** 下载。私有仓库需要先登录有访问权限的 GitHub 账号。
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

## 从源码构建

准备 Windows、PowerShell 5.1 或更新版本及 Zig，按[构建说明](docs/BUILD.md)执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Zig 'C:\path\to\zig.exe'
```

当前脚本在 `dist/` 下生成 x64、ARM64 和通用 EXE；通用文件名仍为 `BrightnessSync-native-universal.exe`，分发时可重命名为 `LumaNudge.exe`。`dist/` 不纳入 Git，仓库根目录保留供下载的 `LumaNudge.exe`。

## 热插拔与资源占用

外屏移除后，程序停止同步，保留一个休眠的后台进程等待显示器变化通知。重连时优先由系统消息唤醒；连接建立期间短时间每秒重试，之后每 5 秒做一次轻量兜底检查。它不会在重连时直接覆盖外屏亮度。

## 适用条件与限制

- 外屏和连接链路必须支持 DDC/CI；显示器 OSD 中可能需要开启该功能。某些转接器、扩展坞或显示器不支持。
- DDC/CI 返回的是显示器报告的亮度设置值，不是实测亮度（nits）。
- 当前版本没有型号筛选；多台外屏会分别按自己的当前亮度应用变化量。
- ARM64 版尚无实机验证；通用启动器是 32 位 x86 程序，可通过 Windows 的兼容支持在 x64 和 ARM64 系统上运行，然后启动相应架构的原生后台程序。
- 发布的 EXE 尚未签名。作为产品分发前，应做代码签名和目标机型测试。
- 这是用户态工具，不包含显示器驱动、Windows Update 自动分发或厂家专用 DDC/CI 命令。

源码不包含网络通信或遥测功能。技术细节见[设计说明](docs/ARCHITECTURE.md)，构建方法见[构建文档](docs/BUILD.md)，兼容性和排查方法见[兼容性文档](docs/COMPATIBILITY.md)。发布前请按[测试方案](docs/TESTING.md)验证，并参考[GitHub 发布清单](docs/RELEASING.md)。

## 许可证

[MIT License](LICENSE) © 2026 Shiping。

