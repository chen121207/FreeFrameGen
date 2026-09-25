# FGDS 0.1 草案：同进程 D3D12 输入契约

这是两个实验项目之间的最小约定，不是正式行业标准。头文件 `include/fgds/fgds.h` 在两个仓库中保存相同快照，FFG 是当前维护来源；修改时必须同步并验证 SHA256。
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
