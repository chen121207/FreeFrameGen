# 卸载与 VAC 边界

## 当前版本

当前为独立显示器捕获插帧预览、D3D12 Native Demo / SDK，**没有**游戏安装器、DLL 注入器、D3D9 代理、游戏启动器、服务、驱动或全局 hook。运行不修改 L4D2。
已提供中文 EXE 安装器及卸载器：只登记当前用户的卸载注册项和快捷方式。详细范围、保护措施及用法见 [INSTALLER.md](INSTALLER.md)。
FFG v0.3 通过 Windows Desktop Duplication 捕获用户选择的显示器，不读取游戏内存。没有 VAC 或其他反作弊厂商认证；公开捕获 API 不构成免封保证。

## 便携安装/卸载（已提供脚本）

从 CMake install 输出的 package 目录执行：
```powershell
.\tools\manage-portable.ps1 -Action Install -Destination "$env:LOCALAPPDATA\GraphicsResearch\FreeFrameGen" -Package .
.\tools\manage-portable.ps1 -Action Uninstall -Destination "$env:LOCALAPPDATA\GraphicsResearch\FreeFrameGen"
```

安装拒绝已有目标目录，不覆盖用户文件；只安装包的 bin/include/lib/docs/tools 和标记/说明，并记录 SHA256 清单。
卸载预检所有清单路径与哈希，只删除未修改的清单文件；修改过的文件阻止卸载，额外文件保留；不递归删除目标目录。
不允许游戏树、路径越界、重解析点。清单是本机可信安装记录，不是对管理员恶意篡改的安全边界。
构建/源码目录本身不是“安装目录”，管理脚本不能拿它当卸载目标。

## 后续游戏适配器上线前的硬性门槛

1. 独立 install manifest：游戏版本、原文件/新增文件、原始与安装后哈希。
2. 原文件备份到独立私有目录，事务式安装；中断恢复、重复卸载、更新回退均须测试。
3. 不覆写不认识的其他 Mod；卸载后按原哈希复核。Steam 校验不会保证移除所有新增 Mod 文件，所以清单清理不可省。
4. 开发构建仅离线/-insecure 测试；未知游戏版本或安全模式拒绝加载（具体接入机制尚未实现）。
5. 不做 VAC 绕过、隐藏模块、关闭反作弊、签名伪装或“零封禁保证”。

## 不能承诺的事情

“正常加载并能进入服务器”不等于得到 Valve 认可，也不证明不会延迟处罚。签名软件同样不能自动获得 VAC 白名单。
最终允许“带渲染器替换进入 VAC 服务器”需要官方允许的接入方式及明确兼容性依据；当前无此依据。
可交付的安全恢复方向是：退出游戏 -> 卸载全部修改 -> 检查旧 Mod/代理残留 -> Steam 校验 -> 原版从 Steam 启动。
不能将该流程宣传为账号封禁风险保证。

参考（Valve 官方）：
- https://help.steampowered.com/en/faqs/view/571A-97DA-70E9-FF74
- https://help.steampowered.com/en/faqs/view/0C48-FCBD-DA71-93EB
