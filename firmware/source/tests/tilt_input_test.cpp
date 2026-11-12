#include "../main/boards/esp-sparkbot/spark_tilt_input.h"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
extern "C" int mainCRTStartup() {
    SparkTiltInput input;
    CHECK(input.Update(0, 0, 1000) == -1);
    unsigned moves = 0;
    for (unsigned now = 1040; now <= 2040; now += 40) {
        int direction = input.Update(200, 0, now);
        if (direction < 0) continue;
        CHECK(direction == 2);
        ++moves;
    }
    CHECK(moves == 1); // One broad turn must not repeat at a fixed frequency.
    for (unsigned now = 2080; now <= 2520; now += 40) CHECK(input.Update(20, 15, now) == -1);
    CHECK(input.Update(-200, 0, 2560) == -1);
    CHECK(input.Update(-200, 0, 2600) == -1);
    CHECK(input.Update(-200, 0, 2640) == 0);
    CHECK(input.Update(0, 200, 2680) == -1); // Early cross-axis recoil cannot become a delayed step.
    for (unsigned now = 2720; now <= 3000; now += 40) CHECK(input.Update(0, 200, now) == -1);
    CHECK(input.Update(0, -200, 3040) == -1);
    CHECK(input.Update(0, -200, 3080) == -1);
    CHECK(input.Update(0, -200, 3120) == 3);
    for (unsigned now = 3160; now <= 3400; now += 40) CHECK(input.Update(0, -200, now) == -1);
    CHECK(input.Update(0, 200, 3440) == -1);
    CHECK(input.Update(0, 200, 3480) == -1);
    CHECK(input.Update(0, 200, 3520) == 1); // Deliberate changes work without a stationary or neutral pose.
    for (unsigned now = 3560; now <= 3800; now += 40) CHECK(input.Update(3, -3, now) == -1);
    CHECK(input.Update(300, 300, 5000) == -1); // A delayed sample is discarded.
    CHECK(input.Update(50, 200, 5040) == -1);
    CHECK(input.Update(50, 200, 5080) == -1);
    CHECK(input.Update(50, 200, 5120) == 1);

    input.Reset();
    CHECK(input.Update(0, 0, 6000) == -1);
    CHECK(input.Update(50, 0, 6040) == -1);
    CHECK(input.Update(80, 0, 6080) == -1);
    CHECK(input.Update(150, 0, 6120) == -1); // 11.2 degrees remains inside the angular dead zone.
    CHECK(input.Update(120, 0, 6160) == -1); // 16 degrees remains below the requested 20-degree threshold.
    CHECK(input.Update(100, 0, 6200) == 2);
    CHECK(input.Update(35, 0, 6240) == -1);
    for (unsigned now = 6280; now <= 6800; now += 40) CHECK(input.Update(20, 0, now) == -1);
    CHECK(input.Update(0, 200, 6840) == -1);
    CHECK(input.Update(0, 200, 6880) == -1);
    CHECK(input.Update(0, 200, 6920) == 1);

    input.Reset();
    CHECK(input.Update(0, 0, 7000) == -1);
    for (unsigned now = 7040; now <= 7360; now += 40) CHECK(input.Update(50, 0, now) == -1);
    CHECK(input.Update(50, 0, 7400) == 2);
    for (unsigned now = 7440; now <= 7800; now += 40) CHECK(input.Update(20, 0, now) == -1);
    for (unsigned now = 7840; now <= 8160; now += 40) CHECK(input.Update(50, 0, now) == -1);
    CHECK(input.Update(50, 0, 8200) == 2); // A larger stroke restarts after a natural speed valley.

    // Fast but sub-threshold 19.2-degree turns, including the return movement, stay inside the dead zone.
    for (int axis = 0; axis < 2; ++axis) {
        for (int sign = -1; sign <= 1; sign += 2) {
            input.Reset();
            unsigned now = 8000;
            CHECK(input.Update(0, 0, now) == -1);
            for (unsigned cycle = 0; cycle < 20; ++cycle) {
                for (int sample = 0; sample < 8; ++sample) {
                    float rate = 120.0f * sign;
                    if (sample >= 4) rate = -rate;
                    now += 40;
                    CHECK(input.Update(axis == 0 ? rate : 0, axis == 1 ? rate : 0, now) == -1);
                }
                now += 40;
                CHECK(input.Update(0, 0, now) == -1);
            }
        }
    }
    input.Reset();
    CHECK(input.Update(0, 0, 20000) == -1);
    for (unsigned now = 20040; now <= 22000; now += 40) CHECK(input.Update(19, -19, now) == -1);
    return 0;
}
