#include "../main/boards/esp-sparkbot/spark_battery_state.h"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
extern "C" int mainCRTStartup() {
    SparkBatteryState battery;
    int level = -1;
    CHECK(!battery.GetLevel(level));
    CHECK(!battery.AcceptVoltage(0));
    CHECK(!battery.AcceptVoltage(4190));
    CHECK(!battery.AcceptVoltage(4192));
    CHECK(battery.AcceptVoltage(4191));
    CHECK(!battery.GetLevel(level)); // Stable voltage alone must never invent a capacity.
    battery.StoreEstimate(97, false);
    CHECK(battery.GetLevel(level) && level == 97 && !battery.IsCharging());
    CHECK(battery.GetLevel(level) && level == 97); // Repeated icon/text reads use the cached estimator result.
    battery.StoreEstimate(98, true);
    CHECK(battery.GetLevel(level) && level == 98 && battery.IsCharging());
    CHECK(!battery.AcceptVoltage(4800));
    CHECK(!battery.GetLevel(level));
    CHECK(!battery.AcceptVoltage(3100));
    CHECK(!battery.AcceptVoltage(3100));
    CHECK(battery.AcceptVoltage(3100));
    CHECK(!battery.GetLevel(level));
    battery.StoreEstimate(4, false);
    CHECK(battery.GetLevel(level) && level == 4); // Preserve the official curve result even at the old linear-empty voltage.
    battery.Invalidate();
    for (int i = 0; i < 30; ++i) {
        CHECK(!battery.AcceptVoltage(i % 2 ? 4200 : 3100));
        CHECK(!battery.GetLevel(level));
    }
    CHECK(!battery.AcceptVoltage(0));
    CHECK(!battery.GetLevel(level));
    return 0;
}
