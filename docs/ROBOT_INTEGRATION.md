# 接入自己的机器人

这份指南面向固件/机器人服务开发者。目标是实现电脑软件已经使用的交互：正常聊天 → 开启听写 → 正文进入 Codex 草稿 → 明确发送 → 明确退出。电脑端不规定主控、外形、传感器或 ASR 厂商。

## 先判断改造位置

| 你的平台 | 推荐实现位置 | 可以复用什么 |
| --- | --- | --- |
| ESP32 / ESP-IDF | 固件 Application、最终 STT 回调、音频和工具执行入口 | `protocomm` Security2、`esp-tls`、自己的显示/音频/BSP |
| Linux / ROS / 树莓派 | 现有机器人进程或受控本地适配服务 | 系统 TLS、现有 ASR、官方 Security2 协议参考实现 |
| 其他 MCU | 固件内或能代表机器人的受控网关 | 已有可信配对/TLS 库；需遵循本仓库当前的 Security2 接口 |
| 完全封闭的商品机器人 | 先向厂商获取接口/可修改固件 | 仅有聊天 API 或 MCP 工具通常不足以实现原生模式锁 |

PC app 当前只实现 Security2 的这一套配对方式。使用别的认证方案，需要同时修改电脑端与机器人端，不能声称“兼容”后退回明文令牌、关闭 TLS 验证或给配对码另做一个无认证 POST 接口。

## 最小能力清单

- 接收**最终 ASR/STT**，能区分用户输入与机器人播放的自声，不把大模型回答当原话。
- 本地维护 `Chat / Entering / Dictating / Exiting / Paused` 状态，以及本轮原文、分类、轮次与授权标记。
- 听写/暂停期间，在实际执行处阻止普通对话的设备工具与模型播报。光改系统提示词不够。
- 每个新识别句递增 turn；发送和输入前核对当前真实轮次，拒绝旧轮次、重复授权与错误分类。
- 按 [PROTOCOL.md](PROTOCOL.md) 实现发现、配对、TLS 和请求/事件。
- 设备上以本地物理操作打开短时配对窗口，提供六位码的可读方式；不要求顶部触摸，也可以用自己的按钮/触摸菜单。无屏设备需自行设计本地码展示方式，当前电脑 UI 不提供无屏配对的另一种协议。
- 开启/退出用本地短反馈；正文只在 `typed` 确认后响一次，`sent` 静默。播放时防止麦克风再次识别提示音。
- 心跳丢失或动作不确定时暂停、清掉本轮和旧队列；只有明确退出口令才恢复正常聊天。

## 先接文本，不改电脑上的模型

机器人只需要交付最终识别文字。识别可以用自己的云服务、局域网模型或离线 ASR；只要事件语义一致，电脑端不用随 ASR 厂商变化。

本仓库不提供音频流上传、录音、STT 模型安装、机器人 TTS 或聊天模型服务。参考小智设备的原有在线识别依赖也不会因为公开本项目而消失。

## 接入步骤

1. **看最小接口**：读 `IRobotFeedback.cs`、`RobotTextSession.cs` 和 `RobotSpeechDispatcher.cs`。电脑侧的文字输入抽象为 `IRobotInput`，设备授权抽象为 `IRobotSpeechFeedback`。
2. **跑不打字的示例**：按 `examples/README.md` 运行内存示例，观察 start/type/send/stop 授权顺序。它不会连机器人、打开端口或向 Codex 输入。
3. **实现发现/安全配对**：复用官方库；按发现 JSON 输出自己的 `name`，`device` 为稳定公开 12 位 hex 标识。Security2 用户名 `sparkbot` 是 v1 的历史协议常量，不是要求你的机器人品牌叫 SparkBot。
4. **实现日常 TLS**：机器人作为客户端连电脑，严格验证配对得到的电脑证书，完成令牌认证，再执行 CVI1 `hello`。
5. **实现真实最终 STT 与轮次**：从识别完成处发 `speech`，不要从模型工具调用中发。新机器人使用 `board: "cvi-robot"`；旧 `esp-sparkbot` 继续兼容。
6. **接模式和音频钩子**：本地先分类独立口令，在聊天态只转发开启口令；听写态非口令均为正文。完成音效与恢复录音分开处理，防止重复提示音。
7. **接授权检查**：电脑发来 `authorize_type/send` 后，机器人核对自己的本轮分类；不匹配就 `ok:false`。不接受“电脑说它是发送所以它就是发送”。
8. **验收以后再连真实输入**：先运行隔离测试和你自己的假输入接收器，再在 Codex 草稿中测试，不要把自动向真实聊天发消息当单元测试。

