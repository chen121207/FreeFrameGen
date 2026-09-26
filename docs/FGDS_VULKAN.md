# FGDS Vulkan 0.1

include/fgds/fgds_vk.h 是 FFG 的 Vulkan 原生输入契约。它与现有
D3D12 版 FGDS 分开，避免把 DXVK 的 Vulkan 设备强行转换成 D3D12 设备。

当前阶段固定以下边界：

- Color、Depth、Motion、Object ID 使用 Vulkan image/view 句柄和显式
  VkImageLayout 数值描述；句柄以 uint64_t 编码，允许 x86/x64 共享头文件。
- 两个端点必须来自同一个 Vulkan device、队列族和尺寸，时间戳/帧号严格递增。
- ready 预留给 timeline semaphore；消费方只能在等待完成后读取资源。
- HUD 和透明度 mask 可以为空；不能为了满足接口伪造数据。
- `vulkan/` 目录现在包含一个独立的 Windows Vulkan compute runtime。它只
  记录命令，不拥有 queue、submit 或 present；调用方负责布局、队列所有权
  和 ready semaphore 等待。当前 shader 是双向重投影加深度/ID 遮挡处理，
  空洞使用最新帧回退，不包含光流或神经网络增强。
- 这是可构建的实验运行时，不等于已经接入 L4D2，也不宣称已经完成游戏内
  的低延迟呈现路径。

DXVK 的下一步仍是输出中间帧所需的 Color/Depth/Camera 元数据，再接入
motion/object-id 采集和实际游戏内 Vulkan compute pass。
