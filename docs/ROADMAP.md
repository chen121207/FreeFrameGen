# FreeFrameGen roadmap / 路线图

The roadmap is a scope and evidence plan, not a promise that a feature is
ready because a prototype exists. For the release decision, see
[RELEASE_GATE.md](RELEASE_GATE.md). Every row must be reported as code,
build, synthetic/GPU validation, and real-game validation separately.

## Current line: v0.4 first public release

| Area | Current evidence | Before `rc` | Before `v1.0.0 stable` |
| --- | --- | --- | --- |
| Native D3D12 | FGDS v0.1 frozen, V2/V3 negotiation and multi-flight WARP/hardware checks | Real host process with resource lifetime and present pacing | Header/ABI hash, compatibility matrix, crash/restart and upgrade evidence |
| Native Vulkan | V0.1/V2/V3 headers and a two-slot timeline smoke test | Real renderer submits and reads back generated output | At least one real Vulkan host on the published driver matrix |
| Cross-process native | D3D12 shared image/fence import, adapter-LUID checks, queue waits and slot-retained COM references; Vulkan descriptor validation | Two-process host matrix, timeout/restart/denial/version-mismatch evidence | Handle lifetime, security and real-game transport matrix |
| Capture | Desktop Duplication SDR monitor/window path, shared D3D11/D3D12 fence, adaptive block matching and borderless replacement | HDR10 source handling, dynamic resize and access-lost recovery | Real HDR displays, device removal and long-run evidence |
| Output | FFG-owned borderless topmost replacement window with waitable DXGI pacing and self-capture exclusion; scRGB FP16 output request | Resize, alt-tab, VRR/vsync and device recovery | Exclusive fullscreen, monitor sleep/removal and end-to-end timing matrix |
| Quality | Synthetic translation/occlusion fixtures and WARP/RTX flow stress | Scene set for cuts, UI, transparency, particles, thin geometry and rapid motion | Reproducible game matrix and no-frame-generation baseline |
| Latency | Deadline/back-pressure counters and internal submission-wait metric | QPC/GPU timestamp instrumentation and frame pacing report | Median/p95/p99 end-to-end input-to-display measurements and two-hour soak |
| Player package | x64 installer, user scoped shortcuts, uninstall safeguards and SHA256 sidecar | Clean-runner install/reinstall/rollback checks | Authenticode signature/timestamp, release artifact and recovery evidence |
| Safety boundary | Capture has no game injection; no driver/service/auto-start | Native integration documentation and explicit opt-in host contract | Compatibility report; never claim universal game support or VAC approval |

## Release labels

- **Experimental:** interfaces and output behavior may change.
- **Early public release:** usable public build with documented limitations.
  `v0.4.0` is this stage.
- **Beta:** public preview with known limitations. `v0.3.0-beta.1` remains the
  earlier beta tag.
- **Release candidate:** feature complete for the announced scope, with no
  open release blocker and reproducible evidence.
- **Stable:** `v1.0.0` only after every gate in
  [RELEASE_GATE.md](RELEASE_GATE.md) has an attached log and artifact.

No milestone should be promoted because a test fixture or one GPU passes. A
missing real-game, HDR, cross-process, full-screen, recovery, or signed-package
result remains “not verified”.

## 下一阶段 / Next implementation order

1. **协议与传输 / Protocol and transport:** run the two-process D3D12 host
   matrix and add timeout, restart, denial, duplicate-handle, and version-mismatch
   evidence; complete Vulkan external-memory import when a real host requires it.
2. **HDR / HDR:** add HDR10 source handling, explicit tone mapping, SDR fallback,
   and tests for no double encoding on real HDR displays.
3. **输出 / Output:** add resize, alt-tab, VRR/vsync, device removal, monitor
   sleep/wake, and self-capture recovery to the borderless replacement path.
4. **质量与调度 / Quality and scheduling:** add motion/occlusion test scenes,
   deadline decisions, GPU/QPC timestamps, and a no-frame-generation baseline.
5. **正式版 / Stable release:** run the hardware/game/driver matrix, soak,
   packaging recovery and signing gates. Do not label the build `v1.0.0`
   before the evidence is attached.

## 中文边界

捕获模式保持“不修改游戏、不注入 DLL”的原则。原生模式只接受游戏或渲染器主动
提供的 Color、Depth、双向 Motion、Object ID 以及同步信息；它不是自动给任意游戏
接入的注入器。项目不使用 AI 也可以发布，但必须如实写明非 AI 算法对透视、薄片、
粒子、透明、快速运动和遮挡重建的限制。