## 平台无关伪代码

下面是接入位置说明，**不是可以直接烧录或直接部署的完整实现**。TLS、Security2、超时、音频隔离和持久化必须另接已有实现。

```text
on_final_stt(text):
    intent = exact_command_or_body(text)
    if intent == STOP and mode_engaged:
        locally_finish_mode_even_if_pc_disconnected()
        if link_live: publish_speech(new_turn, text)
        return
    if not link_live: keep_existing_chat_or_pause_current_mode(); return
    if mode == CHAT and intent != START: pass_to_normal_chat(); return
    freeze_current_turn(text, intent)
    suppress_normal_model_speech_and_device_tools()
    publish_speech(new_turn, text)

on_pc_request(op, session, id, turn):
    require_authenticated_tls_and_current_session()
    reject_stale_or_repeated_action()
    if op == authorize_type: require_current_turn(BODY, not_used)
    if op == authorize_send: require_current_turn(SEND, not_used)
    if op == typed: require_authorized(BODY); queue_one_local_cue()
    if op == sent or op == skip: require_authorized(SEND); finish_silently()
    if op == abort: invalidate_turn_and_pause_without_restoring_chat()
    reply_with_same_session_and_id()
```

还需实现 start/stop、heartbeat/status、播放完成和断线状态；完整语义见协议文档。单独 `发送` 是口令，`帮我增加发送按钮` 是正文。`这个函数退出模式后保存` 也必须保留为正文。

## 接口与板型的关系

- `board` 当前是受支持的**线协议配置标记**：新适配器发 `cvi-robot`，原设备发 `esp-sparkbot`。不要填任意型号字符串，否则电脑会拒绝帧。
- 真实产品名字放发现 JSON 的 `name`，1～64 个字符，不含控制字符。电脑显示它；字段省略时显示“兼容机器人”。
- `device` 不是密钥，可以使用自己的公开唯一标识，不需要依赖 Espressif MAC。
- 端口、帧前缀、Security2 username、固定中文口令和能力标志都是当前兼容约定。改这些时应同时改两端并补测试。
- 单个电脑身份当前最多一条活动连接；机器人成功换绑到新电脑后应停止使用旧绑定。

## 实机验收清单

- [ ] 配对只由本地物理操作开启，120 秒过期；错误码/伪造证明/篡改密文被拒，五次失败关闭窗口。
- [ ] 认证成功且新绑定持久化成功后才替换旧绑定；码、令牌、私钥不进日志。
- [ ] 机器人与电脑都重启，仍能无线自动连接；拔掉 USB 后流程不受影响。
- [ ] 开启 → 两句正文 → 退出，连做两轮，没有自动发送/自动退出。
- [ ] “查看电量”“这个函数退出模式后保存”都进入正文，不调用工具。
- [ ] 正文进入草稿后只响一次；“发送”只提交一次且不播报；空发送不按 Enter。
- [ ] 输入焦点错误、Wi-Fi 中断、PC 关闭、队列溢出，只暂停，无旧文字/旧发送重放。
- [ ] 用户在失联时仍可用明确退出口令恢复机器人聊天。
- [ ] 新电脑无线配对成功，旧电脑不能抢占；电脑证书不匹配时拒绝连接。

请把你的板型、固件版本、ASR、配对入口和完成的条目写进适配 PR。隔离测试通过不等于实机通过；本仓库目前仅有开发设备 ESP-SparkBot 的实机使用反馈，其他平台仍待贡献。
