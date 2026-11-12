#include "usb_voice_feedback.h"
#include "application.h"
#include "board.h"
#include "display.h"
#include "assets/lang_config.h"
#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_timer.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <unistd.h>
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

static unsigned long long NowMs() { return esp_timer_get_time() / 1000; }

void UsbVoiceFeedback::Start() {
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = { .tx_buffer_size = 4096, .rx_buffer_size = 4096 };
        ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    }
    usb_serial_jtag_vfs_use_driver();
    xTaskCreate([](void* self) { static_cast<UsbVoiceFeedback*>(self)->ReadLoop(); },
                "voice_usb", 4096, this, 2, nullptr);
    wifi_.Start([this](const std::string& message) {
        if (queued_frames_.fetch_add(1) < 16) app_.Schedule([this, message]() {
            Process(message, true); --queued_frames_;
        }); else --queued_frames_;
    }, [this]() { app_.Schedule([this]() { if (wireless_session_) Reset(true); }); },
    [this](const char* event,const std::string& code) {
        app_.Schedule([this,event=std::string(event),code]() {
            if (Board::GetInstance().ShowVoicePairing(event, code)) return;
            auto display=Board::GetInstance().GetDisplay();
            if(event=="pairing") {
                display->SetChatMessage("system",("电脑配对码\n"+code+"\n两分钟内有效").c_str());
            } else {
                display->SetChatMessage("system",event=="paired"?"电脑配对成功":event=="pair_expired"?"配对已超时":event=="pair_locked"?"尝试次数过多，请重新长按配对":"配对未开启，请稍后重试");
            }
        });
    });
    esp_timer_handle_t timer;
    esp_timer_create_args_t args = {};
    args.callback = [](void* context) { static_cast<Application*>(context)->NotifyVoiceFeedbackTick(); };
    args.arg = &app_;
    args.name = "voice_feedback";
    args.skip_unhandled_events = true;
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 20000));
}

bool UsbVoiceFeedback::Connected() const {
    return !session_.empty() && NowMs() - state_.heartbeat <= 5000;
}

bool UsbVoiceFeedback::PhysicalMode(bool active) {
    auto now = NowMs();
    if (active && (!Connected() || state_.Engaged())) return false;
    if (!active && !state_.Engaged()) return true;
    ++current_turn_;
    intent_ = active ? Intent::Start : Intent::Stop;
    authorized_ = Intent::Start;
    if (active) state_.Start(now);
    else state_.Stop(now);
    state_.SealLocalTurn(now);
    if (!active) state_.GrantMode(false);
    mode_locked_ = true;
    allow_audio_ = false;
    app_.GetAudioService().EnableVoiceProcessing(false);
    app_.GetAudioService().DiscardSendQueue();
    if (app_.VoiceInputChannelOpen()) app_.AbortSpeaking(kAbortReasonNone);
    if (Connected()) {
        auto json = cJSON_CreateObject();
        cJSON_AddNumberToObject(json, "v", 1);
        cJSON_AddStringToObject(json, "session", session_.c_str());
        cJSON_AddNumberToObject(json, "id", 0);
        cJSON_AddStringToObject(json, "board", "esp-sparkbot");
        cJSON_AddStringToObject(json, "event", "control");
        cJSON_AddStringToObject(json, "source", "touch");
        cJSON_AddStringToObject(json, "action", active ? "start" : "stop");
        cJSON_AddNumberToObject(json, "turn", current_turn_);
        char* encoded = cJSON_PrintUnformatted(json);
        if (encoded) { WriteFrame(std::string("@CVI1 ") + encoded); cJSON_free(encoded); }
        cJSON_Delete(json);
    }
    return true;
}

void UsbVoiceFeedback::BeginPairing() {
    if (state_.Engaged() || app_.GetDeviceState()!=kDeviceStateIdle) {
        Board::GetInstance().GetDisplay()->ShowNotification("请先退出语音输入并结束聊天"); return;
    }
    wifi_.BeginPairing();
}

