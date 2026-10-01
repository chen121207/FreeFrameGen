# FFG v0.3：真实捕获插帧预览

## 入口和边界

`ffg_capture.exe` 是独立程序，使用 Windows Desktop Duplication。无参启动先列出显示器并等待输入编号；输入后才捕获。安装/启动不会自动选择游戏、改启动项或注入 DLL。
默认采集整块选定显示器，可能包括其他应用和通知；也可用 `--list-windows` 列出可见窗口并用 `--window N` 选择其客户端矩形（仍受 Desktop Duplication 的遮挡/保护内容限制）。没有网络功能，不将捕获像素读回 CPU、不保存/上传画面。测试程序的合成图片读回与实际捕获是分开的。

要求 Windows 10 2004+/11、D3D12 GPU、支持 D3D11 shared fence 的驱动、SDR 横向显示器。同一显示器所在适配器同时创建 D3D11/D3D12 设备；不跨显卡传图。HDR/宽色域、旋转显示器明确拒绝，不靠错误色彩继续显示。

```powershell
ffg_capture.exe                         # 交互选择显示器，直到关闭
ffg_capture.exe --list                  # 仅列设备，不捕获
ffg_capture.exe --list-windows          # 列出可见窗口客户端区域
ffg_capture.exe --window 0 --seconds 30 # 选择窗口并捕获其客户端矩形
ffg_capture.exe --output 0 --seconds 30  # 指定设备，限时预览
ffg_capture.exe --output 0 --width 1280  # 处理宽度 320..1920，默认 960；不超过源宽度
ffg_capture.exe --output 0 --seconds 5 --headless --debug
ffg_capture.exe --output 0 --seconds 5 --headless --deadline-ms 16
ffg_capture.exe --self-test           # scheduler/history guard; no display access
ffg_capture.exe --help
```

预览窗口获得焦点时：空格比较 FG ON/OFF，Esc 停止；关闭窗口也会停止。控制台 Ctrl+C 可终止。
`--headless` 必须有明确 `--output` 或 `--window` 和有限 `--seconds`，只用于诊断，不会偷偷无限捕获。`--self-test` 只验证捕获调度的历史连续性，不访问显示器。`--debug` 需要系统 Graphics Tools。

建议先让游戏窗口化/无边框，启动预览观察；**这不是可覆盖游戏全屏操作的成品**，没有鼠标透传或全屏输出。窗口选择只固定启动时的客户端矩形，遮挡、最小化、动态 resize、保护内容、独占全屏和桌面切换可能不可捕获，不绕过系统保护。

## 实际数据路径

1. DuplicateOutput/AcquireNextFrame 取得 BGRA8 纹理和 QPC 时间戳，过滤仅鼠标移动的更新。
2. D3D11 CopyResource 到共享显存；D3D11 Signal/Flush，D3D12 queue Wait 同一个 shared fence。
3. D3D12 compute：SRGB SRV 解码、双线性缩放到工作分辨率；保留前后两帧线性 RGBA32F。
4. GPU 4×4 均值下采样，1/4 分辨率粗搜索（±16 工作像素、步长 4）。粗向量接近零或全分辨率残差较高时，再做竞争种子搜索（±8、步长 2）；最后每 8×8 像素做 ±3 局部细化。
5. 三次 inverse warp，匹配残差/坐标/往返一致性检查；颜色一致时混合，无效时取可信端点或新帧。
6. 原始旧帧 A → 生成的 A/B 中间帧 → 下一对旧帧 B；D3D12 swapchain vsync 呈现。输出做线性→sRGB 编码，不再简单套 2.2 gamma。

没有真实 Depth/Object ID/HUD，因此不向 FGDS 原生接口填假数据；当前 `ColorFlow` 是内部模块，尚未导出 Capture SDK ABI。
没有神经模型、Lossless Scaling、Remix、DLSS，也没有使用 RT Core——此项运动估计是普通 GPU compute 工作，不是光线追踪工作。

