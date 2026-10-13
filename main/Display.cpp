#include "Display.h"

#include <algorithm>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "freertos/task.h"

namespace {
const char* TAG = "display";

// GeekMagic SmallTV Pro (ESP32) pin map, from giovi321/smalltv-mod board_esp32_pro.h.
constexpr spi_host_device_t kHost = SPI2_HOST;
constexpr gpio_num_t kMosi = GPIO_NUM_23;
constexpr gpio_num_t kSclk = GPIO_NUM_18;
constexpr gpio_num_t kDc = GPIO_NUM_2;
constexpr gpio_num_t kRst = GPIO_NUM_4;
constexpr gpio_num_t kBacklight = GPIO_NUM_25;  // active low
// CS is tied to GND on the PCB.
constexpr uint32_t kPclkHz = 40 * 1000 * 1000;
constexpr ledc_channel_t kBlChannel = LEDC_CHANNEL_0;

struct LcdCmd {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[14];
};

// Power, VCOM and gamma setup from TFT_eSPI's ST7789_Init.h; esp_lcd's init
// leaves these at the panel's reset defaults.
constexpr LcdCmd kInitCmds[] = {
    {0x13, 0, {}},                                    // NORON
    {0xB6, 2, {0x0A, 0x82}},                          // display function control
    {0xB0, 2, {0x00, 0xE0}},                          // RAMCTRL
    {0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}},        // PORCTRL
    {0xB7, 1, {0x35}},                                // GCTRL
    {0xBB, 1, {0x28}},                                // VCOMS
    {0xC0, 1, {0x0C}},                                // LCMCTRL
    {0xC2, 2, {0x01, 0xFF}},                          // VDVVRHEN
    {0xC3, 1, {0x10}},                                // VRHS
    {0xC4, 1, {0x20}},                                // VDVSET
    {0xC6, 1, {0x0F}},                                // FRCTR2
    {0xD0, 2, {0xA4, 0xA1}},                          // PWCTRL1
    {0xE0, 14, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17}},
    {0xE1, 14, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E}},
};
}  // namespace

void Band::fill(uint16_t color) {
    std::fill_n(px_, kWidth * h_, __builtin_bswap16(color));
}

void Band::fillRect(int x, int y, int w, int h, uint16_t color) {
    int x0 = std::max(x, 0), x1 = std::min(x + w, kWidth);
    int y0 = std::max(y, y0_), y1 = std::min(y + h, y0_ + h_);
    if (x0 >= x1 || y0 >= y1) return;
    // ST7789 takes RGB565 big-endian over SPI.
    uint16_t c = __builtin_bswap16(color);
    for (int row = y0; row < y1; ++row) {
        std::fill_n(px_ + (row - y0_) * kWidth + x0, x1 - x0, c);
    }
}

bool IRAM_ATTR Display::onTransDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* ctx) {
    auto* self = static_cast<Display*>(ctx);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(self->free_, &woken);
    return woken == pdTRUE;
}

void Display::init() {
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t ch = {};
    ch.gpio_num = kBacklight;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel = kBlChannel;
    ch.timer_sel = LEDC_TIMER_0;
    ch.duty = 0;
    ch.flags.output_invert = 1;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    spi_bus_config_t bus = {};
    bus.mosi_io_num = kMosi;
    bus.miso_io_num = -1;
    bus.sclk_io_num = kSclk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kWidth * kBandRows * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(kHost, &bus, SPI_DMA_CH_AUTO));

    free_ = xSemaphoreCreateCounting(2, 2);

    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.cs_gpio_num = GPIO_NUM_NC;
    io_cfg.dc_gpio_num = kDc;
    io_cfg.spi_mode = 3;  // CS is tied low, so the panel needs mode 3 to frame bytes
    io_cfg.pclk_hz = kPclkHz;
    io_cfg.trans_queue_depth = 4;
    io_cfg.on_color_trans_done = onTransDone;
    io_cfg.user_ctx = this;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(kHost, &io_cfg, &io));

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = kRst;
    panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
    // The driver waits only 10 ms after reset; the ST7789 ignores SLPOUT for 120 ms.
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
    for (const LcdCmd& c : kInitCmds) {
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, c.cmd, c.len ? c.data : nullptr, c.len));
    }
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));

    for (auto& b : buf_) {
        b = static_cast<uint16_t*>(heap_caps_malloc(kWidth * kBandRows * sizeof(uint16_t), MALLOC_CAP_DMA));
        assert(b);
    }

    // Clear before turning the panel on so power-up garbage never shows.
    render([](Band& b) { b.fill(0); });
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));
    ESP_LOGI(TAG, "ST7789 %dx%d up", kWidth, kHeight);
}

void Display::setBrightness(uint32_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, kBlChannel, std::min<uint32_t>(duty, 1023));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, kBlChannel);
}

void Display::render(const std::function<void(Band&)>& draw) {
    for (int y = 0; y < kHeight; y += kBandRows) {
        int rows = std::min(kBandRows, kHeight - y);
        xSemaphoreTake(free_, portMAX_DELAY);
        uint16_t* px = buf_[next_];
        next_ ^= 1;
        Band band(px, y, rows);
        draw(band);
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_, 0, y, kWidth, y + rows, px));
    }
}
