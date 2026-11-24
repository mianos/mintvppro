#include "Temperature.h"

#include <atomic>
#include <cmath>
#include <ctime>
#include <regex>
#include <string>
#include <string_view>

#include "Text.h"
#include "esp_log.h"

namespace {

const char* TAG = "temp";
constexpr time_t kTrendWindowS = 10 * 60;

struct Reading {
    const char* name;
    const char* field;
    double toTenths;    // payload units -> displayed tenths
    time_t staleS;
    int trendTenths;    // change that counts as rising or falling
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
        r.trend = d >= r.trendTenths ? Trend::Rising : d <= -r.trendTenths ? Trend::Falling : Trend::Steady;
    }
    r.refTenths = tenths;
    r.refAt = at;
}

// Grid power swings by a few hundred W as appliances cycle, so its arrow wants a
// bigger change than the 0.1 degree the outside arrow uses.
// The meter reports import as positive; the clock shows export as positive, like the
// e-ink weather display, so a minus sign means paying for power.
Reading s_readings[2] = {{"grid", "power", -0.01, 3 * 60, 3}, {"outside", "temperature", 10, 30 * 60, 1}};

bool clockValid(time_t t) {
    struct tm tm;
    localtime_r(&t, &tm);
    return tm.tm_year + 1900 >= 2025;
}

// Reads exactly `count` digits off the front of `in`.
bool digits(std::string_view& in, int& out, size_t count) {
    if (in.size() < count) return false;
    const auto [end, err] = std::from_chars(in.data(), in.data() + count, out);
    if (err != std::errc()) return false;
    in.remove_prefix(count);
    return true;
}

// "2026-10-13T03:31:30.123Z" (UTC) -> epoch; 0 if unparseable. The separators are
// checked, so a truncated or rearranged stamp does not become a plausible date.
time_t parseIsoUtc(std::string_view s) {
    int y, mo, d, h, mi, sec;
    if (!digits(s, y, 4) || !s.starts_with('-')) return 0;
    s.remove_prefix(1);
    if (!digits(s, mo, 2) || !s.starts_with('-')) return 0;
    s.remove_prefix(1);
    if (!digits(s, d, 2) || !s.starts_with('T')) return 0;
    s.remove_prefix(1);
    if (!digits(s, h, 2) || !s.starts_with(':')) return 0;
    s.remove_prefix(1);
    if (!digits(s, mi, 2) || !s.starts_with(':')) return 0;
    s.remove_prefix(1);
    if (!digits(s, sec, 2)) return 0;
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
    double v = 0;
    if (!json.GetField(r->field, v)) return ESP_OK;
    std::string stamp;
    time_t at = json.GetField("timestamp", stamp) ? parseIsoUtc(stamp) : 0;
    // Retained messages arrive on every connect; only the timestamp says how old they
    // are. The meter's has none, but it reports every 60 s, so arrival time is close.
    if (at == 0) at = time(nullptr);
    const int tenths = static_cast<int>(std::lround(v * r->toTenths));
    // Only a change to the displayed tenth is worth a log line; sensors can report
    // every few seconds with smaller wobble.
    const bool shownChanged = r->sampledAt == 0 || tenths != r->tenths;
    updateTrend(*r, tenths, at);
    r->tenths = tenths;
    r->sampledAt = at;
    if (shownChanged) ESP_LOGI(TAG, "%s %.1f (%s)", r->name, tenths / 10.0, stamp.c_str());
    return ESP_OK;
}

void subscribe(MqttClient& mqtt, const std::string& topic, Reading& r) {
    if (topic.empty()) return;
    mqtt.subscribe(topic);
    mqtt.registerHandler(topic, std::regex(topic), onMessage, &r);
}

}  // namespace

void Temperature::attach(MqttClient& mqtt, const std::string& gridTopic, const std::string& outsideTopic) {
    subscribe(mqtt, gridTopic, s_readings[0]);
    subscribe(mqtt, outsideTopic, s_readings[1]);
}

bool Temperature::latest(Probe probe, int& tenths, Trend& trend) {
    const Reading& r = s_readings[probe == Probe::Grid ? 0 : 1];
    const time_t at = r.sampledAt;
    if (at == 0) return false;
    const time_t now = time(nullptr);
    if (clockValid(now) && now - at > r.staleS) return false;
    tenths = r.tenths;
    trend = r.trend;
    return true;
}
