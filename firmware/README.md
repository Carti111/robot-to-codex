# ESP-SparkBot 参考固件

本目录提供本机桌面机器人使用的小智 v1.9.4 改造工程源码。它包含无线听写、Security2 配对、TLS 连接、七个功能页、触摸/手势适配与 C2 底盘工具。

## 硬件与版本

| 项目 | 参考设备 |
| --- | --- |
| 主控 | ESP32-S3，16 MB Flash / 8 MB Octal PSRAM |
| 显示与音频 | 240×240 ST7789，ES8311 |
| 输入与传感器 | 顶部/两侧电容触摸，BMI260 |
| 摄像头 | GC2145 |
| 底盘 | C2，保留本机原厂固件，主控通过 UART 控制 |
| 小智基线 | v1.9.4，`3ced7709c65a39494f5684e99111854a5bcbd8c7` |
| 构建环境 | ESP-IDF 5.5.6，目标 esp32s3，并行度 2 |

不同屏幕、传感器或底盘批次需要重新适配。此工程的修改和验收面向参考 SparkBot，源码里保留的其他上游板型不代表已经兼容。

## 包含哪些内容

`source/` 包含应用、板级实现、本地组件、素材、分区表、测试、`sdkconfig` 和依赖锁。下载的 `managed_components/`、构建产物、运行日志、私人 Flash 备份和配对凭据均未分发。

原厂图片、字体、音效和 BMI260 配置数据是构建资源，按随附许可证保留。见 [原厂功能来源](source/main/boards/esp-sparkbot/MULTIFUNCTION_UPSTREAM.md)、[BMI260 来源](source/components/bmi270_sensorapi/UPSTREAM.md)、[MIT 许可证](source/LICENSE)。

## 从源码构建

在安装并加载 **ESP-IDF 5.5.6** 的 PowerShell 中，从纯英文、无空格路径执行：

```powershell
Set-Location .irmware\source
.uild-from-source.ps1
```

首次构建需要联网，由 IDF Component Manager 按清单及锁文件还原组件。本次公开整理未从空缓存重新构建固件；此前整机镜像和实机反馈不能替代一次新的干净构建验收。

**烧录前先读：** 分区表、硬件和原机配置需分别核对并完整备份。本次不发布烧录二进制，也不提供自动烧录命令；不要把其他板型、整片备份或普通上游镜像当作这份参考工程。C2 底盘保持原厂固件。

## 运行无硬件状态测试

Windows 上安装 Zig **0.14.1**，在仓库根目录运行：

```powershell
.irmware	est-states.ps1 -ZigPath 'C:	ools\zig\zig.exe'
```

测试编译实际生产头文件，覆盖听写反馈、菜单、手势、电量缓存和摄像头预览变换。它们不访问网络、键盘或机器人，也不能替代无线配对、声音、触摸与实机验收。

`source/tests/ui_snapshot/` 和 `source/tests/battery_estimator/` 还保留原生渲染/组件测试参考。它们需要还原对应组件及额外构建工具，本次未重跑；这里的便携入口仅运行上述五组状态测试。

## 与电脑端的关系

仓库根目录是公开 **0.1.0** 电脑程序。历史整机验证使用本机 **0.2.1** 电脑程序；两端沿用 CVI1 帧协议，但当前源码组合仍需进行完整无线回归，不把帧兼容等同于全部实机交互验收。见 [验证说明](../docs/SHOWCASE_VALIDATION.md)。
