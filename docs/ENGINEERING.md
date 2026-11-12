# 三个开发案例

这些案例根据已有开发记录与当前源码整理。本次上传没有重新执行实机验收，历史记录和本次检查见 [验证说明](SHOWCASE_VALIDATION.md)。

## 1. 从 USB 连接改成日常无线听写

**问题：** 桌面机器人如果一直连 USB 才能输入，摆放和移动受到限制；换电脑时也需要重新建立可信连接。

**实现：** 将文字帧传输迁移到局域网 TLS，保留 USB 作为烧录和诊断入口。首次绑定由机器人本地触摸开启临时窗口，复用官方 Security2 配对，再由设备连接电脑的 TLS 服务。电脑保存当前 Windows 用户加密的配对身份。

**一次具体排错：** 配对发现阶段能收到 UDP 包，但电脑仍搜不到设备。固件启用了 `CONFIG_LIBC_NEWLIB_NANO_FORMAT`，64 位格式串 `%llu` 未按预期生成数字，导致 wire JSON 无效。改成有界 `unsigned` 与 `%u`，并检查超时减法边界。这个问题需要检查真实报文，不能仅凭“网络收到了包”认定发现成功。

**已有验证：** 开发记录包含修复后的真实配对与拔线无线输入确认；本次未重新抓包或执行配对。

源码：[wifi_voice_link.cc](../firmware/source/main/wifi_voice_link.cc)、[wifi_voice_pairing.cc](../firmware/source/main/wifi_voice_pairing.cc)。

## 2. 让听写具有明确的发送、退出与故障行为

**问题：** 同一台机器人既聊天又听写，正文可能包含设备功能名或“退出模式”等词；如果动作由模型自由判断，容易偏离用户正在输入的意图。断线后自动补发旧队列也可能造成重复输入。

**实现：** 将听写做成固件内的固定状态。进入后，非独立口令均按正文处理，阻断设备工具回调。电脑动作前进行逐轮授权，完成后回传确认。输入结果与提示音关联，故障时暂停并丢弃旧队列。侧触切页采用预选，再由顶部触摸确认退出，避免误触。

**取舍：** 普通句子需要准确保留，故障时优先让用户重新确认当前状态；不自动猜测遗漏正文或重试提交。

**验证入口：** 电脑端已有契约测试；固件纯状态测试覆盖模式、反馈、暂停及菜单确认。原生输入成功只表示系统输入事件已送出。

源码：[voice_feedback_state.h](../firmware/source/main/voice_feedback_state.h)、[RobotTextSession.cs](../src/VoiceInput.Robot/RobotTextSession.cs)、[反馈状态测试](../firmware/source/tests/feedback_state_test.cpp)。

## 3. 把传感器数据调成可用的手势交互

**问题：** 2048 初版控制需要回正、停稳后才能继续；放宽触发条件后，又出现一次动作连跳多步。使用者按“拿手机”的姿势操作，绝对倾角和固定回正条件不符合预期。

**实现：** 参考原厂轴向定义，用短时角速度积分判断动作；一个方向累计达到约 20° 后触发一步。低于 20 dps 的轴向速率不累计，持续旋转保持单步锁定，通过速度谷值或明确方向变化开启下一笔动作，并保留 240 ms 最短步进间隔。

**取舍：** 参数反映这台设备的实际握持与操作反馈，不能直接推定对所有硬件和用户都适用。

**已有验证：** 开发过程包含“难触发→太灵敏→增加门槛”的多轮实机反馈；纯逻辑测试检查持续旋转、方向变化与微小动作。实机手感仍应在新设备重新验收。

源码：[spark_tilt_input.h](../firmware/source/main/boards/esp-sparkbot/spark_tilt_input.h)、[手势测试](../firmware/source/tests/tilt_input_test.cpp)、[原厂适配说明](../firmware/source/main/boards/esp-sparkbot/MULTIFUNCTION_UPSTREAM.md)。
