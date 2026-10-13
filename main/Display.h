#pragma once

#include <cstdint>
#include <functional>

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// A horizontal strip of the screen. Drawing calls take screen coordinates and
// clip to the strip, so a scene can be drawn unchanged into every band.
class Band {
public:
    static constexpr int kWidth = 240;

    Band(uint16_t* px, int y0, int height) : px_(px), y0_(y0), h_(height) {}

    void fill(uint16_t color);
    void fillRect(int x, int y, int w, int h, uint16_t color);

private:
    uint16_t* px_;
    int y0_;
    int h_;
};

class Display {
public:
    static constexpr int kWidth = 240;
    static constexpr int kHeight = 240;

    void init();
    // 0 = off, 1023 = full.
    void setBrightness(uint32_t duty);
    void render(const std::function<void(Band&)>& draw);

private:
    static constexpr int kBandRows = 40;
    static bool onTransDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* ctx);

    esp_lcd_panel_handle_t panel_ = nullptr;
    uint16_t* buf_[2] = {};
    int next_ = 0;
    // Counts free band buffers; the SPI done-callback returns them.
    SemaphoreHandle_t free_ = nullptr;
};