## 防反馈、生命周期和错误

- 输出窗口显示前设置并检查 `WDA_EXCLUDEFROMCAPTURE`，失败就停止，避免递归套娃。它不是 DRM/安全承诺；不同驱动下排除效果仍需用户观察。
- 共享纹理返回 COMMON；每次 D3D12 消费完成才允许下次 D3D11 写入。纹理和描述符持久复用，不逐帧创建资源。
- 当前捕获链路保持单在途，使用一次 CPU fence wait 保证资源所有权。转色和插值现在记录在同一个 D3D12 command list 中，避免每个源帧在两段 GPU 工作之间额外 drain 一次；等待仍发生在下一次 Desktop Duplication AcquireNextFrame 之前，因此共享纹理不会在 D3D12 读完前被 D3D11 覆盖。这是可证明正确的低延迟增量，不等同于多槽完全异步调度。
- 默认启用 24 ms 的源帧年龄 deadline，可用 `--deadline-ms 4..100` 调整。每次取得桌面帧后用 `LastPresentTime` 与 QPC 比较；源帧已经过期或上一轮插值预测会超出预算时，只记录转色并显示最新真实帧，不再把旧帧送进插值。插值实际完成后若仍超过预算，丢弃旧的真实帧/中间帧呈现，直接显示当前真实帧。这样不会在 GPU 变慢时积累工作，也不会为判断 deadline 提前释放 D3D11/D3D12 共享纹理所有权。
- 当 `AccumulatedFrames > 1` 时，消费者已经跳过至少一个桌面帧；调度器丢弃这一对插值并把最新帧设为新的历史起点，避免把未知数量的源帧错误拉伸成一次运动。时间戳倒退、重复或超过 250 ms 同样重置历史。`accumulated_skips` 与 `history_resets` 会分别反映这些情况。
- 超过 250 ms 的源时间间隔、时间戳倒退会重置历史，不在不连续帧之间插值。
- 捕获超时继续消息循环；access lost/显示模式变化/锁屏导致错误时退出并提示重启。尚未自动恢复或动态 resize。
- 不合成独立鼠标指针层；系统硬件光标可能不在输出内。

## 性能与画质限制

标题显示收到的桌面帧和生成帧计数，不是游戏 FPS。退出输出 `accumulated_skips` 是桌面捕获累计跳过更新，不代表游戏丢帧。
`mean_flow_submit_wait_ms` 包括颜色转换、下采样/运动估计/warp 以及合并提交的 CPU 等待，不含捕获和 Present，不是纯 GPU 时间，也不是输入到显示延迟。
输入帧率高于消费能力时只取最近桌面状态，没有无限队列，但会跳过源帧；`deadline_skips` 表示因源帧年龄或预测超时而跳过插值，`deadline_overruns` 表示插值已完成但结果过期而被丢弃。该策略仍不是 VRR、动态质量档位或与源帧率同步。
插值等待下一真实帧，增加延迟；不能宣称“2×显示等于2×响应速度”。FG OFF 预览同样不是直接游戏呈现的延迟基线。

搜索范围以**工作分辨率像素**计。高速转镜头、遮挡、细线、透明物、动态灯光、HUD、重复纹理可能出现错位、残影和端点跳变。没有完整场景切换检测；当前仅低匹配可信度端点回退。
高频噪声平移 (6,-2) 的 `ffg_flow_test --stress` 已通过当前回归门槛，覆盖的是合成压力图；高速转镜头、遮挡、细线、动态灯光、HUD 和透明边缘仍需要真实游戏验收。不得把标准测试通过说成通用游戏画质已通过。

## 官方接口依据

- [Desktop Duplication](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api)
- [D3D12 CreateSharedHandle：D3D11 资源/栅栏互操作](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createsharedhandle)
- [SetWindowDisplayAffinity](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)

不修改游戏并不等于 Valve 或任何反作弊厂商认证；没有“VAC 绝对不封”的保证。
