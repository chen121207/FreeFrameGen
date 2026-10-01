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
- 真实游戏画质验收、高质量稠密 Optical Flow、复杂重建、端到端输入延迟、帧率收益。v0.2 的桌面捕获链路验证见下文。
- VAC 服务器兼容或任何“不会封禁”保证。
- 游戏目录安装/回滚：本版无此安装器，便携卸载不代表已经支持旧 Remix 卸载。

GitHub Actions 工作流已提供；本文件只记录本机执行结果，不预先声称云端 CI 通过。

## 中文 EXE 安装器（2026-09-25）

- 使用系统 .NET Framework C# 编译器，以 x64 WinForms 程序内嵌 ZIP 和 SHA256 清单；未获取外部打包工具。
- 安装界面离屏绘制后逐图检查，中文说明、路径、按钮显示完整。不是对所有 DPI/主题的全面验证。
- 真实静默安装、开始菜单双快捷方式、HKCU 已安装应用登记、安装后 Demo 运行，通过。
- 调用安装目录里的 Uninstall.exe（不传 --uninstall），验证临时工作副本、移除程序/卸载器/注册项/快捷方式，通过。
- 拒绝游戏目录、已有目录、重复安装、修改文件、损坏安装记录、占用/只读文件；保留用户额外文件。
- 测试安装使用独立测试目录并已卸载，不改游戏。测试日志与预览保存在 build/installer-tests 下。
- 安装器和卸载器未数字签名；当前仅支持本 Demo/Runtime 的安装，不是游戏 Renderer 安装/回滚证明。
- 本次修改包含安装器 CI 步骤；在新提交上实际运行前，不将其计为已通过的云端结果。

## v0.2 Capture 本机验证（2026-09-25）

- `/W4 /WX` Release 构建通过；CTest 5/5（Native WARP、color-flow WARP、C ABI header、捕获帮助、无效分辨率拒绝）通过。
- RTX 3050 `ffg_flow_test --debug`：只给颜色，不给真实运动矢量。181×105 奇数尺寸测试；质量 MAE 只统计去掉 32 像素边界的 RGB 内部区域，端点则检查全图。
- 静止、(8,4)、(-8,-4)、(4,-8) 的高频纹理，内部 MAE=0；对应非静止 blend MAE 约 0.289。
- (6,-2) 平滑带限纹理：内部 MAE=0.00140979，blend MAE=0.0126274。门槛 MAE<0.015 且小于 blend 的 25%。
- **历史未达标压力场景**：同一 (6,-2) 改为逐像素高频噪声，旧 shader 的 MAE=0.168021，blend=0.285543，未达到相同门槛。后续自适应竞争种子搜索已修复该回归；当前结果见本文末尾 v0.3 复核，不要把这条历史数字当作当前二进制结果。
- 端点 alpha=0/1、红→蓝低匹配可信度回退、BGRA 通道顺序和 IEC sRGB 解码（误差<0.001）通过。
- 真实 Desktop Duplication → shared texture/fence → D3D12 compute：无窗口限时捕获及有窗口呈现实际执行成功。没有保存桌面像素。
- 最终金字塔版本 3840×2160 SDR 源、960×540 工作尺寸、5 秒窗口测试：captured=151、generated=150、accumulated_skips=235、history_resets=1，平均 flow 提交+等待 5.26926 ms。D3D12 调试层无 warning/error。
- 此前无金字塔版本同类测试约 11.9 ms；两次源内容并非固定基准，因此不能当严格性能提升对比。
- 输出窗口创建和 capture-exclusion API 返回值检查通过；未做自动图像闭环排除测试，也未做人工长时间游戏画质验收。
- 空格切换/Esc 实现了窗口消息处理，但未把没有执行的人工按键流程记为已测。
- 未验证 HDR、旋转、独占全屏、长时间稳定性、真实游戏增帧收益或 VAC 兼容。遇显示模式/access-lost 当前停止并要求重启，不是自动恢复。

## v0.2 原生 V2 与窗口入口复核（2026-10-01）

- 重新配置并编译根 D3D12 工程 Release；MSVC 19.51 / Windows SDK 10.0.26100.0，编译通过。
- `ffg_demo.exe --headless --warp --debug` 通过能力查询、V2 无 mask 记录、V2 单端点 mask `E_NOTIMPL` 拒绝，以及原有静态/平移/遮挡/反向、端点、camera cut 和错误契约检查。
- CTest 7/7 通过：新增 `fgds_vk_c_header` 验证 Vulkan V2 C 头文件布局；原有 WARP、color-flow、捕获帮助、调度 self-test 和宽度拒绝继续通过。
- `ffg_capture.exe --self-test` 通过；`--list-windows` 实际列出当前可见窗口客户区。对当前 L4D2 DXVK 窗口执行 `--window 2 --seconds 2 --headless --width 640`：`captured=65`、`generated=64`、`processing=640x276`，D3D11/D3D12 共享纹理、窗口裁剪和 GPU 插帧链路通过。窗口捕获仍是启动时固定矩形，未验证动态移动/resize、遮挡恢复或真实游戏增帧。
- 独立 `build-vulkan` Release 编译通过；导出 `ffgVkGetCapabilities` / `ffgVkRecordV2`。未做 Vulkan 实机提交、图像同步或质量验证。
- 根工程 `FFG_BUILD_VULKAN=ON` 需要 Vulkan-Headers 和 `glslangValidator`；当前主机的独立 `build-vulkan` 已有配置，缺少 SDK 的全新配置会按预期拒绝。

