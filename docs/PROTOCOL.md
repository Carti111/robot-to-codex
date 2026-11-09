# CVI1：机器人到 Codex 的当前接口

版本：线协议 v1，桌面 0.1.0。本文为当前实现，不是标准 MCP，也不是 Codex/OpenAI API。仅描述格式和合成示例，不包含真实凭证。最终实现见 `src/VoiceInput.Robot`。

## 1. 网络角色

| 通道 | 发送方 → 接收方 | 内容 |
| --- | --- | --- |
| 临时配对发现 | 电脑临时 UDP 端口 → 广播 45833 | ASCII `CVI-PAIR/1`，无换行 |
| 临时配对回复 | 机器人 UDP 45833 → 请求的源地址/端口 | 下述公开 JSON，最多 512 字节 |
| 安全配对 | 电脑 → 机器人 HTTP 45834 | `pair-session`、`pair-config`；Security2 加密配置 |
| 日常发现 | 机器人 → 局域网电脑 UDP 45832 | ASCII `CVI-DISCOVER/1 <identity>`，无换行 |
| 日常发现回复 | 电脑 UDP 45832 → 请求的源地址/端口 | ASCII `CVI-HOST/1 <identity> 45831` |
| 日常传输 | 机器人作为客户端 → 电脑 TCP 45831 | TLS 1.2/1.3、令牌认证、CVI1 行帧 |

IPv4 局域网。`identity` 为配对生成并保存的 32 个 hex 字符，是发现关联标识而非认证凭据。收到发现回复后仍需完成 TLS 证书验证和令牌认证，不能信任 UDP 地址直接接收正文。

## 2. 配对发现与 Security2

本地物理操作开启临时窗口；参考实现 120 秒、五次失败关闭。只在窗口内回应：

```json
{"v":1,"name":"My DIY Robot","device":"001122aabbcc","port":45834,"security":2,"patch":1,"remaining":120}
```

`device` 固定 12 个 hex 字符（公开、稳定的设备 ID）；`name` 可省略，有值时 1～64 字符且无控制字符。端口/security/patch 当前严格匹配表中值，不允许降级。**配对码不出现在 UDP JSON 中。**

电脑以 `application/octet-stream`、HTTP POST 访问两个端点，不经代理、不跟随重定向：

1. `/pair-session`：官方 ESP-IDF Protocomm session protobuf 封装的 Security2 SRP6a 握手，双向证据校验。使用 3072 位组、SHA512、16 字节 salt；v1 username 固定为 `sparkbot`，password 为设备本地展示的六位十进制码。该 username 是兼容常量，与产品名称无关。
2. `/pair-config`：握手通过后，用官方 patch 1 的 AES-256-GCM / nonce 计数器规则加密 UTF-8 JSON。密钥从 SRP 派生，不自行换成明文或新的密钥交换。

加密 JSON 内容：

| 字段 | 含义 |
| --- | --- |
| `cert` | 电脑自签名 X.509 公钥证书，PEM；没有电脑私钥 |
| `token` | 安全随机生成的 64 个 hex 字符，日常连接的共享凭据 |
| `identity` | 电脑发现标识，32 个 hex 字符 |
| `host` | 电脑在机器人所在局域网的 IPv4 地址，作连接提示/回退 |

机器人应在验证所有字段、持久化成功后，返回 Security2 加密的 `{"ok":true}`；否则返回失败且保留旧绑定。设备私有存储不得对外暴露。配对成功关闭本轮窗口。HTTP 本身虽是明文传输，配置负载必须由 Security2 认证加密，不是明文发送 token。

