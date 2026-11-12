#include "../main/voice_feedback_state.h"
using State=VoiceFeedbackState;
using Effect=State::Effect;
#define CHECK(c) do { if(!(c)) return __LINE__; } while(0)
extern "C" int mainCRTStartup() {
    State s;
    CHECK(s.Poll(0,true)==Effect::None);
    s.Start(10);
    CHECK(!s.AllowSentence(false));CHECK(s.AllowSentence(true));CHECK(!s.AllowSentence(true));
    s.TtsStopped(20);
    CHECK(s.Poll(21,true)==Effect::None); // Cloud acknowledgement cannot enable mode without the computer.
    CHECK(s.GrantMode(true));CHECK(s.Poll(22,false)==Effect::None);CHECK(s.Poll(23,true)==Effect::Resume);
    CHECK(s.phase==State::Phase::Dictating && !s.AllowSentence(true));
    CHECK(!s.Typed(24)); // No actual STT, no completion grant.
    s.Recognized(30);s.TtsStarted(31);s.TtsStopped(32);
    CHECK(s.Poll(33,true)==Effect::None); // Cloud stop may precede computer completion: wait, do not exit.
    CHECK(s.Typed(34));CHECK(!s.Typed(35));
    CHECK(s.Poll(36,true)==Effect::PlayCue);CHECK(s.Poll(37,false)==Effect::None);
    CHECK(s.Poll(38,true)==Effect::Resume);CHECK(s.Poll(39,true)==Effect::None);
    s.Recognized(40);CHECK(s.Typed(41));CHECK(s.Poll(42,true)==Effect::None); // Reverse arrival order.
    s.TtsStopped(43);CHECK(s.Poll(44,true)==Effect::PlayCue);CHECK(s.Poll(45,true)==Effect::Resume);
    s.Recognized(50);CHECK(s.Sent(51));s.TtsStopped(52);CHECK(s.Poll(53,true)==Effect::Resume); // Explicit send is silent.
    s.Recognized(60);CHECK(s.Typed(61));s.Stop(62);CHECK(!s.cue_pending);CHECK(s.AllowSentence(true));
    s.TtsStopped(63);CHECK(s.Poll(64,true)==Effect::None);CHECK(s.GrantMode(false));
    CHECK(s.Poll(65,true)==Effect::Resume && !s.Engaged());CHECK(s.AllowSentence(false));
    s.Start(70);CHECK(s.GrantMode(true));s.TtsStopped(71);CHECK(s.Poll(72,true)==Effect::Resume);
    s.Recognized(73);CHECK(s.Typed(74));s.TtsStopped(75);CHECK(s.Poll(76,true)==Effect::PlayCue);CHECK(s.Poll(77,true)==Effect::Resume);
    s.Stop(78);CHECK(s.GrantMode(false));s.TtsStopped(79);CHECK(s.Poll(80,true)==Effect::Resume && !s.Engaged());
    s.Start(100);s.GrantMode(true);s.TtsStopped(101);s.Poll(102,true);s.Recognized(103);s.Typed(104);
    CHECK(s.Poll(5101,true)==Effect::Pause && s.Engaged() && s.BlocksDeviceTools()); // Lost computer lease.
    s.Stop(6000);s.GrantMode(false);s.TtsStopped(6001);s.heartbeat=0;CHECK(s.Poll(6002,true)==Effect::Resume && !s.BlocksDeviceTools());
    s.Start(10000);s.GrantMode(true);s.TtsStopped(10001);s.Poll(10002,true);s.Recognized(10003);s.TtsStopped(10004);
    s.heartbeat=40005;CHECK(s.Poll(40005,true)==Effect::Pause && s.Engaged() && s.BlocksDeviceTools()); // No computer result ever arrived: no guessed cue.
    CHECK(s.phase==State::Phase::Paused && s.Poll(999999,true)==Effect::None); // Never silently restore normal chat.
    s.ResumeDictating(1000000);s.Recognized(1000001);CHECK(s.Typed(1000002));s.TtsStopped(1000003);
    CHECK(s.Poll(1000004,true)==Effect::PlayCue && s.BlocksDeviceTools());s.Poll(1000005,true);
    s.Stop(1000006);s.GrantMode(false);s.TtsStopped(1000007);CHECK(s.Poll(1000008,true)==Effect::Resume && !s.BlocksDeviceTools());
    // Native mode has no model/MCP/TTS completion dependency.
    s.Start(2000000);s.SealLocalTurn(2000000);CHECK(s.Poll(2000001,true)==Effect::None);
    CHECK(s.GrantMode(true));CHECK(s.Poll(2000002,true)==Effect::PlayCue);
    s.TtsStarted(2000003);s.TtsStopped(2000004); // Late model notifications cannot hold the local cue open.
    CHECK(s.Poll(2000005,true)==Effect::Resume && s.phase==State::Phase::Dictating);
    s.Recognized(2000006);s.SealLocalTurn(2000006);CHECK(s.Poll(2000007,true)==Effect::None);
    CHECK(s.Typed(2000008));CHECK(s.Poll(2000009,true)==Effect::PlayCue);CHECK(s.Poll(2000010,true)==Effect::Resume);
    s.Recognized(2000011);s.SealLocalTurn(2000011);CHECK(s.Sent(2000012));CHECK(s.Poll(2000013,true)==Effect::Resume);
    s.PauseMode();CHECK(s.Engaged());s.Stop(2000014);s.SealLocalTurn(2000014);CHECK(s.GrantMode(false));
    CHECK(s.Poll(2000015,true)==Effect::PlayCue);CHECK(s.Poll(2000016,true)==Effect::Resume && !s.Engaged());
    return 0;
}