# 设计说明

## 数据流

```mermaid
flowchart LR
    A[Windows 内屏 WMI 亮度\n或主显示器 DDC/CI 亮度] --> B[来源亮度变化检测]
    B --> C[计算百分点变化量]
    C --> D[逐台读取外屏当前亮度]
    D --> E[加上变化量并限制在 0–100%]
    E --> F[DDC/CI VCP 0x10 写入]
```

同步算法：

```text
delta = source_now_percent - source_previous_percent
target_new_percent = clamp(target_current_percent + delta, 0, 100)
```

例如来源 `40% → 45%`，目标外屏当前 `20%`，则写入 `25%`。每台目标都先读取自己的当前值，因此手动调整外屏 OSD 后，下一次同步会从手动值继续加减。首次启动、来源显示器切换、所有目标断开后重连时，只记录新的来源基准，不写入目标。

来源没有变化时，不调用 DDC/CI 写入接口。读取和连接状态检测仍会发生。如果目标当前值已处于 `0%` 或 `100%` 且变化量使目标继续越界，结果保持边界值，也不发送相同的原始亮度值。

如果一次变化中某台目标无法读取亮度，该目标会被跳过；来源基准仍会前进，程序不会在来源不变时补发这次变化。

## 来源和目标选择

1. 能从 `root\wmi` 的 `WmiMonitorBrightness` 读取活动内屏时，使用内屏百分比作为来源。`WmiMonitorBrightnessEvent` 提供变化通知，每秒读取一次作为兜底。
2. 没有可用内屏来源时，使用可读取 DDC/CI 亮度的主显示器作为来源；每 0.5 秒读取一次。若主显示器不可读，则选择首台可读显示器。
3. 其余非内屏、可读取亮度的显示器成为目标。每台目标独立计算变化量。

当前版本没有 EDID 序列号或厂商型号白名单。目标选择依赖本次 Windows 物理显示器枚举；同型号多台显示器可以分别控制，但程序不提供长期稳定的设备绑定。

## DDC/CI 读写

程序先尝试通过 `GetVCPFeatureAndVCPFeatureReply` 读取 VCP `0x10`，失败时尝试 `GetMonitorBrightness`。写入时使用对应的 `SetVCPFeature` 或 `SetMonitorBrightness`。显示器报告的原始范围会换算为百分比；这是显示器的亮度设置值，不是光度计测得的 nits。

物理显示器句柄由 `EnumDisplayMonitors`、`GetPhysicalMonitorsFromHMONITOR` 获取，并在重新扫描时释放。显示器或链路不支持 DDC/CI 时，该外屏不会成为目标。

## 后台任务和热插拔

- 安装创建当前用户的任务计划：登录时启动，之后每分钟尝试启动一次以便异常退出后恢复；命名互斥量阻止重复后台实例。
- 有目标时每 10 秒做常规显示器扫描。隐藏的顶层窗口接收 `WM_DISPLAYCHANGE` 和 `WM_DEVICECHANGE`，收到后立即重新扫描。
- 没有目标时，进程保留休眠监听。常规每 5 秒只检查 Windows 活动显示路径；确认存在外部路径才进行 DDC/CI 枚举。收到显示器变化消息后的 10 秒内，每秒重试一次，让刚接入的显示器有时间完成初始化。
- 重连后的首次来源亮度只建立基准。按照当前产品规则，等待来源亮度下一次变化再写入外屏。

## 通用单文件启动器

`brightness_sync_launcher.c` 编译为 32 位 x86 程序，可在 x64 和 ARM64 Windows 上通过系统兼容支持运行，包括不支持 x64 仿真的 Windows 10 ARM64。它检测宿主架构，从资源中释放对应的 x64 或 ARM64 原生程序到临时文件，转发命令行并等待其退出。安装后，计划任务直接执行已复制到用户目录的原生程序；启动器不常驻。参见 [Microsoft 的 Windows on Arm 仿真说明](https://learn.microsoft.com/en-us/windows/arm/apps-on-arm-x86-emulation)。

## Windows API 参考

- [Monitor Configuration Functions](https://learn.microsoft.com/en-us/windows/win32/api/_monitor/)
- [GetVCPFeatureAndVCPFeatureReply](https://learn.microsoft.com/en-us/windows/win32/api/lowlevelmonitorconfigurationapi/nf-lowlevelmonitorconfigurationapi-getvcpfeatureandvcpfeaturereply)
- [GetPhysicalMonitorsFromHMONITOR](https://learn.microsoft.com/en-us/windows/win32/api/physicalmonitorenumerationapi/nf-physicalmonitorenumerationapi-getphysicalmonitorsfromhmonitor)
- [WM_DISPLAYCHANGE](https://learn.microsoft.com/en-us/windows/win32/gdi/wm-displaychange)
- [WM_DEVICECHANGE](https://learn.microsoft.com/en-us/windows/win32/devio/wm-devicechange)

