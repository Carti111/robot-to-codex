#pragma once
#include <functional>
#include <string_view>
class Application {
public:
 static Application& GetInstance() { static Application value; return value; }
 bool locked = false;
 bool IsVoiceInputLocked() const { return locked; }
 bool IsVoiceInputPaused() const { return false; }
 bool IsVoiceInputConnected() const { return true; }
 void StartVoiceInputFromTouch() { locked = true; }
 void StopVoiceInputFromTouch() { locked = false; }
 void EndFeatureAudio() {}
 void ToggleChatState() {}
 void WakeWordInvoke(const char*) {}
 void BeginVoicePairing() {}
 void PlaySound(std::string_view) {}
 void Schedule(std::function<void()> action) { action(); }
};
