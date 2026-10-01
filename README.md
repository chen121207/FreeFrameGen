# FreeFrameGen (FFG)

**Windows GPU frame-generation runtime, native integration protocol, and desktop capture preview.**
**Windows GPU 帧生成运行时、原生接入协议与桌面捕获预览程序。**

[简体中文](#中文) · [English](#english) · [Releases](https://github.com/chen121207/FreeFrameGen/releases)

> **当前状态 / Status:** v0.4.0 first public release, early access.
> v0.4.0 首个公开发布版，早期体验；不是 v1.0 全游戏兼容承诺。

---

## 中文

### 项目是什么

FreeFrameGen（FFG）把两个使用场景放在同一个 Windows 项目中：

1. **原生模式（FGDS）**：游戏或渲染器提供两端真实 GPU 帧、线性深度、双向运动矢量和稳定 Object ID，FFG 只在调用者的 command list/command buffer 中记录插帧 compute。
2. **捕获模式**：不修改游戏、不注入 DLL，使用 Windows Desktop Duplication 捕获显示器或可见窗口，在 GPU 上估计运动并显示插值预览。

两条路径共享同一个重投影/遮挡处理方向，但资源契约、同步和错误边界是分开的。项目不是 L4D2 专用程序，也不依赖 Lossless Scaling、Remix、DLSS 或 NVIDIA 私有 API。

### 当前能力

- D3D12 原生 FGDS v0.1（冻结 ABI）、v0.2 能力协商、v0.3 多在途 slot 与 v0.4 HDR/跨进程共享资源入口。
- Vulkan 原生 FGDS v0.1/v0.2/v0.3/v0.4 独立 ABI；Vulkan V3 使用 timeline semaphore，v0.4 提供外部对象协议校验。
- D3D12 compute：Color、Depth、双向 Motion Vector、Object ID、深度/ID 遮挡和 camera-cut 回退。
- 捕获模式：Desktop Duplication、D3D11→D3D12 shared texture/fence、GPU 转色/缩放/运动估计/warp 合并提交。
- 自适应块匹配、往返一致性检查、低可信度端点回退，以及 --deadline-ms 超时保护。
- `--replace`/`--fullscreen`/`--borderless` 全屏替换输出；`--hdr-output` 使用 FP16 scRGB 输出。
- 全部捕获算法不使用 AI 或训练模型；捕获路径不注入游戏，只有开发者主动接入的 Native FGDS 才能取得游戏真实资源。
- Windows 安装器、卸载器、开始菜单入口和桌面快捷方式支持；程序不会安装驱动、服务或自启动项。

### 给玩家：安装和快速开始

1. 从 [GitHub Releases](https://github.com/chen121207/FreeFrameGen/releases) 下载 FreeFrameGen-Setup-*-x64.exe。
2. 选择安装目录。图形安装向导默认勾选创建桌面快捷方式，同时创建开始菜单入口；用户可以取消桌面入口。静默安装默认不创建桌面快捷方式，使用 --desktop 或 --no-desktop 明确控制。
3. 启动 **FreeFrameGen**，选择显示器编号；也可以先列出窗口，再选择一个可见窗口的客户区。
4. 预览窗口获得焦点时按 **Space** 切换 FG ON/OFF，按 **Esc** 或关闭窗口停止。

安装器使用 FreeFrameGen 图标，要求安装目录为新的、独立的、可写的用户目录；不会覆盖游戏目录，不会修改 L4D2/Steam，不会注入进程，也不会绕过系统的受保护内容。卸载入口位于 Windows“已安装的应用”、开始菜单和安装目录中的 Uninstall.exe。当前安装包未数字签名，Windows 可能显示未知发布者提示。

高级用户可以直接运行：

~~~powershell
# 启动玩家 GUI（安装后通常从桌面/开始菜单打开）
.\bin\ffg_player.exe

# 列出显示器和窗口
.\bin\ffg_capture.exe --list
.\bin\ffg_capture.exe --list-windows

# 捕获显示器或窗口并打开预览
.\bin\ffg_capture.exe --output 0 --seconds 60 --width 960
.\bin\ffg_capture.exe --window 0 --seconds 60 --width 960

# 为低延迟实验设置源帧 deadline（4..100 ms，默认 24 ms）
.\bin\ffg_capture.exe --output 0 --seconds 60 --width 960 --deadline-ms 16

# 覆盖显示器的 FFG 无边框替换输出（不注入游戏）
.\bin\ffg_capture.exe --output 0 --replace --seconds 60 --width 960

# 请求 FP16 scRGB HDR 输出（Desktop Duplication 源仍为 BGRA8 SDR）
.\bin\ffg_capture.exe --output 0 --replace --hdr-output --seconds 60 --width 960

# 只运行调度/历史连续性自检，不访问桌面
.\bin\ffg_capture.exe --self-test
~~~

捕获程序的完整参数和限制见 [docs/CAPTURE.md](docs/CAPTURE.md)。替换输出是 FFG 自己的置顶无边框窗口，不修改游戏 swapchain、逻辑帧率或输入采样率。

### 两条路径

| 路径 | 适用对象 | 输入 | 同步责任 | 当前边界 |
| --- | --- | --- | --- | --- |
| 原生 FGDS | 游戏/引擎/渲染器开发者 | Color、Depth、双向 Motion、Object ID；v0.4 共享句柄/HDR 元数据 | 宿主负责资源状态、提交、signal、Present；D3D12 共享入口负责 OpenSharedHandle/queue Wait | D3D12 跨进程需同一 adapter 和宿主导出的句柄；Vulkan 外部对象目前只校验协议 |
| 捕获模式 | 不改游戏的玩家和诊断 | Desktop Duplication 颜色帧 | FFG 负责 shared fence、单飞资源所有权和替换输出 | 源仍 SDR BGRA8；HDR 只扩展输出，动态恢复和独占 swapchain 尚未实现 |

### 原生模式协议（FGDS）

FGDS 是本项目维护中的实验协议，不是行业标准。v0.1 的结构布局和入口保持冻结；v0.2/v0.3/v0.4 使用追加结构和新入口，不改变旧 ABI。D3D12 与 Vulkan 是两套独立 ABI，字段含义相近但句柄类型不同。

#### 入口版本

- **D3D12**：include/fgds/fgds.h、include/ffg/ffg.h；ffgCreate/ffgRecord 为旧入口，ffgRecordV2 用于显式资源位，ffgCreateV3/ffgRecordV3 用于多 slot。
- **Vulkan**：include/fgds/fgds_vk.h、include/ffg/ffg_vk.h；ffgVkRecordV2 为显式资源位入口，ffgVkCreateV3/ffgVkRecordV3 使用 timeline semaphore 做多 slot。
- 调用 ffgGetCapabilities* 前必须把 structSize 设置为对应结构的 sizeof。能力不满足时，宿主应显示最近真实帧或回退到旧路径。
- FgdsFrameV2/FgdsPairV2（Vulkan 为 FgdsVk*V2）的 resourceFlags 至少包含 core 四项；HUD/透明 mask 当前没有可用的 mask-aware kernel，设置后会明确报错，不会静默忽略。

#### v0.4 HDR 与跨进程原生接入

`include/fgds/ipc.h` 定义 `FgdsHdrMetadata`、共享图像、共享 fence、共享 frame/pair 结构。HDR 元数据支持 SDR/sRGB、scRGB/linear、HDR10/PQ 和 HLG；两个输入端点与输出目标必须声明一致的元数据。协议只接受线性 RGBA32_FLOAT 计算资源，HDR 色彩转换和 Present 由宿主/输出链路负责。

D3D12 的 `ffgRecordSharedV1` 在消费者进程实际打开导出的资源和 fence 句柄，在同一 command queue 上等待两个 ready value，再调用 V3 kernel；导入对象会保持到 retire fence 完成。生产者必须复制 Win32/NT 句柄到消费者进程，并保证 adapter LUID、格式、尺寸、资源状态和 fence 生命周期正确。Vulkan 的 `FgdsVkExternalImage`/`FgdsVkExternalSync` 目前提供严格协议校验，实际 external memory import 和 queue wait 仍由宿主完成。

#### 每个端点必须提供

| 资源/元数据 | 约定 |
| --- | --- |
| Color | RGBA32_FLOAT，线性 Rec.709，预 HUD，alpha=1 |
| Depth | R32_FLOAT，正的线性 view distance，数值越小越近 |
| Motion | RG32_FLOAT，像素单位，+X 向右、+Y 向下；A 是 A→B，B 是 B→A |
| Object ID | R32_UINT，跨两端点稳定的非零 ID；0 表示无效 |
| worldToClip | row-major、row-vector、未 jitter 的描述矩阵；当前 kernel 不用它替代完整 motion |
| jitterPixels | 当前必须为 (0,0) |
| flags | 相机切换、传送或时间线断裂时设置 FGDS_CAMERA_CUT，该 pair 输出新帧 B |

两端点宽高必须一致，frameId 和同一单调时钟产生的 timestampNs 必须严格递增。每个 pair 需要真实双向 motion；不能把上一对的 backward vector 当作当前 pair 的 forward vector。尺寸变化时，宿主应丢弃旧 pair、等待 GPU 完成并重建输出/context。

#### D3D12 V3 多在途示例

~~~cpp
FfgContextV3* ctx = nullptr;
ffgCreateV3(device, 3, &ctx);

FgdsCapabilitiesV3 caps{sizeof caps};
ffgGetCapabilitiesV3(ctx, &caps);

const uint32_t slot = frameIndex % caps.slotCount;
const uint64_t completion = ++nextFenceValue;
HRESULT hr = ffgRecordV3(ctx, commandList, &pair, output,
                         slot, queueFence, completion);
if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
    // 该 slot 尚未退休：跳过插帧或选择另一个 slot。
}
// 宿主 ExecuteCommandLists；同一 queue 在完成后 Signal(completion)。
~~~

V3 每个 slot 有独立 descriptor heap。ffgRecordV3 只记录 compute 和 UAV barrier，不提交、不等待、不 signal；每次调用都需要非空 fence 和非零、尚未完成的 value。slot 未完成时立即返回 DXGI_ERROR_WAS_STILL_DRAWING，不会修改 descriptor 或 command list。销毁 context 前必须等待所有 slot 的 completion value。

#### Vulkan V3 多在途示例

~~~cpp
FfgVkContextV3* ctx = nullptr;
ffgVkCreateV3(physicalDevice, device, queueFamily,
              vkGetDeviceProcAddr, 3, &ctx);

FgdsVkCapabilitiesV3 caps{sizeof caps};
ffgVkGetCapabilitiesV3(ctx, &caps);
ffgVkRecordV3(ctx, commandBuffer, &pair, &output, slot,
              completionTimeline, completionValue);
// 宿主在同一次 vkQueueSubmit(2) 中 signal completionValue。
~~~

Vulkan V3 要求 Vulkan 1.2 或 VK_KHR_timeline_semaphore，每个 slot 使用独立 descriptor set。旧 token 未达到时返回 VK_NOT_READY；新 token 已经达到当前值时返回校验错误。FFG 不提交、不等待、不 signal，也不负责 image layout、queue ownership 或 ready semaphore 等待。详见 [docs/FGDS_VULKAN.md](docs/FGDS_VULKAN.md)。

#### D3D12/Vulkan 资源状态

- D3D12 输入必须由宿主转换为 NON_PIXEL_SHADER_RESOURCE，输出为同尺寸 RGBA32_FLOAT UAV 且处于 UNORDERED_ACCESS；输入、输出不能别名，所有资源必须来自同一 device。
- Vulkan image/view 必须是同一 device、同一 queue family、单 mip/layer/sample storage image，协议格式 ID 和 layout 必须真实；当前 runtime 要求 VK_IMAGE_LAYOUT_GENERAL。
- FFG 会拒绝错误尺寸、设备、格式、布局、句柄别名、版本、时间顺序、非零 jitter 以及缺失核心资源。宿主应在失败时显示最近真实帧。
- FFG 不保存跨 pair history，不读游戏内存，不创建宿主资源，不调用 Present。宿主必须保持资源、command allocator/list、context 和同步对象存活到 GPU 完成。

协议的完整字段、错误表和生命周期要求见 [docs/FGDS.md](docs/FGDS.md) 与 [docs/FGDS_VULKAN.md](docs/FGDS_VULKAN.md)。

### 捕获模式：低延迟和画质边界

捕获链路基于 [Desktop Duplication](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api)。每次获取桌面帧后，FFG 使用 LastPresentTime、AccumulatedFrames 和 QPC 做连续性判断；D3D11 shared texture/fence 交给 D3D12 后，在同一个 command list 中完成转色、缩放、双向块匹配和 warp，下一次 AcquireNextFrame 前只等待一次 CPU fence。

默认源帧 deadline 为 24 ms。源帧过期、预测会超预算或插值完成后已过期时，FFG 显示最新真实帧并重置历史，不把积压工作无限排队。accumulated_skips、history_resets、deadline_skips、deadline_overruns 和 mean_flow_submit_wait_ms 只描述捕获链路，不能当作端到端输入延迟或游戏 FPS。

当前捕获源仍是 Desktop Duplication 的 BGRA8 SDR，窗口客户区在启动时固定。`--replace` 是覆盖所选显示器的 FFG borderless topmost 窗口；`--hdr-output` 是 FP16 scRGB 输出，不是 HDR10/PQ 源 passthrough。独占全屏、被遮挡/最小化窗口、动态 resize、access-lost 自动恢复、鼠标透传和真实 Depth/Object ID/HUD 仍需宿主或后续版本提供。

### 构建和开发

要求 Windows 10/11 x64、Visual Studio C++、Windows SDK、CMake 3.24+。Debug 检查需要 Graphics Tools。默认构建 D3D12；Vulkan 需要 Vulkan-Headers 和 glslangValidator，可显式传入路径。

~~~powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel

# 本地运行
.\build\Release\ffg_capture.exe --self-test
.\build\Release\ffg_demo.exe --headless --warp --debug
.\build\Release\ffg_flow_test.exe --stress --debug
ctest --test-dir build -C Release --output-on-failure  # 10 tests

# 可选 Vulkan
cmake -S . -B build-vulkan-root -A x64 -DFFG_BUILD_VULKAN=ON -DFFG_VK_HEADERS="D:/path/to/Vulkan-Headers/include" -DFFG_GLSLANG="D:/path/to/glslangValidator.exe"
cmake --build build-vulkan-root --config Release --parallel
ctest --test-dir build-vulkan-root -C Release --output-on-failure  # 12 tests with Vulkan
~~~

ffg_demo 是原生协议和合成场景验证程序，不是 L4D2 渲染器。--warp 只用于软件回归，不代表硬件性能。安装器构建见 [docs/INSTALLER.md](docs/INSTALLER.md)，源代码结构见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

### 本地验证和已知限制

截至 2026-10-01，本机验证包括：D3D12 CTest 10/10、显式 Vulkan 构建 CTest 12/12、真实 Vulkan V3 两 slot/timeline smoke、D3D12 V3 slot 忙/复用、HDR/共享协议伪句柄校验、RTX 3050 Desktop Duplication 替换输出和 scRGB FP16 输出、WARP 与 flow stress、安装包清单/卸载保护测试。替换输出约 1 秒捕获成功退出；内部等待均值不等于端到端延迟或通用游戏画质保证。

仍未验证或未实现：真实游戏内 native 接入、跨进程双进程游戏矩阵、HDR10/PQ 源 passthrough、动态 resize/access-lost 自动恢复、高质量稠密 Optical Flow、复杂反遮挡/temporal reconstruction、独占游戏 swapchain 替换、端到端输入延迟、VRR、VAC/反作弊兼容。项目不会声称“所有游戏都已验证”、零延迟或“绝对不会封禁”。

### 文档索引

- [捕获模式](docs/CAPTURE.md)
- [FGDS D3D12 协议](docs/FGDS.md)
- [FGDS Vulkan 协议](docs/FGDS_VULKAN.md)
- [安装器、卸载器和快捷方式](docs/INSTALLER.md)
- [系统架构](docs/ARCHITECTURE.md)
- [路线图](docs/ROADMAP.md)
- [验证记录](docs/VALIDATION.md)
- [v0.3.0-beta.1 发布说明](docs/RELEASE_NOTES_v0.3.0-beta.1.md)
- [v0.4.0 首个公开发布说明](docs/RELEASE_NOTES_v0.4.0.md)
- [正式版发布门槛](docs/RELEASE_GATE.md)
- [卸载与 VAC 边界](docs/UNINSTALL_AND_VAC.md)

---

## English

### What this project is

FreeFrameGen (FFG) covers two Windows use cases:

1. **Native mode (FGDS):** a game or renderer supplies two real GPU endpoints, linear depth, bidirectional motion vectors, and stable object IDs. FFG records the interpolation compute into the caller's command list/command buffer.
2. **Capture mode:** without modifying or injecting into a game, FFG uses Windows Desktop Duplication to capture a monitor or visible window, estimates motion on the GPU, and displays an interpolated preview.

The paths share the reprojection/occlusion direction but have separate resource, synchronization, and failure contracts. FFG is not an L4D2-specific program and does not depend on Lossless Scaling, Remix, DLSS, or NVIDIA private APIs.

### Current capabilities

- D3D12 native FGDS v0.1 (frozen ABI), v0.2 capability negotiation, v0.3 multi-flight slots, and v0.4 HDR/shared-resource entry points.
- Independent Vulkan FGDS v0.1/v0.2/v0.3/v0.4 ABI; Vulkan V3 retires slots with timeline semaphores and v0.4 validates external-object descriptors.
- D3D12 compute using Color, Depth, bidirectional Motion Vector, Object ID, depth/ID occlusion, and camera-cut fallback.
- Capture path with Desktop Duplication, D3D11-to-D3D12 shared texture/fence, and one combined GPU submission for conversion, scaling, motion estimation, and warp.
- Adaptive block matching, consistency checks, low-confidence endpoint fallback, and a --deadline-ms back-pressure policy.
- `--replace`/`--fullscreen`/`--borderless` monitor replacement output and `--hdr-output` FP16 scRGB presentation.
- No AI or training model is used. Capture mode never injects into a game; only a developer-supplied Native FGDS integration can access real game resources.
- Windows installer, uninstaller, Start menu entry, and desktop shortcut support; no driver, service, or auto-start item is installed.

### For players: install and quick start

1. Download FreeFrameGen-Setup-*-x64.exe from [GitHub Releases](https://github.com/chen121207/FreeFrameGen/releases).
2. Choose an installation directory. The GUI wizard has desktop shortcut creation enabled by default and creates a Start menu entry; the user may clear the desktop option. Silent installs do not touch the desktop unless --desktop or --no-desktop is explicitly passed.
3. Launch **FreeFrameGen** and select a display. You can also list windows and select a visible window client area.
4. With the preview focused, press **Space** to toggle FG ON/OFF, or **Esc**/close the window to stop.

The installer uses the FreeFrameGen icon and requires a new, independent, writable per-user directory. It does not overwrite a game directory, modify L4D2/Steam, inject into a process, or bypass protected content. Uninstall is available from Windows Installed apps, the Start menu, and Uninstall.exe in the installation directory. The current installer is unsigned, so Windows may show an unknown-publisher prompt.

Advanced users can run the capture executable directly:

~~~powershell
.\bin\ffg_player.exe
.\bin\ffg_capture.exe --list
.\bin\ffg_capture.exe --list-windows
.\bin\ffg_capture.exe --output 0 --seconds 60 --width 960
.\bin\ffg_capture.exe --window 0 --seconds 60 --width 960
.\bin\ffg_capture.exe --output 0 --seconds 60 --width 960 --deadline-ms 16
.\bin\ffg_capture.exe --output 0 --replace --seconds 60 --width 960
.\bin\ffg_capture.exe --output 0 --replace --hdr-output --seconds 60 --width 960
.\bin\ffg_capture.exe --self-test
~~~

See [docs/CAPTURE.md](docs/CAPTURE.md) for the complete command reference and known limitations. Replacement output is an FFG-owned topmost borderless window; it does not modify a game's swapchain, logic FPS, or input sampling.

### Two paths

| Path | Intended user | Input | Synchronization owner | Current boundary |
| --- | --- | --- | --- | --- |
| Native FGDS | Game/engine/renderer developers | Color, Depth, bidirectional Motion, Object ID; v0.4 shared handles/HDR metadata | Host owns states, submit, signal, Present; D3D12 shared entry imports and queue-waits | D3D12 sharing requires one adapter and host-exported handles; Vulkan external objects are protocol validation only |
| Capture | Players and diagnostics without game changes | Desktop Duplication color frames | FFG owns shared-fence handoff and replacement output | Input remains SDR BGRA8; HDR output is scRGB and recovery/exclusive swapchain paths are incomplete |

### Native mode protocol (FGDS)

FGDS is an experimental protocol maintained by this project, not an industry standard. v0.1 layout and entry points remain frozen; v0.2/v0.3/v0.4 use append-only structures and new entry points. D3D12 and Vulkan are separate ABIs with similar field meanings but different handle types.

#### Entry points and versions

- **D3D12:** include/fgds/fgds.h, include/ffg/ffg.h; use ffgRecordV2 for explicit resource flags and ffgCreateV3/ffgRecordV3 for multi-flight slots.
- **Vulkan:** include/fgds/fgds_vk.h, include/ffg/ffg_vk.h; use ffgVkRecordV2 for explicit resource flags and ffgVkCreateV3/ffgVkRecordV3 for timeline-semaphore slots.
- Set structSize = sizeof(struct) before every ffgGetCapabilities* call. If required capabilities are unavailable, show the latest real frame or use the older path.
- V2 frame resourceFlags must include the core resources. HUD/transparency masks are rejected until a mask-aware kernel is implemented; they are never silently ignored.

#### v0.4 HDR and cross-process native transport

`include/fgds/ipc.h` defines `FgdsHdrMetadata`, shared images, shared fences, and shared frame/pair structures. HDR metadata covers SDR/sRGB, scRGB/linear, HDR10/PQ, and HLG; both endpoints and the output target must declare matching metadata. The interpolation kernel still consumes linear RGBA32_FLOAT resources, while color conversion and Present remain host/output responsibilities.

D3D12 `ffgRecordSharedV1` opens producer-exported resource and fence handles in the consumer process, waits for both ready values on the supplied command queue, and records the V3 kernel. Imported COM objects remain retained until the retire fence completes. The producer must duplicate Win32/NT handles into the consumer process and maintain adapter LUID, format, size, resource-state, and fence lifetime rules. Vulkan `FgdsVkExternalImage`/`FgdsVkExternalSync` currently provide strict protocol validation; the host still performs external-memory import and queue waits.

#### Required endpoint data

| Resource/metadata | Contract |
| --- | --- |
| Color | RGBA32_FLOAT, linear Rec.709, pre-HUD, alpha=1 |
| Depth | R32_FLOAT, positive linear view distance; smaller is nearer |
| Motion | RG32_FLOAT, pixel units, +X right/+Y down; A is A→B and B is B→A |
| Object ID | R32_UINT, stable non-zero ID across endpoints; 0 means invalid |
| worldToClip | Row-major, row-vector, unjittered descriptive matrix; not a substitute for full motion in the current kernel |
| jitterPixels | Must be (0,0) in the current implementation |
| flags | Set FGDS_CAMERA_CUT for camera cuts, teleports, or timeline breaks; the pair outputs endpoint B |

Endpoint dimensions must match, and frameId/timestampNs from one monotonic clock must increase strictly. Every pair needs true bidirectional motion; a backward vector from the previous pair cannot be reused as the current forward vector. On resize, discard the old pair, wait for GPU completion, and recreate output/context.

#### D3D12 V3 multi-flight example

~~~cpp
FfgContextV3* ctx = nullptr;
ffgCreateV3(device, 3, &ctx);
FgdsCapabilitiesV3 caps{sizeof caps};
ffgGetCapabilitiesV3(ctx, &caps);

uint32_t slot = frameIndex % caps.slotCount;
uint64_t completion = ++nextFenceValue;
HRESULT hr = ffgRecordV3(ctx, commandList, &pair, output,
                         slot, queueFence, completion);
if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
    // Slot is still in flight: skip interpolation or choose another slot.
}
// Host executes the list and signals completion on the same queue.
~~~

Each V3 slot owns a descriptor heap. ffgRecordV3 only records compute and a UAV barrier; it does not submit, wait, or signal. Every call requires a non-null fence and a non-zero value that is not already complete. A busy slot returns immediately without changing descriptors or the command list. Wait for all completion values before destroying the context.

#### Vulkan V3 multi-flight example

~~~cpp
FfgVkContextV3* ctx = nullptr;
ffgVkCreateV3(physicalDevice, device, queueFamily,
              vkGetDeviceProcAddr, 3, &ctx);
FgdsVkCapabilitiesV3 caps{sizeof caps};
ffgVkGetCapabilitiesV3(ctx, &caps);
ffgVkRecordV3(ctx, commandBuffer, &pair, &output, slot,
              completionTimeline, completionValue);
// Signal completionValue in the same vkQueueSubmit(2) submission.
~~~

Vulkan V3 requires Vulkan 1.2 or VK_KHR_timeline_semaphore and a descriptor set per slot. A slot whose previous token has not completed returns VK_NOT_READY; a new token that is already reached is rejected. FFG does not submit, wait, or signal and does not own image layout, queue ownership, or ready-semaphore waits. See [docs/FGDS_VULKAN.md](docs/FGDS_VULKAN.md).

#### D3D12/Vulkan resource states

- D3D12 inputs must be in NON_PIXEL_SHADER_RESOURCE; output must be an equal-size RGBA32_FLOAT UAV in UNORDERED_ACCESS. Inputs and output must not alias and must come from the same device.
- Vulkan image/views must use one device, queue family, mip/layer/sample, and real protocol format IDs; the current runtime requires VK_IMAGE_LAYOUT_GENERAL.
- The runtime rejects bad dimensions, device/format/layout mismatches, handle aliasing, versions, time order, non-zero jitter, and missing core resources. The host should present the latest real frame on failure.
- FFG does not read game memory, create host resources, call Present, or retain history across pairs. The host owns the lifetime of resources, command lists/allocators, contexts, and synchronization objects until completion.

The complete field, error, and lifetime contract is in [docs/FGDS.md](docs/FGDS.md) and [docs/FGDS_VULKAN.md](docs/FGDS_VULKAN.md).

### Capture mode: latency and quality boundary

The capture path is based on [Desktop Duplication](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api). FFG uses LastPresentTime, AccumulatedFrames, and QPC for continuity; after the D3D11 shared texture/fence handoff, conversion, scaling, bidirectional block matching, and warp are recorded in one D3D12 command list and one CPU fence wait occurs before the next AcquireNextFrame.

The default source-frame deadline is 24 ms. If a source frame is stale, the estimate predicts a deadline miss, or interpolation finishes too late, FFG presents the newest real frame and resets history instead of accumulating work. accumulated_skips, history_resets, deadline_skips, deadline_overruns, and mean_flow_submit_wait_ms describe only the capture path; they are not end-to-end input latency or game FPS.

Capture input remains Desktop Duplication BGRA8 SDR, with a selected window client rectangle fixed at startup. `--replace` covers the selected monitor with an FFG-owned borderless topmost window; `--hdr-output` is FP16 scRGB output, not HDR10/PQ source passthrough. Exclusive fullscreen, occluded/minimized windows, dynamic resize, access-lost recovery, mouse passthrough, and real Depth/Object ID/HUD still require a host or later implementation.

### Build and develop

Requirements: Windows 10/11 x64, Visual Studio C++, Windows SDK, and CMake 3.24+. Debug validation requires Graphics Tools. D3D12 is built by default. Vulkan requires Vulkan-Headers and glslangValidator; pass explicit paths when the SDK is not discoverable.

~~~powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel

.\build\Release\ffg_capture.exe --self-test
.\build\Release\ffg_demo.exe --headless --warp --debug
.\build\Release\ffg_flow_test.exe --stress --debug
ctest --test-dir build -C Release --output-on-failure  # 10 tests

cmake -S . -B build-vulkan-root -A x64 -DFFG_BUILD_VULKAN=ON -DFFG_VK_HEADERS="D:/path/to/Vulkan-Headers/include" -DFFG_GLSLANG="D:/path/to/glslangValidator.exe"
cmake --build build-vulkan-root --config Release --parallel
ctest --test-dir build-vulkan-root -C Release --output-on-failure  # 12 tests with Vulkan
~~~

ffg_demo validates native contracts and synthetic scenes; it is not an L4D2 renderer. --warp is for software regression only and is not a hardware performance result. See [docs/INSTALLER.md](docs/INSTALLER.md) for packaging and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the source layout.

### Validation and known limitations

As of 2026-10-01, local validation includes D3D12 CTest 10/10, an explicit Vulkan build with CTest 12/12, a real Vulkan V3 two-slot/timeline smoke test, D3D12 V3 busy-slot/reuse checks, HDR/shared protocol validation, RTX 3050 Desktop Duplication replacement and scRGB FP16 output, WARP and flow stress, and installer manifest/uninstall protection tests. Replacement output runs completed on the RTX 3050; internal wait timing is not end-to-end latency or a universal game-quality result.

Not yet verified or implemented: real game-host native integrations, a two-process game matrix, HDR10/PQ source passthrough, automatic resize/access-lost recovery, dense production optical flow, complex disocclusion/temporal reconstruction, exclusive game swapchain replacement, end-to-end input-latency measurement, VRR pacing, and VAC/anti-cheat compatibility. FFG does not claim that every game is validated, that latency is zero, or that bans are impossible.

### Documentation

- [Capture mode](docs/CAPTURE.md)
- [FGDS D3D12 protocol](docs/FGDS.md)
- [FGDS Vulkan protocol](docs/FGDS_VULKAN.md)
- [Installer, uninstaller, and shortcuts](docs/INSTALLER.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Roadmap](docs/ROADMAP.md)
- [Validation record](docs/VALIDATION.md)
- [v0.3.0-beta.1 release notes](docs/RELEASE_NOTES_v0.3.0-beta.1.md)
- [v0.4.0 first public release notes](docs/RELEASE_NOTES_v0.4.0.md)
- [Stable-release gate](docs/RELEASE_GATE.md)
- [Uninstall and VAC boundary](docs/UNINSTALL_AND_VAC.md)

---

FreeFrameGen is experimental software. Test it on a copy of your game configuration, keep the latest real frame as a fallback, and report reproducible captures or native-contract failures with the command line, GPU, driver, and validation output.
