#pragma once

#include <cstdint>

#include "driver/touch_sens.h"

// The front capacitive key (T9 / GPIO32). ESP32 touch readings drop when
// touched, so "pressed" is a fraction below a slowly tracked baseline.
class TouchKey {
public:
    void init();
    // Call periodically (~20 ms); also updates the baseline while released.
    bool pressed();

private:
    static constexpr int kChannel = 9;
    // A finger drops this pad's reading only ~20%, so press at 10% with
    // hysteresis to stop a marginal touch chattering.
    static constexpr float kPressRatio = 0.90f;
    static constexpr float kReleaseRatio = 0.95f;
    // Phantom dips seen in the field last 20-60 ms; a deliberate tap is longer.
    static constexpr int kPressPolls = 5;

    touch_sensor_handle_t sens_ = nullptr;
    touch_channel_handle_t chan_ = nullptr;
    float baseline_ = 0;
    bool last_ = false;
    int lowPolls_ = 0;
};
