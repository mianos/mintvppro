#include "Temperature.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <regex>
#include <string>

#include "esp_log.h"

namespace {

const char* TAG = "temp";
constexpr time_t kStaleS = 30 * 60;
constexpr time_t kTrendWindowS = 10 * 60;
constexpr int kTrendTenths = 1;  // the smallest change the display shows

struct Reading {
    const char* name;
    std::atomic<int> tenths{0};
    std::atomic<time_t> sampledAt{0};  // 0 = never seen
    std::atomic<Trend> trend{Trend::Unknown};
    // Touched only from the MQTT task.
    int refTenths = 0;
    time_t refAt = 0;
};

// Compares against the reading from at least one window ago, then moves the
// reference forward, so the arrow reflects roughly the last 10 minutes.
void updateTrend(Reading& r, int tenths, time_t at) {
    if (r.refAt == 0 || at < r.refAt) {
        r.refTenths = tenths;
        r.refAt = at;
        return;
    }
    const time_t age = at - r.refAt;
    if (age < kTrendWindowS) return;
    if (age > 3 * kTrendWindowS) {
        r.trend = Trend::Unknown;  // the sensor was away; the difference isn't a trend
    } else {
        const int d = tenths - r.refTenths;
        r.trend = d >= kTrendTenths ? Trend::Rising : d <= -kTrendTenths ? Trend::Falling : Trend::Steady;
    }
    r.refTenths = tenths;
    r.refAt = at;
}

Reading s_readings[2] = {{"inside"}, {"outside"}};

bool clockValid(time_t t) {
    struct tm tm;
    localtime_r(&t, &tm);
    return tm.tm_year + 1900 >= 2025;
}

// "2026-10-13T03:31:30.123Z" (UTC) -> epoch; 0 if unparseable.
time_t parseIsoUtc(const std::string& s) {
    int y, mo, d, h, mi, sec;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6) return 0;
    // Days from civil (Howard Hinnant), since newlib has no timegm.
    y -= mo <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long days = long(era) * 146097 + doe - 719468;
    return time_t(days) * 86400 + h * 3600 + mi * 60 + sec;
}

esp_err_t onMessage(MqttClient*, const std::string& topic, const JsonWrapper& json, void* ctx) {
    auto* r = static_cast<Reading*>(ctx);
    double t = 0;
    if (!json.GetField("temperature", t)) return ESP_OK;
    std::string stamp;
    time_t at = json.GetField("timestamp", stamp) ? parseIsoUtc(stamp) : 0;
    // Retained messages arrive on every connect; only the timestamp says how old they are.
    if (at == 0) at = time(nullptr);
    const int tenths = static_cast<int>(std::lround(t * 10));
    updateTrend(*r, tenths, at);
    r->tenths = tenths;
    r->sampledAt = at;
    ESP_LOGI(TAG, "%s %.2f (%s)", r->name, t, stamp.c_str());
    return ESP_OK;
}

void subscribe(MqttClient& mqtt, const std::string& topic, Reading& r) {
    if (topic.empty()) return;
    mqtt.subscribe(topic);
    mqtt.registerHandler(topic, std::regex(topic), onMessage, &r);
}

}  // namespace

void Temperature::attach(MqttClient& mqtt, const std::string& insideTopic, const std::string& outsideTopic) {
    subscribe(mqtt, insideTopic, s_readings[0]);
    subscribe(mqtt, outsideTopic, s_readings[1]);
}

bool Temperature::latest(Probe probe, int& tenths, Trend& trend) {
    const Reading& r = s_readings[probe == Probe::Inside ? 0 : 1];
    const time_t at = r.sampledAt;
    if (at == 0) return false;
    const time_t now = time(nullptr);
    if (clockValid(now) && now - at > kStaleS) return false;
    tenths = r.tenths;
    trend = r.trend;
    return true;
}
