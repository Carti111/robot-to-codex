#pragma once
class Board {
public:
    static Board& GetInstance() { static Board board; return board; }
    bool GetBatteryLevel(int& level, bool& charging, bool& discharging) {
        level = battery_level;
        charging = false;
        discharging = true;
        return battery_available;
    }
    int battery_level = 75;
    bool battery_available = true;
};
