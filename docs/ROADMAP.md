# 阶段门槛

| 阶段 | 验收 | 当前 |
|---|---|---|
| P0 独立 D3D12 / FGDS | 编译、硬件与 WARP、debug 无告警、可复现输出 | 已建立原型 |
| P0 FFG 中间帧 | 比线性 blend 更接近真实中点；端点/cut/错误输入 | 合成平移场景通过 |
| P0 安装卸载 | 清单、哈希、修改文件保护、越界拒绝 | 脚本与测试 |
| P1 游戏边界 | 真实离线 tracer、线程/资源/提交覆盖，静态世界与模型最小接入 | 未实现 |
| P1 FG 质量 | 多物体/镜头/薄片/快速运动/遮挡统计，GPU holes/confidence/reconstruction | 基础候选/深度检查，其余未实现 |
| P2 资源和调度 | 多在途帧安全、worker command lists、GPU skinning/culling/indirect、帧时间分析 | 未实现 |
| P2 Capture | 用户选窗口、Windows 捕获、GPU optical flow、丢帧/resize | v0.2 显示器捕获、可见窗口客户端区域选择、GPU 块匹配与合并提交低延迟路径；动态 resize/自动恢复/稠密光流未完成 |
| P3 DXR / DLSS | 正确几何/材质/灯光、BLAS/TLAS、时域输入、硬件能力降级 | 未实现 |
| P3 FFG latency | deadline/VRR/调度、端到端测量与原生基线 | 未实现 |
| P4 SDK / FGDS | 能力协商、IPC handles/fences、版本兼容、安全与文档 | 同进程 D3D12/Vulkan V2/V3 多槽协商和接入文档；跨进程 handles/fences 与稳定标准未完成 |
| 发布门槛 | 回滚实测、长期稳定、画质/性能报告、明确反作弊边界 | 不可当完整游戏版发布 |

每阶段分别报告“代码已写、编译通过、合成 GPU 验证、真实游戏验证”；不得互相代替。
