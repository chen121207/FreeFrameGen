# Windows 安装器与卸载器

## 用户操作

1. 双击本产品的 `FreeFrameGen-Setup-0.3.0-x64.exe`。
2. 选择新的独立目录，点击“安装”。默认目录：`%LOCALAPPDATA%\Programs\FreeFrameGen`。
3. 安装器默认创建桌面快捷方式，同时创建开始菜单入口；如不需要可取消勾选。启动后选择显示器/窗口，预览内空格比较开/关，Esc 停止。
4. 卸载任选：Windows 设置 → 应用 → 已安装的应用；开始菜单“卸载”；安装目录内双击 `Uninstall.exe`。

安装显示器捕获插帧预览、Native Demo、Runtime（FFG）、SDK 与说明。不是 L4D2 游戏安装器，不处理旧 Remix，不提供光追或生产级低延迟全屏帧生成。
Windows 10/11 x64 + .NET Framework 4.8；Windows 11 通常已内置。程序本身还需要支持 D3D12 的设备。
安装器未签名，Windows 可能显示“未知发布者”或 SmartScreen 提示；不要求关闭安全软件。

## 改动范围

- 程序：所选独立目录，含内嵌 SHA256 文件清单对应的载荷、`Uninstall.exe` 和安装记录 `install-state.xml`。
- 当前用户开始菜单、默认桌面快捷方式（可在 GUI 中取消）。快捷方式使用内嵌 FFG 图标。
- 当前用户（HKCU、64-bit view）的单一卸载注册项：`Software\Microsoft\Windows\CurrentVersion\Uninstall\8DE1241F-5D23-49D2-A5AA-D8EF2801D362`。
- 用户输出工作目录：`%LOCALAPPDATA%\GraphicsResearch\FreeFrameGen`。保留目录用于诊断/用户数据，捕获程序不保存画面；卸载不删除用户数据。
- 不提权，不安装服务/驱动，不写系统级 HKLM，不注入游戏，不设置自启动。

## 安全与卸载行为

- 拒绝已有目标目录、已有本产品安装记录；**不做覆盖升级或自动修复**。升级先卸载，再安装。
- 拒绝 Steam 游戏目录、包含 left4dead2.exe 的父目录、UNC 路径和重解析点。
- 在私有临时子目录解压，按内嵌清单验证每个文件后才转移为安装目录。普通错误会尽力回退本次创建且未修改的文件。
- 断电/强杀后的完整事务恢复尚未实现；如有残留先备份并检查日志，不应手工删除未知文件。
- 卸载预检注册项、安装记录哈希、卸载器身份、所有载荷文件哈希、只读/占用状态。
- 发现修改后的程序文件或安装记录时停止，不删除程序文件。先备份修改，再恢复原文件，使用该安装目录的卸载器重试。
- 已经缺失的载荷允许继续卸载；额外用户文件和修改过的快捷方式保留；只清理空目录，绝不递归删除整个安装树。
- v0.2 修正快捷方式只比较字节哈希导致的残留：目标、参数、工作目录、图标等受记录保护的属性未变时，允许清理仅 Shell 元数据变化的入口。用途/属性改动仍保留。
- 如旧版本已卸载但开始菜单入口仍残留，安装器会停止而非覆盖。确认是失效旧入口后手动移走/删除，再安装。
- 文件删除/权限在预检后仍可能变化；中途错误可能留下部分安装，记录保留以便重试。不是防恶意管理员篡改的安全边界。

Windows 会锁定正在运行的 EXE。安装目录的卸载器会先把同一程序复制到系统 TEMP，退出启动器后再执行卸载，因此可移除安装目录的 Uninstall.exe。
TEMP 中的小型卸载工作副本由系统临时文件清理或用户之后清理；不为了自删除启动 cmd/batch 或修改重启删除注册项。

## 静默模式

```powershell
Start-Process -Wait .\FreeFrameGen-Setup-0.3.0-x64.exe -ArgumentList '--silent --install --dir "D:\Apps\FreeFrameGen" --log "D:\Apps\setup.log"'
# Silent deployments do not touch the desktop unless explicitly requested.
Start-Process -Wait .\FreeFrameGen-Setup-0.3.0-x64.exe -ArgumentList '--silent --install --desktop --dir "D:\Apps\FreeFrameGen" --log "D:\Apps\setup.log"'
Start-Process -Wait "D:\Apps\FreeFrameGen\Uninstall.exe" -ArgumentList '--silent --log "D:\Apps\uninstall.log"'
```

父目录需可写，日志路径父目录必须已存在。外部 Setup.exe 用 `--uninstall --dir ...` 也能卸载，但必须与当初安装的构建完全相同。
安装/外部卸载成功退出 0、失败退出 1。**安装目录的卸载 EXE 是启动器**，其退出 0 只代表工作副本已启动；自动化应等待日志 `RESULT=SUCCESS` 或 `RESULT=FAILED`，不能只看启动器退出码。
`--preview image.png` 生成真实 WinForms 控件的离屏预览，不安装任何东西。

## 从源码构建与验证

```powershell
.\tools\build-installer.ps1
.\tools\test-shortcut-identity.ps1
.\tools\test-installer.ps1 -Installer .\build\setup\FreeFrameGen-Setup-0.3.0-x64.exe
```

构建脚本查找现有 CMake/Visual Studio，并使用系统 .NET Framework C# 编译器；不联网获取工具。
使用 `-CMake <cmake.exe>` 可显式指定。已有输出不覆写，可用 `-OutputDirectory <新目录>`。
CMake 安装阶段生成的 package 中 GUI 载荷仅选择 bin/include/lib/docs、README.md、product.txt，不混用便携版卸载脚本。

两个仓库的 `installer/Setup.cs` 是同一份实现；`installer/product.json` 决定名称、独立产品 ID、程序入口与说明。修改共享实现应同步两个仓库。
编译时生成 Product.g.cs，将所有载荷 SHA256 内嵌进 EXE；不信任外部可编辑的卸载文件列表。
