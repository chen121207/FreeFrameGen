# FGDS 0.1 / 0.2 / 0.3 草案：同进程 D3D12 输入契约

这是两个实验项目之间的最小约定，不是正式行业标准。FFG 是当前协议头文件
`include/fgds/fgds.h` 的维护来源。先前 v0.1 快照曾与 Renderer 仓库同步；本次
v0.3 扩展只在 FFG 仓库开发，尚未核对另一仓库的头文件哈希。Renderer 实际接入
V3 前，需要复制兼容头文件并重新编译其调用方。
不依赖 Steam、L4D2、Remix、Lossless Scaling、DLSS 或 NVIDIA 私有 API。

## 坐标与格式

| 数据 | v0 格式与含义 |
|---|---|
| Color | RGBA32_FLOAT，线性 Rec.709，不含 HUD，不做 sRGB 二次解码，alpha=1 |
| Depth | R32_FLOAT，正数线性 view distance，越小越近；不能直接塞反向 Z/硬件深度 |
| Motion | RG32_FLOAT，像素单位，+X 向右，+Y 向下；this -> other endpoint |
| Object ID | R32_UINT，跨两端点稳定非零 ID，0 表示无效 |
| Camera | row-major、row-vector worldToClip，未 jitter；运动向量已包含相机运动，不重复补偿 |
| HUD / transparency | 预留 R8_UNORM；v0 必须为空，非空会返回 E_INVALIDARG |
| Output | RGBA32_FLOAT UAV，等尺寸，非输入别名 |

第一帧 motion 是 older -> newer，第二帧 motion 是 newer -> older。上一对的 backward MV **不等于**本对的 forward MV。
本 demo 通过已知两个端点的物体位置计算双向 MV。实际引擎要在两帧就绪后提供对应方向，或将来增加显式的 motion inversion pass。
v0 只接受无 jitter；相机矩阵用于描述/后续扩展，当前 kernel 不使用矩阵替代完整运动场。
相机旋转/透视深度变化尚未验收；当前 depth consistency 检查可能保守拒绝这些像素。

## 时间与重置

两个 frameId、timestampNs 严格递增；时间戳来自同一单调时钟。alpha 在 [0,1]。
任一帧设置 CAMERA_CUT 时输出新帧；尺寸变化应由宿主丢弃旧 pair 并重新创建资源。
接口不保存跨 pair history，不存在偷偷积累的历史帧。
透明/粒子/HUD 不支持时必须事先剥离或禁用插帧；当前 demo 无这些内容，不伪造 mask。

## 所有权、线程、同步

- 这是本机同进程 API，void* 实际为 ID3D12Resource*；不能序列化指针用于 x86/x64 或跨进程通信。
- 纹理均同设备、2D、1 mip、1 array、1 sample；API 校验格式、尺寸、设备、ID、时间及 flags。
- 全部输入在 NON_PIXEL_SHADER_RESOURCE，输出在 UNORDERED_ACCESS；输入状态不变，输出仍为 UAV，并记录 UAV barrier。
- 调用者必须保持资源、command allocator/list、FFG context 存活直到 GPU fence 完成。
- 单 context 单在途 dispatch。只有 fence 完成后才允许再次 record、改 descriptor 或 destroy；禁止多线程并发调用。
- ffgRecord 只记录 GPU 工作，不提交、不等待、不 Present、不做每帧 CPU readback；修改调用者的 compute root/PSO/descriptor heaps，调用者后续自行重绑。
- demo 为便于验证每次提交都等 fence，**不是低延迟最佳实践**。
- 真正跨进程版未来使用共享纹理 HANDLE、adapter LUID、共享 fence/value、双向消费确认、版本与超时；尚未实现。

## v0.2 原生 SDK 协商（D3D12）

v0.1 的 `FgdsFrame`、`FgdsPair` 和 `ffgRecord` 是冻结 ABI。不要在这两个
结构体末尾追加字段，也不要把旧调用改成可变 `sizeof`。新游戏应包含同一套
头文件后，先调用：

```cpp
FgdsCapabilities caps{sizeof caps};
HRESULT hr = ffgGetCapabilities(context, &caps);
```

`caps.requiredResources` 当前为 Color、Depth、Motion、Object ID 四项，格式
仍分别是 `RGBA32_FLOAT`、`R32_FLOAT`、`RG32_FLOAT` 和 `R32_UINT`；最大尺寸为
16384×16384。`caps.featureFlags` 只报告当前 kernel 实际消费的双向运动、深度
遮挡、Object ID 检查和 camera cut。`caps.optionalResources` 目前为 0。

