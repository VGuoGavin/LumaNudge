# 构建说明

## 环境

- Windows 10/11，PowerShell 5.1 或更新版本。
- Zig 0.17.0。当前源码和脚本已使用该版本编译 x64、ARM64 及通用启动器。请从 [Zig 官方下载页](https://ziglang.org/download/)获取 Zig，或将现有 `zig.exe` 的路径传给脚本。
- 不需要 Visual Studio、.NET SDK 或 PowerShell 运行时依赖来运行生成的 EXE。

## 一键构建

在项目根目录执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Zig 'C:\path\to\zig.exe'
```

如果 `zig` 已在 `PATH` 中，可省略 `-Zig`。脚本依次生成：

| 文件 | 用途 |
| --- | --- |
| `dist/BrightnessSync-x64.exe` | x64 原生程序 |
| `dist/BrightnessSync-arm64.exe` | ARM64 原生程序 |
| `dist/BrightnessSync-native-universal.exe` | 同时包含两个架构的单文件启动器 |

`dist/` 被 `.gitignore` 忽略，适合将源码提交到 GitHub，并把 EXE 作为 GitHub Release 附件发布。

构建流程先分别交叉编译 `src/brightness_sync_native.cpp`，再把两个 EXE 作为 RCDATA 嵌入 `src/brightness_sync_bundle.rc`，最后把 `src/brightness_sync_launcher.c` 编译为 32 位 x86 启动器。x86 启动器可在 x64 Windows，以及支持 x86 仿真的 Windows 10/11 ARM64 上运行。构建脚本会把 RC 文件复制到 `dist/`，保证相对资源路径指向刚生成的两个 EXE。

## 只读冒烟测试

`--list` 只读取显示器信息，不写入亮度。程序使用 Windows GUI 子系统，PowerShell 直接运行时不一定显示输出；可重定向标准输出：

```powershell
$p = Start-Process -FilePath .\dist\BrightnessSync-native-universal.exe `
  -ArgumentList '--list' -Wait -PassThru `
  -RedirectStandardOutput .\dist\list.txt
Get-Content .\dist\list.txt
"ExitCode=$($p.ExitCode)"
```

检查显示器名称、亮度百分比与“可同步目标数”是否合理。实际亮度同步需要在支持 DDC/CI 的外屏上测试。ARM64 程序必须在 ARM64 Windows 上完成运行验证；交叉编译成功只能证明构建通过。

## 发布前检查

1. 在 x64 和 ARM64 真机上分别测试安装、暂停、继续、卸载、亮度变化和拔插显示器。
2. 测试多台外屏、DDC/CI 关闭、不同接口与扩展坞等场景。
3. 对将要发布的 EXE 做代码签名，并记录 SHA-256：`Get-FileHash .\dist\BrightnessSync-native-universal.exe -Algorithm SHA256`。
4. 上传三个 EXE 到 GitHub Release；通用 EXE 供普通用户下载，分架构 EXE 供测试与故障排查。

