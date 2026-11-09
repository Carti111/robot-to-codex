# 构建与验证

## 环境

Windows 10 2004（19041）或更新 / Windows 11、x64、.NET 8 SDK。使用 WinUI 3，NuGet 会恢复 Windows App SDK、Windows SDK 构建工具和依赖；缺少 Windows 构建组件时按构建错误安装对应 Windows SDK / Visual Studio Windows 开发组件。普通使用者运行完整的 self-contained 便携目录，不需安装 Python 或 .NET SDK。

本地开发基线：.NET SDK 8.0.427。源码脚本从 PATH 查找 `dotnet`，不依赖作者电脑的 C:/D: 路径。

```powershell
git clone https://github.com/Carti111/robot-to-codex.git
cd robot-to-codex
.\scripts\test.ps1
.\build.ps1 -Configuration Release
.\package.ps1
.\Start.cmd
```

`package.ps1` 生成 `dist/CodexVoiceInputRobot`，包含所有依赖。不要只复制 EXE。构建/打包不修改防火墙，不启动程序，不安装开机自启，不操作机器人。

## 自动测试范围

`./scripts/test.ps1` 构建并运行 RobotContract 与 TextPostProcessorContract。机器人测试使用合成文字、内存 wire、回环 TLS/HTTP/WebSocket 与临时 DPAPI 文件，不需要机器人，不向 Codex 注入，不访问真实小智账户或 ASR。

覆盖固定口令、完整句匹配、逐轮授权、发送序列、队列溢出/暂停、重复事件/旧会话、模型伪造动作拒绝、两轮模式切换、TLS 凭据/证书拒绝、Security2 错码/篡改/降级，以及通用 `cvi-robot` 标记与发现名称。库内 MCP 兼容测试不会在桌面应用中启用 MCP。

剪贴板测试会改写当前剪贴板，默认不运行；仅在允许覆盖剪贴板的隔离环境显式运行：

```powershell
.\scripts\test.ps1 -IncludeClipboard
```

适配示例不打开任何网络或键盘接口：

```powershell
.\scripts\run-example.ps1
```

实机验收单独看 [ROBOT_INTEGRATION.md](ROBOT_INTEGRATION.md)。代码编译、假设备测试和本机 TLS 测试都不等于新板型已经可用。

## 安装包（按需自行构建）

需要 [Inno Setup 6](https://jrsoftware.org/isinfo.php)。没有它也能构建便携程序。

```powershell
.\build-installer.ps1
# 或指定 ISCC.exe：
.\build-installer.ps1 -CompilerPath 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
```

输出在 `dist/releases`，包含 RobotToCodex-Setup-0.1.0.exe、RobotToCodex-win-x64-0.1.0.zip、SHA256SUMS.txt 和使用说明。安装器可创建桌面/开始菜单快捷方式；本仓库的 GitHub Actions 只构建和测试，不自动发布 Release。不要把构建安装包理解为已经获得新板型硬件验证。

当前应用版本为 0.1.0，程序集/程序名沿用 CodexVoiceInput，仓库及发行版名称为 Robot to Codex。

## 目录与安全

源码提交不包含 `dist/bin/obj/logs`、SDK、NuGet 缓存、实际配置、配对码、证书私钥、历史或固件备份。图标 ICO/PNG 和 Inno 语言文件属于应用构建资源。

公开前运行 `scripts/check-public-source.ps1`；它扫描 Git 暂存/已跟踪内容的敏感文件名和常见凭据模式，只显示路径/规则，不打印命中的值。检查不能替代人工 review。
