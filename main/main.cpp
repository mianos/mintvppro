// mintvpro — desk clock for the GeekMagic SmallTV Pro.
//
// Wi-Fi: ESP-Touch v2 provisioning on first boot (mianesp WiFiManager), then SNTP.
// OTA: POST the .bin to /firmware (see WebApi.h).
// Touch key: tap steps the backlight dimmer, wrapping to brightest (saved in NVS);
//            hold 10 s within a minute of power-on clears Wi-Fi and reboots into provisioning.
// Sunset: computed locally for the configured location, on the date line.
// Settings (time, location, MQTT): web page at /, or GET/POST /config.
// Temperature: inside/outside from Node-RED retained MQTT topics, above the digits.

#include <atomic>
#include <cstdlib>
#include <ctime>
#include <string>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "ClockFace.h"
#include "Display.h"
#include "LogBuffer.h"
#include "MqttClient.h"
#include "NvsStorageManager.h"
#include "Settings.h"
#include "Sun.h"
#include "Temperature.h"
#include "TouchKey.h"
#include "WebApi.h"
#include "wifimanager.h"

namespace {

const char* TAG = "clock";

constexpr uint32_t kBrightness[] = {40, 120, 300, 600, 1023};
constexpr int kLevels = sizeof(kBrightness) / sizeof(kBrightness[0]);
constexpr TickType_t kResetHold = pdMS_TO_TICKS(10000);
// Wi-Fi reset by touch is only armed just after power-on, so a stuck or phantom
// touch while running can't wipe the credentials.
constexpr TickType_t kResetWindow = pdMS_TO_TICKS(60000);
constexpr TickType_t kOtaVerifyTimeout = pdMS_TO_TICKS(120000);
// WiFiManager retries on every disconnect, but has been seen to stay off the
// network; a software restart recovers it and keeps the RTC log for /log?prev.
constexpr TickType_t kOfflineRestart = pdMS_TO_TICKS(3 * 60 * 1000);

std::atomic<NetState> s_net{NetState::Connecting};

// Mirrors of the settings the main loop reads, written by /config's onChange
// hooks on the HTTP task.
std::atomic<bool> s_use24h{false};
std::atomic<double> s_latitude{0};
std::atomic<double> s_longitude{0};
std::atomic<bool> s_sunsetDirty{false};

void applyTz(const std::string& tz) {
    setenv("TZ", tz.c_str(), 1);
    tzset();
}

// NvsStorageManager treats these as success without erasing, leaving NVS
// uninitialised (e.g. a partition left over from previous firmware).
void initNvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition unusable (%s), erasing", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void onIpEvent(void*, esp_event_base_t base, int32_t id, void*) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) s_net = NetState::Online;
}

void onWifiEvent(void*, esp_event_base_t, int32_t id, void* data) {
    if (id != WIFI_EVENT_STA_DISCONNECTED) return;
    const auto* ev = static_cast<const wifi_event_sta_disconnected_t*>(data);
    ESP_LOGW(TAG, "Wi-Fi disconnected, reason %d, rssi %d", ev->reason, ev->rssi);
    NetState online = NetState::Online;
    s_net.compare_exchange_strong(online, NetState::Connecting);
}

bool otaPendingVerify() {
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

int loadBrightness(NvsStorageManager& nvs) {
    std::string v;
    if (!nvs.retrieve("bright", v)) return kLevels - 2;
    int level = std::atoi(v.c_str());
    return (level >= 0 && level < kLevels) ? level : kLevels - 2;
}

}  // namespace

