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
    // Copies a w x h image of big-endian RGB565 pixels (the panel's byte order).
    void blit(int x, int y, int w, int h, const uint8_t* px);
    // Paints color through a w x h coverage map (0 = keep, 255 = solid), for
    // anti-aliased text.
    void blend(int x, int y, int w, int h, const uint8_t* alpha, uint16_t color);
    // Adds light: each intensity (0-255) in the w x h map picks an RGB colour
    // from ramp that is added to the pixel, saturating, then dithered to RGB565.
    void addGlow(int x, int y, int w, int h, const uint8_t* intensity, const uint8_t (*ramp)[3]);

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
