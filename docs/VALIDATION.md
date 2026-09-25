# 本机验证 — 2026-09-25

环境：Windows 11 (10.0.26220)、MSVC 19.51.36257 / VS 2026、Windows SDK 10.0.26100.0、CMake 4.3.1。
硬件：NVIDIA GeForce RTX 3050；另用 Microsoft WARP 软件设备作回归。硬件结果与 WARP 结果分开验证，不混称 GPU 加速。
Release 使用静态 MSVC runtime；系统 D3D12/DXGI/D3DCompiler 为动态系统依赖。

## 已实际执行

- Release x64 编译，/W4 /WX，通过。
- 硬件 --headless --debug，通过。
- WARP --headless --debug，通过。
- 窗口程序 --debug --frames 12 实际创建 DXGI swapchain / Present / 正常退出，通过；这不是人眼长时间画质验收。
- D3D12 debug layer 检查所有 warning/error/corruption：通过。早期 clear-value 警告已经修正，没有忽略告警。
- C 编译头文件测试，通过。
- CMake install 到 package + 便携安装/卸载测试：正常卸载、修改文件保留、清单路径越界拒绝，通过。
- 两仓库 FGDS 头文件相同；Renderer 通过外部 FFG package 的头文件/import lib/DLL 调用成功，平移中点 MAE=0。
- 输出 BMP 文件已生成，并检查生成帧（橙/绿物体、棋盘背景）。不是 L4D2 截图。

## FFG 合成数值结果

MAE 计算在 512x288 线性 RGBA 上，包括 alpha，不是感知质量或真实游戏指标。

| 场景 | FFG vs 独立渲染中点 MAE | 简单两帧平均 vs 中点 MAE |
|---|---:|---:|
| 静止 | 0 | 0 |
| 水平平移 | 0 | 0.00378543 |
| 遮挡交叠 | 0.000291843 | 0.00426897 |
| 反向移动 | 0 | 0.00378543 |

另外验证 alpha=0 / alpha=1 精确端点；camera cut 输出新帧；拒绝错误版本、尺寸、NaN alpha、逆序时间、非零 jitter、输入输出别名。
这些场景颜色简单、运动已知；不能外推为通用画质保证或声称遮挡问题全部解决。

## 尚未验证 / 未实现

- L4D2 中接入、地图/模型/材质正确性、真实游戏线程/资源生命周期。
- DXR 光追、DLSS、GPU 蒙皮/culling/indirect、多线程性能。
- 捕获普通游戏、Optical Flow、复杂重建、端到端输入延迟、帧率收益。
- VAC 服务器兼容或任何“不会封禁”保证。
- 游戏目录安装/回滚：本版无此安装器，便携卸载不代表已经支持旧 Remix 卸载。

GitHub Actions 工作流已提供；本文件只记录本机执行结果，不预先声称云端 CI 通过。
