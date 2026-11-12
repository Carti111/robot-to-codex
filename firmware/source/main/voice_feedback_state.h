#pragma once
// Only actual STT plus a matching computer acknowledgement can advance an input turn.
class VoiceFeedbackState {
public:
    using Time = unsigned long long;
    enum class Phase { Chat, Entering, Dictating, Exiting, Paused };
    enum class Effect { None, PlayCue, Resume, Abort, Pause };
    Phase phase = Phase::Chat;
    bool cue_playing=false, cue_pending=false, turn_open=false, boundary=false;
    bool announcement_used=false, completion_received=false, mode_granted=false, awaiting_completion=false;
    bool local_turn=false;
    Time heartbeat=0, deadline=0;
    bool Engaged() const { return phase!=Phase::Chat; }
    void Reset() {
        phase=Phase::Chat;
        cue_playing=cue_pending=turn_open=boundary=announcement_used=completion_received=mode_granted=awaiting_completion=false;
        deadline=0; local_turn=false;
    }
    bool PauseMode() {
        if (!Engaged()) return false;
        if (phase==Phase::Exiting) { Reset(); return false; } // Only an actual exit utterance creates Exiting.
        bool changed=phase!=Phase::Paused;
        cue_playing=cue_pending=turn_open=boundary=announcement_used=completion_received=mode_granted=awaiting_completion=false;
        phase=Phase::Paused; deadline=0; return changed;
    }
    void ResumeDictating(Time now) {
        phase=Phase::Dictating; heartbeat=now; deadline=0;
        cue_playing=cue_pending=turn_open=boundary=completion_received=awaiting_completion=false;
    }
    void SealLocalTurn(Time now) { local_turn=true; turn_open=false; boundary=true; deadline=now+30000; }
    bool BlocksDeviceTools() const { return Engaged(); }
    void Start(Time now) { Reset(); phase=Phase::Entering; heartbeat=now; deadline=now+30000; turn_open=true; }
    void Stop(Time now) { Reset(); phase=Phase::Exiting; heartbeat=now; deadline=now+30000; turn_open=true; }
    bool GrantMode(bool active) {
        if (phase!=(active?Phase::Entering:Phase::Exiting)) return false;
        mode_granted=true; if(local_turn) cue_pending=true; return true;
    }
    void Recognized(Time now) {
        if (!Engaged()) return;
        turn_open=true; boundary=false; awaiting_completion=true; cue_pending=completion_received=false; deadline=now+30000;
    }
    void TtsStarted(Time now) {
        if (!Engaged() || local_turn) return;
        turn_open=true; boundary=false; deadline=now+30000;
    }
    void TtsStopped(Time now) {
        if (!Engaged() || local_turn) return;
        turn_open=false; boundary=true; deadline=now+30000;
    }
    bool Typed(Time now) {
        if (phase!=Phase::Dictating || !awaiting_completion || cue_playing || cue_pending || completion_received) return false;
        cue_pending=completion_received=true; deadline=now+30000; return true;
    }
    bool Sent(Time now) {
        if (phase!=Phase::Dictating || !awaiting_completion || cue_playing || completion_received) return false;
        cue_pending=false; completion_received=true; deadline=now+30000; return true;
    }
    bool AllowSentence(bool expected) {
        if (phase==Phase::Chat) return true;
        if (phase==Phase::Dictating || !expected || announcement_used) return false;
        announcement_used=true; return true;
    }
    Effect Poll(Time now,bool playback_idle) {
        if (!Engaged() || phase==Phase::Paused) return Effect::None;
        if (phase==Phase::Exiting && mode_granted && deadline && now>deadline) { Reset(); return Effect::Resume; }
        if (phase!=Phase::Exiting && (now-heartbeat>5000 || (deadline && now>deadline))) { PauseMode(); return Effect::Pause; }
        if (cue_playing) {
            if (!playback_idle || turn_open) return Effect::None;
            cue_playing=false; boundary=false; awaiting_completion=false; deadline=0;
            if(phase==Phase::Entering) phase=Phase::Dictating;
            else if(phase==Phase::Exiting) Reset();
            return Effect::Resume;
        }
        if (!boundary || !playback_idle) return Effect::None;
        if (phase==Phase::Entering) {
            if (!mode_granted) return Effect::None;
            if(local_turn && cue_pending) {cue_pending=false;cue_playing=true;deadline=now+5000;return Effect::PlayCue;}
            phase=Phase::Dictating; boundary=false; deadline=0; return Effect::Resume;
        }
        if (phase==Phase::Exiting) {
            if (!mode_granted) return Effect::None;
            if(local_turn && cue_pending) {cue_pending=false;cue_playing=true;deadline=now+5000;return Effect::PlayCue;}
            Reset(); return Effect::Resume;
        }
        if (!awaiting_completion) { boundary=false; deadline=0; return Effect::Resume; }
        // Cloud completion and computer completion may arrive in either order.
        if (!completion_received) return Effect::None;
        if (cue_pending) { cue_pending=false; cue_playing=true; deadline=now+5000; return Effect::PlayCue; }
        boundary=false; awaiting_completion=false; deadline=0; return Effect::Resume;
    }
};
