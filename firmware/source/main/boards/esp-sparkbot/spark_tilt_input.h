#pragma once

// A stroke produces one step. Motion valleys or deliberate direction changes delimit the next stroke.
class SparkTiltInput {
public:
    void Reset() {
        stage_ = Stage::Tracking;
        started_ = false;
        pitch_angle_ = yaw_angle_ = peak_speed_ = 0;
        restart_speed_ = 28;
        observed_direction_ = -1;
        last_sample_ = gesture_since_ = last_step_ = low_since_ = 0;
    }

    int Update(float pitch_rate, float yaw_rate, unsigned long long now) {
        if (!started_ || now <= last_sample_ || now - last_sample_ > 160) {
            Reset();
            started_ = true;
            last_sample_ = now;
            return -1;
        }
        float seconds = static_cast<float>(now - last_sample_) / 1000.0f;
        last_sample_ = now;
        float vertical = Absolute(pitch_rate);
        float horizontal = Absolute(yaw_rate);
        float speed = Maximum(vertical, horizontal);

        if (stage_ == Stage::Latched) {
            peak_speed_ = Maximum(peak_speed_, speed);
            int direction = StrongDirection(pitch_rate, yaw_rate);
            if (direction >= 0 && direction != observed_direction_) {
                // Record early recoil too, so that it cannot become a delayed extra step.
                observed_direction_ = direction;
                low_since_ = 0;
                if (now - last_step_ >= 240) {
                    stage_ = Stage::Tracking;
                    ClearStroke();
                }
            }
            if (stage_ == Stage::Latched) {
                float release_speed = Maximum(25, peak_speed_ * 0.30f);
                if (release_speed > 40) release_speed = 40;
                if (speed <= release_speed) {
                    if (!low_since_) low_since_ = now;
                    if (now - low_since_ >= 80) {
                        stage_ = Stage::AwaitingStroke;
                        restart_speed_ = Maximum(28, release_speed * 1.2f);
                        ClearStroke();
                    }
                } else {
                    low_since_ = 0;
                }
                return -1;
            }
        }

        if (stage_ == Stage::AwaitingStroke) {
            if (speed < restart_speed_ || now - last_step_ < 240) return -1;
            stage_ = Stage::Tracking;
            ClearStroke();
        }
        if (speed < 20) {
            ClearStroke();
            return -1;
        }
        if (!gesture_since_ || now - gesture_since_ > 450) {
            ClearStroke();
            gesture_since_ = now;
        }
        // Sub-threshold motion on either axis must not build up into a gesture.
        if (Absolute(pitch_rate) >= 20) pitch_angle_ += pitch_rate * seconds;
        if (Absolute(yaw_rate) >= 20) yaw_angle_ += yaw_rate * seconds;
        vertical = Absolute(pitch_angle_);
        horizontal = Absolute(yaw_angle_);
        if (vertical < 20 && horizontal < 20) return -1;
        int direction;
        if (vertical > horizontal) direction = pitch_angle_ < 0 ? 0 : 2;
        else direction = yaw_angle_ > 0 ? 1 : 3;
        stage_ = Stage::Latched;
        last_step_ = now;
        observed_direction_ = direction;
        peak_speed_ = speed;
        low_since_ = 0;
        ClearStroke();
        return direction;
    }

private:
    enum class Stage { Tracking, Latched, AwaitingStroke };
    Stage stage_ = Stage::Tracking;
    bool started_ = false;
    float pitch_angle_ = 0;
    float yaw_angle_ = 0;
    float peak_speed_ = 0;
    float restart_speed_ = 28;
    int observed_direction_ = -1;
    unsigned long long last_sample_ = 0;
    unsigned long long gesture_since_ = 0;
    unsigned long long last_step_ = 0;
    unsigned long long low_since_ = 0;

    void ClearStroke() {
        pitch_angle_ = yaw_angle_ = 0;
        gesture_since_ = 0;
    }
    static float Absolute(float value) { return value < 0 ? -value : value; }
    static float Maximum(float a, float b) { return a > b ? a : b; }
    static int StrongDirection(float pitch, float yaw) {
        float vertical = Absolute(pitch);
        float horizontal = Absolute(yaw);
        if (vertical >= 30 && vertical > horizontal * 1.5f) return pitch < 0 ? 0 : 2;
        if (horizontal >= 30 && horizontal > vertical * 1.5f) return yaw > 0 ? 1 : 3;
        return -1;
    }
};
