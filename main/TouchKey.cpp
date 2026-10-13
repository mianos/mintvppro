#include "TouchKey.h"

#include <cinttypes>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
const char* TAG = "touch";
}

void TouchKey::init() {
    touch_sensor_sample_config_t sample[TOUCH_SAMPLE_CFG_NUM] = {
        TOUCH_SENSOR_V1_DEFAULT_SAMPLE_CONFIG(5.0, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_1V7)};
    touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, sample);
    ESP_ERROR_CHECK(touch_sensor_new_controller(&sens_cfg, &sens_));

    // Hardware threshold is unused (we poll), but must be set to something sane.
    touch_channel_config_t chan_cfg = {};
    chan_cfg.abs_active_thresh[0] = 0;
    chan_cfg.charge_speed = TOUCH_CHARGE_SPEED_7;
    chan_cfg.init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT;
    chan_cfg.group = TOUCH_CHAN_TRIG_GROUP_BOTH;
    ESP_ERROR_CHECK(touch_sensor_new_channel(sens_, kChannel, &chan_cfg, &chan_));

    touch_sensor_filter_config_t filter = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    ESP_ERROR_CHECK(touch_sensor_config_filter(sens_, &filter));
    ESP_ERROR_CHECK(touch_sensor_enable(sens_));
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(sens_));

    // The smoothed reading climbs from zero for a couple of seconds after start;
    // wait until it stops rising before taking the baseline.
    uint32_t v = 0, prev = 0;
    for (int i = 0; i < 50; ++i) {
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_ERROR_CHECK(touch_channel_read_data(chan_, TOUCH_CHAN_DATA_TYPE_SMOOTH, &v));
        if (prev && v < prev + prev / 100) break;
        prev = v;
    }
    baseline_ = float(v);
    ESP_LOGI(TAG, "T%d baseline %" PRIu32 ", press below %.0f", kChannel, v, baseline_ * kPressRatio);
}

bool TouchKey::pressed() {
    uint32_t v = 0;
    if (touch_channel_read_data(chan_, TOUCH_CHAN_DATA_TYPE_SMOOTH, &v) != ESP_OK) return last_;
    const bool low = float(v) < baseline_ * (last_ ? kReleaseRatio : kPressRatio);
    lowPolls_ = low ? lowPolls_ + 1 : 0;
    bool now = last_ ? low : lowPolls_ >= kPressPolls;
    if (now && lowPolls_ >= kStuckPolls) {
        ESP_LOGW(TAG, "low for 30 s (value %" PRIu32 ", baseline %.0f), recalibrating", v, baseline_);
        baseline_ = float(v);
        lowPolls_ = 0;
        now = false;
    }
    if (!now) baseline_ += (float(v) - baseline_) * 0.01f;
    if (now != last_) {
        ESP_LOGI(TAG, "%s (value %" PRIu32 ", baseline %.0f)", now ? "pressed" : "released", v, baseline_);
    }
    last_ = now;
    value_ = v;
    baselineShown_ = static_cast<uint32_t>(baseline_);
    return now;
}
