#pragma once

// Qualifies voltage readings and caches the upstream estimator result without remapping it.
class SparkBatteryState {
public:
    void Invalidate() {
        samples_ = next_ = 0;
        ready_ = false;
        estimated_ = false;
    }

    bool AcceptVoltage(int millivolts) {
        if (millivolts < 2500 || millivolts > 4500) {
            Invalidate();
            return false;
        }
        voltages_[next_] = millivolts;
        next_ = (next_ + 1) % 3;
        if (samples_ < 3) ++samples_;
        if (samples_ < 3) return false;
        int minimum = voltages_[0], maximum = voltages_[0];
        for (int voltage : voltages_) {
            if (voltage < minimum) minimum = voltage;
            if (voltage > maximum) maximum = voltage;
        }
        ready_ = maximum - minimum <= 150;
        return ready_;
    }

    void StoreEstimate(int level, bool charging) {
        level_ = level;
        charging_ = charging;
        estimated_ = true;
    }

    bool GetLevel(int& level) const {
        if (!ready_ || !estimated_) return false;
        level = level_;
        return true;
    }

    bool IsCharging() const { return charging_; }

private:
    int voltages_[3]{};
    int samples_ = 0;
    int next_ = 0;
    int level_ = 0;
    bool ready_ = false;
    bool estimated_ = false;
    bool charging_ = false;
};
