# FreeFrameGen release gate / 发布门槛

This document is the release decision record for FreeFrameGen. It keeps a
working beta from being described as a complete game product. A test that
passes on one development machine is evidence for that test only; it does not
replace the gates below.

## Version policy / 版本策略

| Label | Meaning |
| --- | --- |
| `experimental` | Developer builds. Interfaces and output behavior may change. |
| `beta` | Public preview with documented limitations. The current `v0.3.0-beta.1` release is here. |
| `rc` | Feature complete for the announced scope, with no open release blocker and a reproducible validation report. |
| `stable` | `v1.0.0` or later. The compatibility, quality, latency, packaging, and recovery gates below are all evidenced. |

`v0.3.0-beta.1` **must remain a beta**. It is not a `1.0 stable` release and
its capture path is not a drop-in replacement for Lossless Scaling. A future
implementation may add HDR, cross-process transport, and a full-screen output
path, but those features do not become release-ready merely because the code
compiles.

## Required `v1.0.0` evidence / `v1.0.0` 必须提供的证据

### 1. Native contract and transport / 原生协议与传输

- Freeze and publish the D3D12 and Vulkan header hashes, ABI version, feature
  negotiation rules, error table, and migration notes.
- Exercise the native path in a real host process, not only the in-process
  demo. The host must provide real color, depth, bidirectional motion, and
  object-ID resources and must verify the generated output.
- If cross-process mode is advertised, test adapter LUID matching,
  `NT handle`/shared-image lifetime, shared fence or timeline values, access
  denial, producer/consumer restart, timeout, duplicate handles, and version
  mismatch. Raw in-process pointers are never a cross-process protocol.
- Keep capture mode free of game DLL injection. Any native game integration
  is opt-in and must be documented as a developer supplied protocol.

### 2. HDR and output / HDR 与输出

- Verify SDR, scRGB and HDR10 metadata handling, color-space transitions,
  tone-map policy, and an explicit SDR fallback on real HDR displays.
- Verify borderless and exclusive-fullscreen output, resize, display removal,
  occlusion, alt-tab, VRR/vsync pacing, device removal and access-lost
  recovery. The generated window must not recursively capture itself.
- Measure source-present to generated-present timing with QPC/GPU timestamps.
  `mean_flow_submit_wait_ms` is an internal work metric, not end-to-end
  latency and not an FPS guarantee.

### 3. Quality and performance / 画质与性能

- Record a reproducible game matrix covering at least one D3D12 host and one
  Vulkan host, three GPU vendors where available, Windows 10 and 11, SDR and
  HDR, 30/60/120 Hz, camera cuts, UI, transparency, particles and rapid
  motion.
- Compare against a no-frame-generation baseline. Report median/p95/p99
  frame time, generated-frame rate, dropped/deadline frames, GPU memory, and
  end-to-end latency. State the scene, driver, resolution, refresh rate and
  capture mode for every number.
- Run a two-hour soak on every release candidate and retain logs for device
  removal, resize, monitor sleep/wake, minimize/restore and producer crash.
  A synthetic WARP or translation fixture cannot satisfy this gate alone.

### 4. Packaging and safety / 打包与安全

- Build the exact release commit on a clean Windows runner, run the complete
  CTest suite, and verify the installer SHA256 sidecar in CI.
- Test fresh install, reinstall, upgrade, rollback, uninstall, modified-file
  protection, user-data retention, desktop/Start-menu shortcut identity and
  a path containing spaces. The installer must not write a game directory,
  install a driver/service, or create a game hook.
- Publish signed binaries when claiming a stable player release. Until
  Authenticode signing and timestamp verification are available, keep the
  “unsigned/unknown publisher” warning in the release notes.

### 5. Compatibility and claims / 兼容性与宣传边界

- Record the exact tested game/engine, renderer API, driver and display mode.
  “Works for all games”, “same as Lossless Scaling”, “zero latency”, and
  “VAC safe” are not valid claims without evidence and an applicable vendor
  approval.
- AI or neural refinement is optional. A non-AI path can still be released,
  but its quality and motion limits must be stated rather than hidden behind a
  model name.

## Current decision / 当前结论

| Release | Decision | Reason |
| --- | --- | --- |
| `v0.3.0-beta.1` | **Beta only** | Local D3D12/Vulkan and capture checks pass, but the release lacks the full HDR/cross-process/full-screen evidence, real-game matrix, long-run recovery data, signed package, and end-to-end latency report required for stable. |
| `v1.0.0` | **Not eligible yet** | Promote only after every gate above has an attached log, test command, and reproducible build artifact. |

When a gate is met, add the command, commit, hardware/driver, result, and
artifact link to `docs/VALIDATION.md`. Do not replace “not tested” with “works”
because a later code path was added.

---

## 中文说明

本文是 FreeFrameGen 的正式发布决策记录，用来防止把开发 Beta 描述成完整的
游戏产品。单台开发机上的一次通过只能证明对应测试，不能替代下面的发布门槛。

当前 `v0.3.0-beta.1` 必须保持 **Beta** 标签，不能称为 `1.0 stable`，也不能
宣称是 Lossless Scaling 的即插即用替代品。即使之后加入 HDR、跨进程传输和全屏
输出，代码能编译也不等于这些功能已经达到正式版标准。

正式版前必须有：冻结并验证 D3D12/Vulkan 原生 ABI；真实宿主进程接入；跨进程句柄、
fence/timeline、崩溃和超时测试；SDR/scRGB/HDR10 色彩链路；窗口、无边框和独占全屏
输出；resize、设备移除、休眠恢复、VRR/vsync 和防递归捕获；多 GPU、Windows 10/11、
30/60/120 Hz、复杂运动和至少两种原生图形 API 的真实游戏矩阵；长时间稳定性；端到端
延迟基线；干净 runner 上的 CTest、安装/卸载/回滚、SHA256 与签名证据。

捕获模式继续禁止游戏 DLL 注入。原生模式是开发者主动提供数据的接入协议，不代表 FFG
会自动修改任意游戏。没有官方反作弊兼容依据时，也不能承诺 VAC 安全；不使用 AI 不会
降低这些验证门槛，只需如实说明非 AI 算法的画质边界。