优先复用 [ESP-IDF protocomm](https://github.com/espressif/esp-idf/tree/v5.5.6/components/protocomm) 和 [官方 Security2 客户端参考](https://github.com/espressif/esp-idf/blob/v5.5.6/tools/esp_prov/security/security2.py)。电脑对应实现为 `EspSecurity2Client.cs`，采用 Bouncy Castle/Google.Protobuf/.NET AES-GCM；以契约测试核对证据、nonce 与篡改拒绝，不手写另一套密码协议。

## 3. TLS 与日常认证

机器人验证连接对端是配对保存的电脑证书（包括正常有效性检查/预期证书验证方式），不使用“接受任意证书”。电脑生成的私钥不会下发机器人。

TLS 建立后，机器人先发 ASCII 行：

```text
@CVI-AUTH1 <pairing-token>\n
```

其中 `<pairing-token>` 是配对保存的 64 个 hex 字符，上面的 `\n` 表示真正 LF，不是两个可见字符。电脑固定时间比较凭据，成功回复真正换行结尾的 `@CVI-AUTH1 OK`。失败/超时/已有活动机器人时关闭连接，不转入输入协议。

这是证书验证加 TLS 内共享凭据认证，**不是 mTLS 客户端证书登录**。程序当前只接受一条活动机器人连接。认证最长约 8 秒。

## 4. CVI1 行帧

每帧为 UTF-8，一行：`@CVI1 ` + JSON + 真正 LF。禁止在 JSON 字符串里放未转义换行；必须能处理网络拆包/粘包和跨包中文。

电脑握手：

```json
{"v":1,"session":"0123456789abcdef0123456789abcdef","id":1,"op":"hello","turn":0}
```

机器人应答：

```json
{"v":1,"session":"0123456789abcdef0123456789abcdef","id":1,"ok":true,"board":"cvi-robot","speech":true,"mode_lock":true,"local_mode":true,"event":"accepted","phase":"chat"}
```

上面每条 JSON 实际发送时都要加 `@CVI1 ` 和 LF。新适配器 `board` 使用 `cvi-robot`，旧 `esp-sparkbot` 兼容；其他值拒绝。它是兼容配置标记，真实产品名用发现的 `name`。

| 字段 | 规则 |
| --- | --- |
| `v` | 数字 1 |
| `session` | 电脑每次连接生成 32 位小写 hex；应答/事件必须原样携带 |
| `id` | 电脑从 1 起单调递增的正整数；应答相同，异步事件用 0 |
| `op` | 仅电脑请求包含，见操作表 |
| `turn` | 最终 STT 轮次正整数；与真实识别句绑定，状态/心跳可为 0 |
| `ok` | 应答必填布尔；拒绝 `false`，电脑不得据此继续动作 |
| `speech` / `mode_lock` / `local_mode` | hello 必须全为 true，表示最终 STT、设备工具锁和原生本地模式 |

不要因为版本号一样就宣称这些能力。能力必须真实实现，否则电脑应拒绝握手。

匹配 session、board 和 id 的回复最多等约 3 秒，电脑约每秒发 ping。固件租约参考 5 秒，重复命令不续租。无效/超长报文与错误会话不能执行动作。

## 5. 最终 STT 与模式

聊天态只转发真实的开启口令；听写态转发最终 STT，并在设备侧保留原文、分类和当前 turn：

```json
{"v":1,"session":"0123456789abcdef0123456789abcdef","id":0,"board":"cvi-robot","event":"speech","turn":12,"text":"给这个函数增加日志"}
```

同一连接内 turn 严格递增，避免重复/旧语音重放。建议使用 1～2147483647 的安全整数；不要因一轮模式退出又从 1 开始。在下一句前先完成本句边界处理，避免电脑尚未授权时原文被覆盖。

口令规则：去空白、英文小写、只去首尾中英文逗号/句号/感叹号/问号/引号，然后匹配**完整句子**，不删除正文内部标点来拼命令。

- 核心开启：`开启语音输入`；发送：`发送`；退出：`退出模式`。
- 历史兼容开启包括“打开语音输入”“开启/打开语音输入功能”“开启/打开语音输入模式”“开启/打开 Codex 语音输入”“开启/打开 Codex 语音输入功能”，及部分“语言输入”识别变体。
- 历史退出包括“退出语音输入”“退出语音输入功能”“退出语音输入模式”“退出 Codex 语音输入功能”。完整别名以 `RobotTextSession.Command` 为准，两端须一致。
- `增加发送按钮`、`这个函数退出模式后保存`、`发，送` 均为正文。

不能从大模型工具参数伪造这些事件。听写及暂停期间阻止普通模型语音与设备工具执行；网络故障只暂停，不恢复聊天。失联时用户明确退出仍应在机器人本地生效。

## 6. 电脑操作

所有操作都需要正确 session 和单调 id。实际改变状态/授权/完成的操作还需当前真实 turn 与意图匹配。

| op | 机器人应检查/处理 |
| --- | --- |
| `hello`, `ping`, `status` | 握手、心跳、只读状态；返回匹配 id 的应答 |
| `start` | 对应本地真实开启 STT；确认允许进入听写，不能让任意远程命令强行开启 |
| `stop` | 对应本地真实退出 STT；本地已退出时也可确认该退出轮次 |
| `authorize_type` | 当前 turn 是 Body、处于听写态、尚未授权/完成；通过才允许电脑输入 |
| `authorize_send` | 当前 turn 是独立 Send、处于听写态、尚未授权/完成；通过才允许按 Enter |
| `typed` | 已授权 Body 本轮在电脑完成输入，排一次机器人本地提示音 |
| `sent` | 已授权 Send 本轮在电脑完成 Enter，结束本轮，不播报 |
| `skip` | 电脑没有新的听写内容可发；需已授权 Send，本轮静默结束，不按 Enter |
| `abort` | 本轮作废并暂停保留模式；不把连接失败当成用户退出 |
| `test_sound` | 仅开发：待机、非听写且播放空闲才能试音，日常流程不用 |

成功应答示例：

```json
{"v":1,"session":"0123456789abcdef0123456789abcdef","id":7,"board":"cvi-robot","ok":true,"event":"accepted"}
```

拒绝用相同 session/id、`ok:false`；不能先返回成功再异步发现授权失败。重复 id 不再执行，旧 session/旧 turn 不执行。完成确认只反馈输入事件已执行，不证明 Codex 服务器已经收到任务。

## 7. 正常消息顺序

下列省略 frame/session/id，实际请求都须带全；ping 可穿插且占用递增 id。

```text
robot speech(turn=1, text=开启语音输入)
PC    start(turn=1)               -> robot ok
robot 本地开启反馈 -> 准备新一轮
robot speech(turn=2, text=正文)
PC    authorize_type(turn=2)      -> robot ok
PC    向当前 Codex 焦点输入正文
PC    typed(turn=2)               -> robot ok
robot 响一次提示音 -> 排空播放/自声 -> 恢复录音
robot speech(turn=3, text=发送)
PC    authorize_send(turn=3)      -> robot ok
PC    Enter（仅有未发送听写内容时）
PC    sent(turn=3) 或 skip(turn=3)-> robot ok，静默恢复听写
robot speech(turn=4, text=退出模式)
PC    stop(turn=4)                -> robot ok
robot 本地退出反馈 -> 恢复正常聊天
```

提示音播放与录音恢复必须处理音频队列，不能让提示音又作为 STT 上行，也不能正常正文后再由模型问一遍“是否发送”。进入/退出短语和正文完成音由机器人实现；电脑不播放。

## 8. 异步状态与故障

可发 `ready`（本轮反馈后就绪）、`chat`（真实退出后正常聊天）、`paused`/`timeout`（保留模式并暂停）、`tool_blocked`、`heard_start/body/send/stop` 等 id=0 事件。`heard_*` 是不含正文的诊断分类，不替代 `speech`，也不授权输入。

**尤其不能在普通模型回复、一次正文结束、网络断开或随机 UI 操作后发 `chat`。** 当前电脑会据此结束听写状态。只在真实退出后报告它。

边界：电脑读取每行最多 20000 字节，STT 原文 1～4000 个 .NET UTF-16 字符单位；电脑请求保持 4096 字节内。工作队列 8、事件/传输队列有界；超量需暂停而不是无限堆积。

断线使用新 session 重新握手，清理旧轮次授权/待处理队列，不重发 `typed/sent` 或输入正文。暂停后只处理新的真实 STT，不能自动把旧缓存补到当前聊天。应用不知道你是否切换了聊天。

## 9. 代码入口

- `WifiRobotPairing.cs`：公开发现 JSON、Security2 配置上传。
- `EspSecurity2Client.cs`：官方 protobuf/SRP/AES-GCM 兼容实现。
- `WifiRobotServer.cs`：电脑 TLS 服务、令牌认证、日常发现。
- `UsbRobotFeedback.cs`：历史命名的共同 CVI1 行协议引擎；当前桌面注入 Wi-Fi wire。
- `RobotSpeechDispatcher.cs` / `RobotTextSession.cs`：有界队列、原话分类、动作前授权、故障暂停。
- `RobotCodexInput.cs`：Windows 前台 Codex 校验、实际输入与 Enter。
- `tests/RobotContract`：隔离测试与合成设备响应，不会向真实 Codex 发送。