需要显式资源位和版本协商的调用可使用 append-only 的 `FgdsFrameV2`/
`FgdsPairV2` 与 `ffgRecordV2`：

```cpp
FgdsPairV2 pair{};
pair.structSize = sizeof pair;
pair.version = FGDS_VERSION_0_2;
// 填充两端点，resourceFlags 至少包含 FGDS_RESOURCE_CORE
HRESULT hr = ffgRecordV2(context, commandList, &pair, output);
```

V2 会校验资源位与指针、时间线、尺寸、矩阵和未 jitter 约束，然后复用与
v0.1 相同的 GPU kernel。HUD/透明度 mask 字段已经定义，但当前能力未声明它们；
提供这些 mask 会返回 `E_NOTIMPL`，不会被静默忽略或当作已处理。游戏应在收到
该结果后剥离这些内容、回退到真实帧，或继续使用 v0.1 等价输入。这样可以在
将来增加 mask-aware kernel 时保持结构布局和错误协商方式不变。

原生模式仍是同进程、同一 D3D12 device 的借用资源接口。它不负责捕获、注入、
提交、等待、Present 或跨进程句柄；游戏/引擎必须持有资源和命令列表直到 fence
完成，并按 `ffgRecord` 的状态和单飞约束自行调度。

## v0.3 多在途记录（D3D12）

v0.3 保留 v0.1/v0.2 的所有结构和入口，新增 `FfgContextV3`、
`ffgCreateV3`、`ffgGetCapabilitiesV3`、`ffgRecordV3`。V3 的输入仍是
`FgdsPairV2`，因此真实数据要求不变：两端点必须来自同一个设备和同一尺寸，
Color/Depth/双向 Motion/Object ID 必须按上文格式填充，时间戳和 frameId 严格递增，
运动向量必须已经包含相机运动；V3 不会从历史帧猜测缺失数据，也不接受尚未声明的
HUD/透明 mask。

创建时指定 1 到 `FGDS_V3_MAX_SLOTS`（当前 8）的 slot 数量：

```cpp
FfgContextV3 *ctx = nullptr;
ffgCreateV3(device, 3, &ctx);
FgdsCapabilitiesV3 caps{sizeof caps};
ffgGetCapabilitiesV3(ctx, &caps);
```

每个 slot 都有独立的 shader-visible SRV/UAV descriptor heap。`ffgRecordV3` 只向
调用者的 command list 记录 dispatch，不提交、不等待，也不替调用者发 signal：

```cpp
uint64_t signalValue = nextFenceValue; // 由宿主在提交后 Signal
HRESULT hr = ffgRecordV3(ctx, list, &pair, output, slotIndex,
                         queueFence, signalValue);
```

`signalValue` 必须是该 command list 执行完后由宿主在同一设备 queue 上 signal 的值。
FFG 记录成功后把这个 fence/value 绑定到 slot；下次复用前它会读取
`GetCompletedValue()`。若前一个值尚未完成，返回 `DXGI_ERROR_WAS_STILL_DRAWING`，
不会改写 descriptor、不会写入 command list，也不会 CPU 等待。`completionFence` 和
`completionValue` 在每次调用（包括首次使用）都必须非空、非零；否则返回
`E_INVALIDARG`；它还必须大于该 fence 当前的 `GetCompletedValue()`，否则它无法
代表本次尚未执行的 dispatch，也会返回 `E_INVALIDARG`。FFG 不验证宿主未来是否真的
signal，因此宿主必须保证 signal 一定发生，否则 slot 会保持 busy。

输入纹理、输出纹理、command allocator/list 以及 queue fence 的生命周期仍由宿主
负责，至少保持到对应 signal 完成；输出仍为 UAV，调用会覆盖 compute root/PSO/
descriptor heap 状态并追加 UAV barrier，宿主随后自行恢复。不同 slot 可在不同尚未
完成的 command list 上并行记录，从而避免 v0.1/v0.2 的 descriptor 重用竞态；同一
slot 仍禁止并发调用。`ffgDestroyV3` 前必须等待所有已绑定 fence 完成。

V3 的能力结构会报告实际 `slotCount`、`maxSlots` 和 `FGDS_FEATURE_MULTI_FLIGHT`。
它没有改变 shader 算法或跨进程语义；多 slot 只解决 descriptor/调度在途安全，不能
替代游戏提供真实、同步且与端点严格对应的 motion/depth/object 数据。
