# 无键盘适配示例

```powershell
.\scripts\run-example.ps1
```

示例直接复用 `RobotTextSession` 和 `IRobotSpeechFeedback`，用合成 STT 和 console sink 演示真实轮次 → 授权 → 输入 → 完成的顺序。

不会打开网络、不读私钥、不连接机器人、不调用 ASR，也不会向 Codex 或其他窗口输入。它**不是完整机器人服务或可烧录固件**，不能用它跳过 TLS、Security2、本地模式锁、音频隔离与失联处理。实际接入见 ../docs/ROBOT_INTEGRATION.md。
