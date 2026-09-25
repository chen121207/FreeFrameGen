# FreeFrameGen (FFG)

Windows-only 的独立帧生成软件 / Runtime 研究项目。帧生成和低延迟调度属于同一软件；与 [L4D2 D3D12 Renderer](https://github.com/chen121207/l4d2-d3d12-renderer) 解耦。

**当前 v0.1 是可运行的 D3D12 合成场景 Demo 与最小 DLL SDK，不是已经可给任意游戏插帧的产品。**

## 已实现

- 原生 D3D12 compute 中间帧生成，无大型 AI 模型、不依赖 Lossless Scaling/Remix/DLSS。
- 线性 Color + Depth + 两端点双向 Motion Vector + Object ID。
- 有界多种子 inverse warp、深度择近、可见端点的 Object ID/双向 MV 一致性检查。
- alpha 两端点原样输出、camera cut 输出新帧；未解决洞使用新帧对应像素回退（尚非完整重建）。
- 独立 `FreeFrameGen.dll` 的 create/record/destroy API；应用持有设备、纹理、提交与 Present。
- demo 按“旧真实帧 -> 中间帧 -> 下一对旧真实帧”顺序显示。
- CPU 基准/真实中间时刻 GPU 渲染对照、错误契约拒绝、debug-layer 检查与便携卸载。

## 还没有

普通游戏屏幕捕获、Optical Flow、HDR、HUD/透明/粒子处理、通用透视运动、复杂反遮挡/temporal reconstruction、AI refinement、生产级低延迟调度、跨进程 IPC。
FGDS 只是 0.1 草案，并非成熟标准；两帧都必须有 motionToOther。不要把单向 MV 当双向数据。

## 构建 / 运行

Windows 10/11 + Visual Studio C++ + Windows SDK + CMake 3.24+。debug 模式需要 Graphics Tools。
```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
.\build\Release\ffg_demo.exe --debug
.\build\Release\ffg_demo.exe --headless --debug
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix build/package
```

默认硬件 GPU；`--warp` 只用于软件回归测试。窗口默认运行 120 对帧，`--frames N` 可调整。
输出 `out/real_0.bmp`、`real_1.bmp`、`generated.bmp`、`ground_truth.bmp` 和 `blend_baseline.bmp`。
`support/scene_renderer.hpp` 是 FFG 自带的原创合成测试源，**不链接另一个项目，也不读取 L4D2**。

## SDK 与延迟边界

`include/ffg/ffg.h`：`ffgCreate`、`ffgRecord`、`ffgDestroy`。
`ffgRecord` 只往调用者命令列表记录 compute，不做 CPU 像素处理、等待或呈现。详见 [FGDS](docs/FGDS.md)。

双帧插值需要未来端点，天然存在等待；提高显示帧率不等于提高游戏逻辑/输入采样频率，也不等于降低输入延迟。
demo 刻意逐提交等待 GPU fence，尚未进行端到端延迟测量；不能拿其速度宣传低延迟或性能收益。

## 文档与安装

- [算法、时间线、Native/Capture 两条路径](docs/ARCHITECTURE.md)
- [FGDS](docs/FGDS.md)
- [阶段路线图](docs/ROADMAP.md)
- [卸载与 VAC](docs/UNINSTALL_AND_VAC.md)
- [验证记录](docs/VALIDATION.md)

只支持独立目录便携安装，不修改游戏、不安装服务/驱动。便携包管理脚本在 tools 中。