void UsbVoiceFeedback::ReadLoop() {
    std::string line;
    bool overflow = false;
    while (true) {
        char bytes[64];
        auto count = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), pdMS_TO_TICKS(50));
        for (int i = 0; i < count; ++i) {
            char c = bytes[i];
            if (c == '\n') {
                if (!overflow && line.rfind("@CVI1 ", 0) == 0 && queued_frames_.fetch_add(1) < 16) {
                    app_.Schedule([this, message = line]() { Process(message); --queued_frames_; });
                } else if (!overflow && line.rfind("@CVI1 ", 0) == 0) { --queued_frames_; }
                line.clear(); overflow = false;
            } else if (c != '\r') {
                if (line.size() < 4096) line.push_back(c); else overflow = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void UsbVoiceFeedback::Reply(unsigned id, bool ok, const char* event) {
    if (session_.empty()) return;
    const char* phase = state_.phase == VoiceFeedbackState::Phase::Chat ? "chat" :
        state_.phase == VoiceFeedbackState::Phase::Entering ? "entering" :
        state_.phase == VoiceFeedbackState::Phase::Exiting ? "exiting" :
        state_.phase == VoiceFeedbackState::Phase::Paused ? "paused" : "dictating";
    char frame[768];
    snprintf(frame, sizeof(frame), "@CVI1 {\"v\":1,\"session\":\"%s\",\"id\":%u,\"ok\":%s,\"board\":\"esp-sparkbot\",\"fw\":\"%s\",\"speech\":true,\"mode_lock\":true,\"wifi\":true,\"local_mode\":true,\"event\":\"%s\",\"phase\":\"%s\",\"cues\":%u,\"ready\":%u,\"dropped\":%u,\"blocked\":%u}",
           session_.c_str(), id, ok ? "true" : "false", esp_app_get_description()->version,
           event, phase, cues_, ready_, dropped_audio_.load(), blocked_tools_);
    WriteFrame(frame);
}

void UsbVoiceFeedback::WriteFrame(const std::string& frame) {
    if (wireless_session_) { wifi_.Send(frame); return; }
    flockfile(stdout); printf("\n%s\n", frame.c_str()); fflush(stdout); funlockfile(stdout);
}

void UsbVoiceFeedback::Process(const std::string& line, bool wireless) {
    cJSON* root = cJSON_Parse(line.c_str() + 6);
    if (!root) return;
    auto v = cJSON_GetObjectItemCaseSensitive(root, "v");
    auto session = cJSON_GetObjectItemCaseSensitive(root, "session");
    auto id = cJSON_GetObjectItemCaseSensitive(root, "id");
    auto operation = cJSON_GetObjectItemCaseSensitive(root, "op");
    auto turn = cJSON_GetObjectItemCaseSensitive(root, "turn");
    bool valid = cJSON_IsNumber(v) && v->valuedouble == 1 && cJSON_IsString(session) &&
        strlen(session->valuestring) == 32 && strspn(session->valuestring, "0123456789abcdef") == 32 &&
        cJSON_IsNumber(id) && id->valuedouble >= 1 && id->valuedouble <= 2147483647 &&
        id->valuedouble == id->valueint && cJSON_IsString(operation);
    if (!valid) { cJSON_Delete(root); return; }
    std::string requested_session = session->valuestring;
    std::string op = operation->valuestring;
    unsigned request_id = id->valueint;
    unsigned requested_turn = cJSON_IsNumber(turn) && turn->valuedouble > 0 && turn->valuedouble <= 2147483647 && turn->valuedouble == turn->valueint ? turn->valueint : 0;
    if (op == "pair_wifi") {
        auto field = [root](const char* name) -> std::string {
            auto value=cJSON_GetObjectItemCaseSensitive(root,name);
            return cJSON_IsString(value) ? value->valuestring : "";
        };
        bool ok=!wireless && !state_.Engaged() && !wifi_.Connected() &&
            wifi_.Configure(field("cert"),field("token"),field("identity"),field("host"));
        cJSON_Delete(root);
        // Pairing is local USB only; never echo credentials or forward them to Wi-Fi.
        if (!wireless) {
            flockfile(stdout);
            printf("\n@CVI1 {\"v\":1,\"session\":\"%s\",\"id\":%u,\"ok\":%s,\"board\":\"esp-sparkbot\",\"event\":\"paired\"}\n", requested_session.c_str(),request_id,ok?"true":"false");
            fflush(stdout); funlockfile(stdout);
        }
        return;
    }
    cJSON_Delete(root);
    if (!wireless && wifi_.Connected()) return;
    if (op != "hello" && wireless != wireless_session_) return;
    if (op == "hello" && requested_session != session_) {
        Reset(true);
        wireless_session_ = wireless;
        session_ = requested_session;
        last_id_ = 0;
    }
    if (requested_session != session_) return;
    if (request_id <= last_id_) {
        Reply(request_id, request_id == last_id_ && last_ok_, "duplicate");
        return; // Do not execute or renew the lease for duplicate/stale requests.
    }
    auto now = NowMs();
    state_.heartbeat = now;
    bool ok = false;
    if (op == "hello" || op == "ping" || op == "status") ok = true;
    else if (op == "start") {
        ok = requested_turn == current_turn_ && intent_ == Intent::Start && state_.GrantMode(true);
    } else if (op == "stop") {
        ok = requested_turn && requested_turn == current_turn_ && intent_ == Intent::Stop &&
             (state_.phase == VoiceFeedbackState::Phase::Chat || state_.GrantMode(false));
    } else if (op == "authorize_type" || op == "authorize_send") {
        auto expected = op == "authorize_send" ? Intent::Send : Intent::Body;
        ok = requested_turn && requested_turn == current_turn_ && state_.phase == VoiceFeedbackState::Phase::Dictating &&
             intent_ == expected && authorized_ != expected && !state_.completion_received;
        if (ok) authorized_ = expected;
    } else if (op == "typed" || op == "sent" || op == "skip") {
        auto expected = op == "typed" ? Intent::Body : Intent::Send;
        if (requested_turn && requested_turn == current_turn_ && intent_ == expected && authorized_ == expected) {
            bool cloud_ended = state_.boundary;
            ok = op == "typed" ? state_.Typed(now) : state_.Sent(now);
            if (ok && !cloud_ended) app_.AbortSpeaking(kAbortReasonNone);
        }
    }
    else if (op == "abort") { Reset(true); ok = true; }
    else if (op == "test_sound") {
        if (!state_.Engaged() && app_.GetDeviceState() == kDeviceStateIdle && app_.GetAudioService().IsPlaybackIdle()) {
            app_.GetAudioService().PlaySound(Lang::Sounds::OGG_SUCCESS); ++cues_; ok = true;
        }
    }
    last_id_ = request_id; last_ok_ = ok;
    Reply(request_id, ok, "accepted");
}

void UsbVoiceFeedback::ForwardSpeech(const std::string& text) {
    auto frame = cJSON_CreateObject();
    cJSON_AddNumberToObject(frame, "v", 1);
    cJSON_AddStringToObject(frame, "session", session_.c_str());
    cJSON_AddNumberToObject(frame, "id", 0);
    cJSON_AddStringToObject(frame, "board", "esp-sparkbot");
    cJSON_AddStringToObject(frame, "event", "speech");
    cJSON_AddNumberToObject(frame, "turn", current_turn_);
    cJSON_AddStringToObject(frame, "text", text.c_str());
    char* encoded = cJSON_PrintUnformatted(frame);
    if (encoded) {
        WriteFrame(std::string("@CVI1 ")+encoded);
        cJSON_free(encoded);
    }
    cJSON_Delete(frame);
}
void UsbVoiceFeedback::AudioChannelOpened() {
    auto now=NowMs();
    if (state_.phase==VoiceFeedbackState::Phase::Paused && !session_.empty() && now-state_.heartbeat<=5000) {
        state_.ResumeDictating(now); mode_locked_=true; allow_audio_=false;
        // Resume only recording. No old utterance or computer action is replayed.
        app_.ResumeAfterVoiceFeedback();
        Reply(0, true, "resumed");
    }
}
void UsbVoiceFeedback::Recognized(const std::string& text) {
    auto now = NowMs();
    auto normalized = Normalize(text);
    Intent next = Intent::Body;
    if (normalized == "进入语音输入" || normalized == "进入语音输入模式" || normalized == "开启语言输入" || normalized == "打开语言输入" || normalized == "开启语言输入功能" || normalized == "打开语言输入功能" || normalized == "开启codex语音输入功能" || normalized == "开启codex语言输入功能" ||
        normalized == "开启codex语音输入" || normalized == "开启语音输入" || normalized == "开启语音输入功能" ||
        normalized == "打开语音输入" || normalized == "打开语音输入功能" || normalized == "开启语音输入模式" ||
        normalized == "打开语音输入模式" || normalized == "打开codex语音输入" || normalized == "打开codex语音输入功能") next = Intent::Start;
    else if (normalized == "发送") next = Intent::Send;
    else if (normalized == "退出模式" || normalized == "退出语音输入模式" || normalized == "退出语音输入功能" ||
             normalized == "退出codex语音输入功能" || normalized == "退出语音输入") next = Intent::Stop;
    bool link_live = !session_.empty() && now - state_.heartbeat <= 5000;
    // Exit is local and explicit, even when the computer link has failed. No model decision may exit.
    if (next == Intent::Stop && state_.Engaged()) {
        ++current_turn_; intent_ = next; authorized_ = Intent::Start;
        state_.Stop(now); state_.SealLocalTurn(now); state_.GrantMode(false); mode_locked_ = true; allow_audio_ = false;
        app_.GetAudioService().EnableVoiceProcessing(false); app_.GetAudioService().DiscardSendQueue();
        if(app_.VoiceInputChannelOpen()) app_.AbortSpeaking(kAbortReasonNone);
        Board::GetInstance().GetDisplay()->ShowNotification("已退出语音输入");
        if (link_live) ForwardSpeech(text);
        return;
    }
    if(link_live) Reply(0,true,next==Intent::Start?"heard_start":next==Intent::Stop?"heard_stop":next==Intent::Send?"heard_send":"heard_body");
    if (!link_live) return;
    if (state_.phase == VoiceFeedbackState::Phase::Chat && next != Intent::Start) return;
    if (state_.phase != VoiceFeedbackState::Phase::Chat && state_.phase != VoiceFeedbackState::Phase::Dictating &&
        state_.phase != VoiceFeedbackState::Phase::Paused) return;
    if (state_.phase == VoiceFeedbackState::Phase::Paused && next != Intent::Start) state_.ResumeDictating(now);
    ++current_turn_; intent_ = next; authorized_ = Intent::Start;
    if (next == Intent::Start) { state_.Start(now); allow_audio_ = false; }
    else if (next == Intent::Stop) { state_.Stop(now); allow_audio_ = false; }
    else state_.Recognized(now);
    state_.SealLocalTurn(now);
    mode_locked_ = state_.Engaged(); allow_audio_=false;
    app_.GetAudioService().EnableVoiceProcessing(false);app_.GetAudioService().DiscardSendQueue();
    if(app_.VoiceInputChannelOpen()) app_.AbortSpeaking(kAbortReasonNone);
    if(next==Intent::Start) Board::GetInstance().GetDisplay()->ShowNotification("语音输入已开启");
    ForwardSpeech(text);
}
void UsbVoiceFeedback::TtsStarted() {
    state_.TtsStarted(NowMs());
    allow_audio_ = !state_.Engaged();
    if (state_.Engaged() && !state_.cue_playing) app_.GetAudioService().EnableVoiceProcessing(false);
}
bool UsbVoiceFeedback::TtsStopped() {
    bool engaged = state_.Engaged();
    state_.TtsStopped(NowMs());
    allow_audio_ = !engaged;
    return engaged;
}

std::string UsbVoiceFeedback::Normalize(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (c==' ' || c=='\t' || c=='\r' || c=='\n') continue;
        out.push_back(c<128 ? std::tolower(c) : c);
    }
    // Strip punctuation only at the edges; never turn an internally punctuated sentence into a command.
    bool changed=true;
    while (changed && !out.empty()) {
        changed=false;
        for (const char* token : {"。", "，", "！", "？", "“", "”", "\"", ".", ",", "!", "?"}) {
            auto size=strlen(token);
            if (out.compare(0,size,token)==0) { out.erase(0,size); changed=true; }
            if (out.size()>=size && out.compare(out.size()-size,size,token)==0) { out.erase(out.size()-size); changed=true; }
        }
    }
    return out;
}

bool UsbVoiceFeedback::Sentence(const std::string& text) {
    if(state_.Engaged()) {allow_audio_=false;return false;}
    auto normalized = Normalize(text);
    bool expected = state_.phase == VoiceFeedbackState::Phase::Entering ? normalized == "语音输入已开启" :
                    state_.phase == VoiceFeedbackState::Phase::Exiting && normalized == "已退出语音输入";
    bool allowed = state_.AllowSentence(expected);
    allow_audio_ = allowed;
    return allowed;
}

void UsbVoiceFeedback::Reset(bool close_channel) {
    bool was_active = state_.Engaged();
    bool changed = state_.PauseMode();
    mode_locked_ = state_.Engaged();
    allow_audio_ = !state_.Engaged();
    if (was_active && close_channel) app_.CloseVoiceInputChannel();
    if (changed) Reply(0, false, "paused");
}

void UsbVoiceFeedback::Tick() {
    auto effect = state_.Poll(NowMs(), app_.GetAudioService().IsPlaybackIdle());
    mode_locked_ = state_.Engaged();
    if (effect == VoiceFeedbackState::Effect::Pause || effect == VoiceFeedbackState::Effect::Abort) {
        allow_audio_ = !state_.Engaged();
        app_.CloseVoiceInputChannel();
        Reply(0, false, "paused");
    } else if (effect == VoiceFeedbackState::Effect::PlayCue) {
        // Warm the microphone during the local cue, but discard its outgoing audio until the cue drains.
        app_.GetAudioService().EnableWakeWordDetection(false);
        app_.GetAudioService().EnableVoiceProcessing(true);
        app_.GetAudioService().PlaySound(state_.phase==VoiceFeedbackState::Phase::Entering ? Lang::Sounds::OGG_VOICE_INPUT_ON :
            state_.phase==VoiceFeedbackState::Phase::Exiting ? Lang::Sounds::OGG_VOICE_INPUT_OFF : Lang::Sounds::OGG_SUCCESS);
        ++cues_;
        Reply(0, true, "cue");
    } else if (effect == VoiceFeedbackState::Effect::Resume) {
        app_.GetAudioService().DiscardSendQueue();
        allow_audio_ = !state_.Engaged();
        if (!state_.Engaged()) {
            app_.CloseVoiceInputChannel();Reply(0,true,"chat");return;
        }
        if (app_.ResumeAfterVoiceFeedback()) {
            ++ready_; Reply(0, true, state_.Engaged() ? "ready" : "chat");
        } else { Reset(true); Reply(0, false, "paused"); }
    }
}
