# FreeFrameGen v0.4.0 — first public release / 首个公开发布版

## 中文

这是 FreeFrameGen 的首个非 Beta 公开发布版，提供非 AI 的 GPU 运动估计、显示器替换输出、原生 FGDS 接入和 D3D12 跨进程共享资源路径。它仍然不是 v1.0 的“所有游戏保证兼容”产品；正式稳定版门槛见 [`docs/RELEASE_GATE.md`](RELEASE_GATE.md)。

### 玩家功能

- `ffg_player.exe` 双语 GUI，可选择显示器/可见窗口、处理宽度和 deadline。
- `--replace`/`--fullscreen`/`--borderless` 创建覆盖所选显示器的置顶无边框输出窗口。
- `--hdr-output` 使用 FP16 scRGB swapchain 和 Windows 色彩空间标签；Desktop Duplication 输入仍是 BGRA8 SDR，不能称为 HDR passthrough。
- 捕获模式不注入游戏、不修改游戏文件、不读取游戏内存，不安装驱动或服务。
- Windows x64 安装器支持选择目录、桌面快捷方式、开始菜单和卸载保护。

### 开发者功能

- D3D12 FGDS v0.1/v0.2/v0.3 旧 ABI 保持兼容。
- FGDS v0.4 HDR 元数据和共享句柄结构，`ffgRecordSharedV1` 实际调用 `OpenSharedHandle`、队列 fence wait，并保持导入对象直到 retire fence 完成。
- Vulkan FGDS v0.4 提供 HDR/外部 memory/timeline semaphore 协议校验；当前不替调用方导入 Vulkan 对象或提交跨进程等待。
- 不使用神经网络或训练模型，运动估计使用 GPU compute 的自适应块匹配和一致性检查。

### 真实边界

- 捕获模式的源端仍受 Desktop Duplication 的 BGRA8 SDR 限制；HDR10/PQ/HLG 源捕获、曝光和色调映射尚未完成。
- 替换输出是 FFG 自己的 borderless 窗口，不接管游戏 swapchain，也不实现独占游戏全屏；窗口遮挡、access-lost、动态 resize 和显示器切换仍需重启处理。
- D3D12 共享路径要求生产者预先导出并复制 NT/Win32 资源和 fence 句柄，双方使用同一 adapter LUID；Vulkan 外部对象目前只有协议验证。
- 真实游戏画质、端到端输入延迟、VRR、长时间稳定性、签名和反作弊兼容仍需逐硬件/逐游戏验收。

## English

FreeFrameGen v0.4.0 is the first non-beta public release. It provides non-AI GPU motion estimation, a monitor replacement output, native FGDS integration, and a D3D12 cross-process shared-resource path. It is not a v1.0 claim of universal game compatibility; see [`docs/RELEASE_GATE.md`](RELEASE_GATE.md) for the stable-release gates.

### Player features

- Bilingual `ffg_player.exe` GUI for display/window selection, processing width, and source deadline.
- `--replace`, `--fullscreen`, and `--borderless` create a topmost borderless output window covering the selected monitor.
- `--hdr-output` requests an FP16 scRGB swapchain and Windows color-space tag. Desktop Duplication input remains BGRA8 SDR, so this is not HDR passthrough.
- Capture mode does not inject into games, edit game files, read game memory, install drivers, or install services.
- Windows x64 installer with selectable directory, desktop shortcut, Start menu entry, and guarded uninstall.

### Developer features

- D3D12 FGDS v0.1/v0.2/v0.3 ABIs remain available.
- FGDS v0.4 HDR metadata and shared-handle structures; `ffgRecordSharedV1` opens resources with `OpenSharedHandle`, inserts queue fence waits, and retains imports until the retire fence completes.
- Vulkan FGDS v0.4 validates HDR, external-memory, and timeline-semaphore descriptors. It does not yet import Vulkan objects or submit cross-process waits for the caller.
- No neural network or training model is used. Motion estimation is adaptive block matching and consistency checking in GPU compute.

### Known boundaries

- Capture input is still limited by Desktop Duplication's BGRA8 SDR surface; HDR10/PQ/HLG source capture, exposure, and tone mapping are not implemented.
- Replacement output is an FFG-owned borderless window. It does not take over a game's swapchain or implement exclusive game fullscreen; occlusion, access-lost, dynamic resize, and display changes still require restart.
- D3D12 sharing requires the producer to export and duplicate resource/fence handles and both processes to use the same adapter LUID. Vulkan external objects are currently protocol-only.
- Real-game image quality, end-to-end input latency, VRR pacing, long-run stability, signing, and anti-cheat compatibility require per-hardware and per-game validation.

## Installer checksum

The release assets include `FreeFrameGen-Setup-0.4.0-x64.exe.sha256`. Verify the sidecar before running the installer.
