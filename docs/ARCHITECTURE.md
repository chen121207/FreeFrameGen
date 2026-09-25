# FreeFrameGen 架构与算法边界

## 两个独立入口

```text
Native（本版原型）：
Producer -> Color/Depth/bidirectional MV/Object ID -> FGDS -> GPU interpolation -> Host Present

Capture（尚未实现）：
Windows Graphics Capture / Desktop Duplication
  -> 连续帧、时间戳 -> GPU optical flow / confidence
  -> FFG reconstruction -> FFG-owned output window
```

Capture 不伪造不存在的引擎 Depth/Object ID；必须另设 capability flags / descriptor，而非把当前 Native 契约填零。
捕获只处理用户选择的窗口/显示器，遵守 OS 保护内容限制，不读取游戏内存，不做注入。
捕获输出不能反过来再次捕获自身。丢帧、resize、受保护窗口、最小化都需要明确状态机。

## v0 compute

1. 两端点颜色/线性深度/MV/ID 已驻 GPU。
2. 每目标像素在两端点分别解 source + phase * motion(source) = target。
3. 多种子、三次固定点迭代；水平 seeds +-32 px、垂直 +-16 px。不是无限运动搜索。
4. 检查坐标范围、finite、有效 ID、残差；可见端点检查 ID/depth/往返 MV，一端遮挡保留另端候选。
5. 按正线性深度选择近表面；身份匹配才按时间混合。
6. 两端都失败：取新帧同像素；这会拖影/跳变，不是完整 hole reconstruction。
7. Camera cut 直接输出新帧；端点 alpha=0/1 精确复制。

此方法局限于测试覆盖的低复杂度刚体平移，不宣称正确解决相机透视/旋转、薄片、细线、粒子、透明、快速运动和动态光照。
后续独立实现 confidence/hole mask、空间重建、历史拒绝与测试集，不先靠 AI 隐藏问题。

## 延迟时间线

```text
真实帧 A 完成 --- 真实帧 B 完成 --- 才能计算 A/B 中间帧
                  等待 + compute + pacing + scanout
```

插值不是预测。为了保持 A -> middle -> B 时间顺序，必须有呈现延迟预算，不能把 B 先显示再补中间帧。
目标是减少额外排队和等待，而不是声称消除未来帧需求。输入延迟仍由游戏逻辑/采样率决定。

未来 scheduler 需要：
- source timestamp、GPU timestamp、present statistics、显示刷新率和 deadline；
- 限制在途帧，deadline miss 跳过生成而非增加排队；
- VRR/vsync 模式、生产/消费 fence、minimize/resize/cut、丢帧/恢复；
- 与原生不插帧 baseline 比较 median/p95/p99 frametime、GPU compute 耗时、端到端输入到显示；
- 原生/捕获不同成本。不可拿“2x显示帧”当“延迟减半”。

本版只设置 DXGI waitable swapchain + maximum frame latency=1，Demo 每次提交 fence wait，用于正确性，不是完成调度。

## API 和模块

- include/fgds：格式/时间/所有权契约草案。
- include/ffg + src/ffg.cpp：最小 in-process D3D12 DLL SDK。
- shaders/interpolate.hlsl：独立 GPU 算法。
- support/scene_renderer.hpp：合成输入 fixture，不属于实际游戏兼容层。
- src/demo.cpp：独立 host/present 与数值回归。
- future capture、scheduler、IPC、advanced reconstruction：尚无实现。

Windows 捕获一手资料：
https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api
