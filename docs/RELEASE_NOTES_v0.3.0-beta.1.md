# FreeFrameGen v0.3.0-beta.1

## 中文

首个公开 Beta，包含玩家安装器、桌面 GUI 和原生 FGDS v0.3 多在途接口。

### 玩家功能

- 双语 Windows x64 安装向导，支持选择安装目录。
- 默认创建开始菜单和桌面快捷方式，快捷方式使用 FFG 图标。
- `ffg_player.exe` 玩家控制台：选择显示器/可见窗口、处理宽度和 deadline，然后启动或停止捕获预览。
- `ffg_capture.exe` 保留为命令行诊断入口。

### 开发者功能

- D3D12 FGDS v0.3：多 slot、外部 fence 退休令牌、非阻塞 slot 检查。
- Vulkan FGDS v0.3：timeline semaphore、多 slot descriptor set 和严格资源契约。
- v0.1 ABI 保持冻结，v0.2/v0.3 使用追加结构和新入口。

### 捕获模式

- Desktop Duplication 显示器/窗口捕获。
- 转色、缩放、运动估计和 warp 合并提交。
- 自适应块匹配与 `--deadline-ms` 过期保护。

### 已知限制

当前仍是实验性预览：没有真实游戏内注入、跨进程共享纹理/fence、HDR、动态 resize/access-lost 自动恢复、全屏替换输出或端到端输入延迟保证。真实游戏画质需要逐游戏验证。

## English

First public beta with a player installer, desktop control GUI, and FGDS v0.3 multi-flight native APIs.

### Player features

- Bilingual Windows x64 installer with a selectable installation folder.
- Start menu and desktop shortcuts are created by default in the interactive wizard and use the FFG icon.
- `ffg_player.exe` lets players choose a display or visible window, processing width, and deadline before starting or stopping capture preview.
- `ffg_capture.exe` remains available for command-line diagnostics.

### Developer features

- D3D12 FGDS v0.3 with multi-flight slots, external fence retirement tokens, and non-blocking slot checks.
- Vulkan FGDS v0.3 with timeline semaphores, per-slot descriptor sets, and strict resource validation.
- The v0.1 ABI remains frozen; v0.2/v0.3 use append-only structures and new entry points.

### Capture mode

- Desktop Duplication monitor/window capture.
- Conversion, scaling, motion estimation, and warp are recorded in one submission.
- Adaptive block matching and `--deadline-ms` stale-frame protection.

### Known limitations

This is still an experimental preview. It does not provide in-game injection, cross-process shared texture/fence transport, HDR, automatic resize/access-lost recovery, full-screen replacement output, or an end-to-end input-latency guarantee. Game image quality must be validated per game.

## Installer checksum

The published installer is accompanied by `FreeFrameGen-Setup-0.3.0-x64.exe.sha256`.
Use that sidecar file to verify the exact binary downloaded from the Beta release.

## Release status and stable gate / 发布状态与正式版门槛

This tag is intentionally **beta**, not `v1.0.0 stable`. The checks listed in
this file prove the beta artifacts and the tested synthetic/capture paths; they
do not prove universal game compatibility. In particular, the beta does not
claim HDR output, cross-process native texture/fence transport, production
full-screen replacement, an end-to-end latency number, Authenticode signing,
VAC approval, or parity with Lossless Scaling.

Do not retag this commit as a stable release after adding code elsewhere. A
future release must pass the evidence gates in
[`docs/RELEASE_GATE.md`](RELEASE_GATE.md), including a real host/game matrix,
HDR and output recovery, cross-process lifetime/security tests, long-run
latency measurements, clean-runner packaging, and signed artifacts. The beta
installer remains useful for previewing the current capture path, but its
limitations are release blockers for `v1.0.0`.

本标签明确是 **Beta**，不是 `v1.0.0 stable`。这里的测试只证明 Beta 安装包以及
当前合成/捕获路径的已测行为，不能证明“所有游戏兼容”。本 Beta 不承诺 HDR 输出、
跨进程纹理/fence 传输、生产级全屏替换、端到端延迟数值、数字签名、VAC 认可，或与
Lossless Scaling 完全一致。

正式版必须按 [`docs/RELEASE_GATE.md`](RELEASE_GATE.md) 补齐真实宿主/游戏矩阵、
HDR 和输出恢复、跨进程生命周期与安全、长期延迟测量、干净 runner 打包以及签名
证据；不能因为其他提交后来加入了代码，就把这个旧 Beta 标签重新描述为 stable。
