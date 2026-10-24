# EXVS2IB Ultrawide UI Fix

将 EXVS2IB 在超宽屏下横向拉伸的 UI 居中为 16:9，同时保持 3D 画面全宽。

- 5120×1440（32:9）时，UI 位于中央 2560×1440 区域。
- 其他宽于 16:9 的分辨率按比例自动适配。
- 16:9 不修改画面。
- 支持像素和 NDC 两种 UI 顶点格式，不修改 3D 相机或呈现管线。
- 同一 DLL 自动识别下述两个 exe 版本；未知版本不会安装 hook。

## 兼容版本

| Profile | `vsac29_Release.exe` SHA-256 |
| --- | --- |
| `ib1.0.4-legacy` | `c2af12bdf3b45c2810c889535a20c30ed16a3e18a88f58db705e07b9950f231e` |
| `ib1.0.4-hotupdate-20261024` | `e4509672c9e61d91c7c3cc6f3390197264dcce40e39a9bb67f1b7f32676ed91c` |

其他版本请先确认 hook 地址，不要直接安装。

## 安装

1. 关闭客户端和 `vsac29_Release_LM.exe`。LM 也会加载并锁住 `nbamUsbFinder.dll`。
2. 备份游戏目录中的 `nbamUsbFinder.dll` 和 `config.ini`。
3. **从自己的同版本原版游戏取得干净的原始 `nbamUsbFinder.dll`，将其备份为 `nbamUsbFinder_orig.dll`。** 如果该备份已存在且确定是干净原库，请保留。
4. 将发布包中的 `nbamUsbFinder.dll` 和 `uwfix.ini` 复制到 `vsac29_Release.exe` 所在目录。
5. 自行确认 `config.ini`：
   - `Windowed=False`
   - `Resolution=1080p`
6. 按平时顺序启动 Server、LM 和客户端。

发布包不包含原始游戏 DLL、个人配置、Server 或游戏本体。不要删除运行代理所需的 `nbamUsbFinder_orig.dll`，也不要把旧代理误当作干净原库。

## 配置

`uwfix.ini` 默认：

```ini
enabled=1
log_all=1
hook_vpapply=0
aspect=1.7777778
target_rva=0x11db8e
```

- `enabled=0` 可临时关闭修复，约 1.5 秒热更新，便于对照。
- 日志位于游戏工作目录的 `uwfix_log.txt`。
- 正常启动应出现 `lumen hooks mask=0x1f` 和 `lumen=OK`。
- 32:9 无边框下应出现 `scale=0.500000 offset=480.000`。
- 日志中的 `profile=` 会显示实际识别到的版本。

## 卸载

关闭客户端和 LM，恢复安装前备份的原 `nbamUsbFinder.dll`，并按需删除 `uwfix.ini`。如果没有其他备份，可在关闭后将干净的 `nbamUsbFinder_orig.dll` 复制/改名回 `nbamUsbFinder.dll`。

## 从源码构建

要求 Windows、Visual Studio 2022 的 MSVC x64 工具和 Windows SDK。

在 **x64 Native Tools Command Prompt for VS 2022** 中运行：

```bat
build.bat
```

产物位于 `dist/`。构建不需要 Ghidra、Python 或额外第三方库。

## 实现说明

代理通过 `nbamUsbFinder.dll` 加载，并将六个原始导出转发给用户自己备份的 `nbamUsbFinder_orig.dll`。修复仅对 UI 的横向顶点做中心预压缩：

- 像素坐标：`x' = (1920 - 1920*scale)/2 + x*scale`
- NDC 坐标：`x' = x*scale`
- `scale = min(1, (16/9) / window_aspect)`

直接 2D 命令使用独立类型和精确返回地址白名单；全屏 `PrimitivePre2d` 与 3D 效果路径不缩放。各版本地址表见 `src/uwfix.c` 中的 `g_profiles`。
