# FGDS Vulkan 0.1 / 0.2 / 0.3

include/fgds/fgds_vk.h 是 FFG 的 Vulkan 原生输入契约。它与现有
D3D12 版 FGDS 分开，避免把 DXVK 的 Vulkan 设备强行转换成 D3D12 设备。

当前阶段固定以下边界：

- Color、Depth、Motion、Object ID 使用 Vulkan image/view 句柄和显式
  VkImageLayout 数值描述；句柄以 uint64_t 编码，允许 x86/x64 共享头文件。
- `FgdsVkImage.format` 是 `FGDS_VK_FORMAT_*` 协议 ID，不能填 `VkFormat`
  枚举值。Color/Output 用 `FGDS_VK_FORMAT_RGBA32_FLOAT`，Depth 用
  `FGDS_VK_FORMAT_R32_FLOAT`，Motion 用 `FGDS_VK_FORMAT_RG32_FLOAT`，
  Object ID 用 `FGDS_VK_FORMAT_R32_UINT`；所有 image layout 必须是
  `VK_IMAGE_LAYOUT_GENERAL`。当前 shader 没有 jitter 重建，两个 frame 的
  `jitterPixels` 必须都是 `(0,0)`。
- 两个端点必须来自同一个 Vulkan device、队列族和尺寸，时间戳/帧号严格递增。
- ready 预留给 timeline semaphore；消费方只能在等待完成后读取资源。
- V1 `FgdsVkFrame` 的 HUD/透明度 mask 必须完全为空；若任一 mask image/view
  或元数据非零，`ffgVkRecord` 会拒绝调用，避免旧接口静默忽略资源。V2/V3
  可以描述 mask，但当前 kernel 仍会返回 `VK_ERROR_FEATURE_NOT_PRESENT`。
- `vulkan/` 目录现在包含一个独立的 Windows Vulkan compute runtime。它只
  记录命令，不拥有 queue、submit 或 present；调用方负责布局、队列所有权
  和 ready semaphore 等待。当前 shader 是双向重投影加深度/ID 遮挡处理，
  空洞使用最新帧回退，不包含光流或神经网络增强。
- runtime 检查声明的协议格式 ID、布局、零 jitter，并拒绝 Output 与任一
  输入的相同 image 或 view 句柄。Vulkan 不提供从这些句柄直接读取真实格式、
  extent 或绑定内存的能力；真实资源必须与声明相符，输出与输入也不能共享
  底层内存，这由游戏接入方保证。
- 这是可构建的实验运行时，不等于已经接入 L4D2，也不宣称已经完成游戏内
  的低延迟呈现路径。

DXVK 的下一步仍是输出中间帧所需的 Color/Depth/Camera 元数据，再接入
motion/object-id 采集和实际游戏内 Vulkan compute pass。

## v0.2 原生 SDK 协商

`FgdsVkFrame`、`FgdsVkPair` 和 `ffgVkRecord` 的 v0.1 ABI 已冻结。新调用方
可以在创建 context 后查询：

```cpp
FgdsVkCapabilities caps{sizeof caps};
VkResult result = ffgVkGetCapabilities(context, &caps);
```

当前 `requiredResources` 为 Color、Depth、双向 Motion 和 Object ID 四项；
`optionalResources` 为 0。句柄、布局、设备和队列族要求仍由调用方保证，runtime
只会记录 compute 命令，不替调用方等待 `ready` timeline semaphore。

需要显式资源位的调用可使用 append-only `FgdsVkFrameV2`/`FgdsVkPairV2` 与
`ffgVkRecordV2`。V2 无 mask 时转换到同一 v0.1 kernel；如果设置 HUD 或
transparency mask，返回 `VK_ERROR_FEATURE_NOT_PRESENT`，不会静默丢弃 mask。
后续 mask-aware kernel 可以在不改变 v0.1 布局的情况下协商启用。

## v0.3 多在途录制协议

V0.3 不修改任何 v0.1/v0.2 结构，也不改变 `ffgVkRecord` 或
`ffgVkRecordV2` 的行为。需要同时录制多个尚未提交/尚未完成的 command
buffer 时，创建独立的 V3 context：

```cpp
FfgVkContextV3* context = nullptr;
VkResult r = ffgVkCreateV3(physicalDevice, device, queueFamily,
                           vkGetDeviceProcAddr, 3, &context);
FgdsVkCapabilitiesV3 caps{sizeof caps};
r = ffgVkGetCapabilitiesV3(context, &caps);
```

`slotCount` 范围为 1..8。每个 slot 有独立的 descriptor pool/set，因此在
slot 0 的 dispatch 仍在 GPU 上执行时，CPU 可以把另一对帧写入 slot 1，不会
改写 slot 0 正在读取的 descriptor。`ffgVkRecordV3` 的参数与 V2 相同，附加
`slotIndex` 和一个必需的完成令牌：

```cpp
r = ffgVkRecordV3(context, commandBuffer, &pair, &output,
                   slotIndex, completionTimeline, completionValue);
```

完成令牌是调用方拥有的 Vulkan timeline semaphore 和 signal value，**每次
调用都必须提供非空 semaphore 和非零 value**。FFG 只记录 compute 命令，
不提交、不等待，也不发出 signal；应用必须在提交该 command buffer 的同一次
`vkQueueSubmit(2)` 中按顺序 signal `completionValue`。FFG 在录制前查询新
令牌的当前值；如果当前值已经大于或等于 `completionValue`，返回
`VK_ERROR_VALIDATION_FAILED_EXT`，避免把已完成的旧值误当成未来完成点。重用
slot 时，FFG 还查询上一次令牌：

- 前一次令牌尚未达到时返回 `VK_NOT_READY`，调用方应改用其他 slot 或稍后重试；
- 已达到时允许重用，并把新的、尚未达到的令牌替换到该 slot。

V3 context 创建要求设备支持 `vkGetSemaphoreCounterValue`（Vulkan 1.2 或
`VK_KHR_timeline_semaphore`）。timeline semaphore 必须属于同一个
`VkDevice`，signal value 应严格按应用的提交顺序递增；Vulkan 句柄、队列
族、资源布局、队列所有权和 `ready` 等待仍由调用方负责。销毁 context 前，
调用方必须确认所有 slot 的完成令牌已达到，之后才可调用
`ffgVkDestroyV3`。

V3 仍拒绝 HUD/transparency mask（返回 `VK_ERROR_FEATURE_NOT_PRESENT`），
因为当前 shader 没有 mask-aware 路径。`FgdsVkCapabilitiesV3.featureFlags`
只有在可创建 V3 context 时才包含 `FGDS_VK_FEATURE_MULTI_FLIGHT`。
