#pragma once
#include <atomic>
#include <string>
#include "voice_feedback_state.h"
#include "wifi_voice_link.h"
class Application;

class UsbVoiceFeedback {
public:
    explicit UsbVoiceFeedback(Application& app) : app_(app) {}
    void Start();
    void BeginPairing();
    bool Connected() const;
    bool PhysicalMode(bool active);
    bool Paused() const { return state_.phase == VoiceFeedbackState::Phase::Paused; }
    void Tick();
    void Recognized(const std::string& text);
    void AudioChannelOpened();
    void TtsStarted();
    bool TtsStopped();
    bool Sentence(const std::string& text);
    bool AllowAudio() const { return allow_audio_.load(); }
    bool HoldOutgoing() const { return state_.cue_playing; }
    bool Engaged() const { return mode_locked_.load(); }
    void DroppedAudio() { ++dropped_audio_; }
    void DeviceToolBlocked() { ++blocked_tools_; Reply(0, false, "tool_blocked"); }
    void Reset(bool close_channel);
private:
    Application& app_;
    WifiVoiceLink wifi_;
    bool wireless_session_ = false;
    void WriteFrame(const std::string& frame);
    VoiceFeedbackState state_;
    std::atomic<bool> allow_audio_{true};
    std::atomic<bool> mode_locked_{false};
    std::atomic<unsigned> queued_frames_{0};
    std::atomic<unsigned> dropped_audio_{0};
    std::string session_;
    unsigned last_id_ = 0, cues_ = 0, ready_ = 0, blocked_tools_ = 0;
    bool last_ok_ = false;
    unsigned current_turn_ = 0;
    enum class Intent { Body, Start, Send, Stop };
    Intent intent_ = Intent::Body;
    Intent authorized_ = Intent::Start; // Not a body/send authorization.
    static std::string Normalize(const std::string& text);
    void ForwardSpeech(const std::string& text);
    void ReadLoop();
    void Process(const std::string& line, bool wireless = false);
    void Reply(unsigned id, bool ok, const char* event);
};