extern "C" void app_main(void) {
    LogBuffer::install();
    initNvs();
    static NvsStorageManager nvs;
    static Settings settings(nvs);
    settings.log();
    applyTz(settings.tz);
    s_use24h = settings.use24h != 0;
    s_latitude = std::strtod(settings.latitude.c_str(), nullptr);
    s_longitude = std::strtod(settings.longitude.c_str(), nullptr);
    settings.onChange("tz", [] {
        applyTz(settings.tz);
        s_sunsetDirty = true;
    });
    settings.onChange("use_24h", [] { s_use24h = settings.use24h != 0; });
    settings.onChange("latitude", [] {
        s_latitude = std::strtod(settings.latitude.c_str(), nullptr);
        s_sunsetDirty = true;
    });
    settings.onChange("longitude", [] {
        s_longitude = std::strtod(settings.longitude.c_str(), nullptr);
        s_sunsetDirty = true;
    });
    static Display display;
    display.init();

    int level = loadBrightness(nvs);
    ESP_LOGI(TAG, "brightness level %d from NVS", level);
    display.setBrightness(kBrightness[level]);

    FaceState face;
    face.use24h = s_use24h;
    face.message = "STARTING";
    display.render([&](Band& b) { drawClockFace(b, face); });

    static WiFiManager wifi(nvs, onIpEvent, nullptr);
    // Build-time hostname only seeds a fresh device; after that the page owns it.
    std::string host;
    if (!nvs.retrieve("hostname", host)) {
        host = CONFIG_CLOCK_HOSTNAME;
        wifi.configSetHostName(host);
    }
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, onWifiEvent, nullptr));

    static WebContext webCtx(&wifi);
    static TouchKey key;
    static WebApi web(&webCtx, settings, key);
    if (web.start() != ESP_OK) ESP_LOGE(TAG, "web server failed to start, OTA unavailable");

    wifi_config_t sta = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &sta) == ESP_OK && sta.sta.ssid[0] == '\0') {
        s_net = NetState::Unprovisioned;
    }

    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp_cfg));

    static const std::string mqttUri = settings.mqttUri();
    static esp_mqtt_client_config_t mqttCfg = {};
    mqttCfg.broker.address.uri = mqttUri.c_str();
    static MqttClient mqtt(mqttCfg, CONFIG_CLOCK_HOSTNAME);
    Temperature::attach(mqtt, settings.insideTopic, settings.outsideTopic);  // queued; replayed on connect
    bool mqttStarted = false;

    key.init();

    FaceState shown;
    bool wasPressed = false;
    TickType_t pressStart = 0;
    bool otaPending = otaPendingVerify();
    TickType_t lastOnline = xTaskGetTickCount();
    int sunsetDay = -1;

    while (true) {
        const TickType_t tick = xTaskGetTickCount();
        const bool pressed = key.pressed();
        // Step on press, not release, so the screen reacts under the finger.
        if (pressed && !wasPressed) {
            pressStart = tick;
            level = (level + kLevels - 1) % kLevels;
            display.setBrightness(kBrightness[level]);
            nvs.store("bright", std::to_string(level));
            ESP_LOGI(TAG, "brightness level %d", level);
        }
        const TickType_t held = pressed ? tick - pressStart : 0;
        wasPressed = pressed;

        if (!mqttStarted && s_net == NetState::Online) {
            mqtt.start();
            mqttStarted = true;
        }

        if (s_net == NetState::Online) {
            lastOnline = tick;
        } else if (s_net == NetState::Connecting && tick - lastOnline > kOfflineRestart) {
            ESP_LOGE(TAG, "offline for 3 min, restarting");
            esp_restart();
        }

        if (otaPending && s_net == NetState::Online) {
            ESP_LOGI(TAG, "OTA image verified (got IP), cancelling rollback");
            esp_ota_mark_app_valid_cancel_rollback();
            otaPending = false;
        } else if (otaPending && tick > kOtaVerifyTimeout) {
            ESP_LOGE(TAG, "no IP within 120 s of OTA boot, rolling back");
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }

        time_t now = time(nullptr);
        localtime_r(&now, &face.now);
        // RTC keeps time across soft resets, so a plausible year means synced.
        face.timeValid = face.now.tm_year + 1900 >= 2025;
        face.net = s_net;
        face.use24h = s_use24h;
        if (face.timeValid && (face.now.tm_yday != sunsetDay || s_sunsetDirty.exchange(false))) {
            time_t set;
            face.hasSunset = sunsetFor(now, s_latitude, s_longitude, set);
            if (face.hasSunset) localtime_r(&set, &face.sunset);
            sunsetDay = face.now.tm_yday;
            ESP_LOGI(TAG, "sunset %02d:%02d", face.sunset.tm_hour, face.sunset.tm_min);
        }
        face.hasInside = Temperature::latest(Temperature::Probe::Inside, face.insideTenths, face.insideTrend);
        face.hasOutside =
            Temperature::latest(Temperature::Probe::Outside, face.outsideTenths, face.outsideTrend);

        if (pressed && held >= kResetHold && pressStart < kResetWindow) {
            face.message = "WIFI RESET";
            display.render([&](Band& b) { drawClockFace(b, face); });
            ESP_LOGW(TAG, "touch held 10 s: clearing Wi-Fi credentials");
            wifi.clear();  // reboots
        } else if (face.net == NetState::Unprovisioned) {
            face.message = "USE ESPTOUCH V2";
        } else if (!face.timeValid) {
            face.message = face.net == NetState::Online ? "NTP SYNC..." : "WIFI...";
        } else {
            face.message.clear();
        }

        if (!(face == shown)) {
            display.render([&](Band& b) { drawClockFace(b, face); });
            shown = face;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