### v0.2 打包和卸载修正

- 便携包安装/卸载、已修改/额外文件保留和路径越界拒绝，通过。
- 安装器入口改为 `ffg_player.exe` 玩家控制台；捕获程序 `ffg_capture.exe` 仍作为底层诊断入口，版本信息随产品版本生成。
- 快捷方式卸载识别增加受安装记录哈希保护的目标、参数、工作目录、图标、描述、热键、窗口样式和 Shell Link flags；字节哈希不同但这些属性一致时允许清理，属性变化仍保留。
- `tools/test-shortcut-identity.ps1` 在独立测试目录验证原始入口、仅元数据变化清理资格、用户修改参数保留，通过；不改真实开始菜单。
- v0.2/v0.3 EXE 完整安装/卸载回归被本机旧开始菜单残留阻止：
  `C:\Users\Administrator\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\FreeFrameGen`
  中仅有一个指向已不存在程序的 `卸载 FreeFrameGen.lnk`，且没有安装登记。安装器没有覆盖它。
- 用户同意删除旧入口，但自动删除被执行环境策略阻止，未删除。清理该旧入口后再运行 `tools/test-installer.ps1`；不能把 v0.1 安装回归当作 v0.2 已通过。

## v0.3 多在途与捕获低延迟复核（2026-10-01）

- 根 D3D12 Release 工程重新配置、构建通过；`FreeFrameGen.dll` 导出
  `ffgCreateV3`、`ffgDestroyV3`、`ffgGetCapabilitiesV3`、`ffgRecordV3`。
- 独立 `build-vulkan` Release 构建通过，`FreeFrameGenVulkan.dll` 导出
  `ffgVkCreateV3`、`ffgVkDestroyV3`、`ffgVkGetCapabilitiesV3`、`ffgVkRecordV3`；
  Vulkan-only CTest `vulkan_v3_smoke` 通过，真实 Vulkan device 的两 slot、timeline
  retirement、slot busy/复用和 output readback 通过，仍不等于游戏内接入验证。
- 顶层 `FFG_BUILD_VULKAN=ON` 也已用显式 `FFG_VK_HEADERS`/`FFG_GLSLANG` 路径完成
  Release 构建，顶层 CTest 10/10 通过；未提供这两个依赖时，默认关闭 Vulkan 的根工程仍可正常构建。
- `ffg_demo.exe --headless --warp --debug` 通过 V3 capability、双 slot 同时记录、
  未完成 slot 返回 `DXGI_ERROR_WAS_STILL_DRAWING`、提交完成后复用，以及空 fence/已完成
  fence value 拒绝。V3 不由 DLL submit/signal/wait。
- D3D12 CTest 9/9 通过，显式 Vulkan 顶层 CTest 10/10 通过，新增 `color_flow_stress_warp` 和 `player_help`；硬件 RTX 3050 的
  `ffg_flow_test --debug` 和 `--stress --debug` 均通过。当前合成压力场景
  (6,-2) MAE=0，遮挡平移 MAE=0.00988204，简单 blend 基线=0.0194666；这些不是
  通用游戏画质证明。
- RTX 3050 实际 Desktop Duplication headless 运行：
  `ffg_capture --output 0 --seconds 2 --headless --width 640 --deadline-ms 16` 捕获 100、
  生成 98、`accumulated_skips=79`、`history_resets=2`、`deadline_skips=1`、
  `deadline_overruns=0`、处理尺寸 640x360，合并提交路径的
  `mean_flow_submit_wait_ms=4.2628`。该数字包括转色、flow、GPU 等待，不是端到端输入
  延迟；`--debug` 和不同桌面负载会改变结果。
- 捕获路径现在把转色、下采样、双向估计和 warp 放在同一 command list，并在下一次
  Desktop Duplication acquire 前等待一次 fence；这是资源所有权正确的单飞低延迟路径。
  多槽异步 capture 仍未实现，动态 resize/access-lost 自动恢复、HDR、遮挡窗口和真实
  游戏画质仍需单独验收。
- 当前中文 Beta 安装器成功构建为 `build/setup-beta-release2/FreeFrameGen-Setup-0.3.0-x64.exe`，
  嵌入 22 个文件（含 ffg_player.exe、图标和双语发布说明）；以旁边的 `.sha256` 文件核对 SHA256。
  默认安装包不含可选 Vulkan DLL。完整安装/卸载回归仍被上文记录的
  旧开始菜单残留入口阻止，未把“安装器可编译”当作“本次安装回归通过”。
- `cmake --install` 生成的独立 v0.3 package 通过 `tools/test-install.ps1`：安装、
  修改文件保护、路径遍历拒绝和额外用户文件保留均通过；测试目录位于临时目录并已清理。
