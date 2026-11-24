#include "NetWatch.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

namespace {

const char* TAG = "net";

// 30 s is often enough to catch an outage before the clock looks stale, and slow
// enough that the probe is invisible next to MQTT and SNTP.
constexpr uint32_t kIntervalMs = 30000;
constexpr int kConnectTimeoutMs = 3000;
constexpr int kFailsBeforeDown = 2;
constexpr uint16_t kDefaultPort = 53;  // DNS answers on TCP as well as UDP
constexpr UBaseType_t kTaskStack = 5120;

struct Target {
    std::string host;
    uint16_t port;
};

std::atomic<bool> s_up{false};
std::atomic<bool> s_known{false};
std::atomic<int> s_rttMs{0};
std::atomic<uint32_t> s_sinceOkMs{0};  // millis, against esp_timer; 0 before start()
std::atomic<int> s_fails{0};

SemaphoreHandle_t s_lock = nullptr;
std::vector<Target> s_targets;
TaskHandle_t s_task = nullptr;

uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

bool blank(char c) { return c == ' ' || c == '\t'; }

// "1.1.1.1:53,ct.mianos.com:443" -> targets. A bare host takes the DNS port. The
// last good list is kept if the new one parses to nothing, so a typo through the
// settings page cannot silently stop the check.
std::vector<Target> parse(const std::string& text) {
    std::vector<Target> out;
    size_t at = 0;
    while (at <= text.size()) {
        const size_t end = text.find(',', at);
        std::string item = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? text.size() + 1 : end + 1;
        while (!item.empty() && blank(item.back())) item.pop_back();
        while (!item.empty() && blank(item.front())) item.erase(item.begin());
        if (item.empty()) continue;
        Target t{"", kDefaultPort};
        const size_t colon = item.rfind(':');
        if (colon != std::string::npos && colon + 1 < item.size()) {
            const int port = std::atoi(item.c_str() + colon + 1);
            if (port > 0 && port < 65536) {
                t.port = static_cast<uint16_t>(port);
                item.resize(colon);
            }
        }
        if (!item.empty()) t.host = item;
        if (!t.host.empty()) out.push_back(t);
    }
    return out;
}

// A connect that either completes, is refused, or gives up after the timeout.
// Only a completed handshake counts: a timeout and a refusal both mean the path
// out is not working, which is the thing being measured.
bool probeOne(const Target& t, int& rttMs) {
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    const std::string port = std::to_string(t.port);
    if (getaddrinfo(t.host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
        ESP_LOGD(TAG, "%s: no address", t.host.c_str());
        return false;
    }

    bool ok = false;
    const int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd >= 0) {
        const int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        const int64_t started = esp_timer_get_time();
        if (connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
            ok = true;
        } else if (errno == EINPROGRESS) {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(fd, &writable);
            struct timeval timeout = {kConnectTimeoutMs / 1000, (kConnectTimeoutMs % 1000) * 1000};
            if (select(fd + 1, nullptr, &writable, nullptr, &timeout) > 0) {
                int err = 0;
                socklen_t len = sizeof err;
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) ok = true;
            }
        }
        if (ok) rttMs = static_cast<int>((esp_timer_get_time() - started) / 1000);
        close(fd);
    }
    freeaddrinfo(res);
    return ok;
}

bool probeCycle() {
    std::vector<Target> targets;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        targets = s_targets;
        xSemaphoreGive(s_lock);
    }

    bool ok = false;
    int rtt = 0;
    for (const Target& t : targets) {
        if (probeOne(t, rtt)) {
            ok = true;
            break;
        }
    }
    s_known = true;

    if (ok) {
        s_fails = 0;
        s_rttMs = rtt;
        s_sinceOkMs = nowMs();
        s_up = true;
        return true;
    }
    if (s_sinceOkMs.load() == 0) s_sinceOkMs = nowMs();
    if (++s_fails >= kFailsBeforeDown) s_up = false;
    return s_up;
}

void task(void*) {
    bool wasUp = false;  // a failed first probe is the normal boot state; log the recovery
    for (;;) {
        const bool up = probeCycle();
        if (up != wasUp) {
            if (up) {
                ESP_LOGI(TAG, "internet reachable, %d ms", s_rttMs.load());
            } else {
                ESP_LOGW(TAG, "internet unreachable");
            }
            wasUp = up;
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kIntervalMs));
    }
}

}  // namespace

namespace NetWatch {

void start(const char* targets) {
    if (s_task) return;
    s_lock = xSemaphoreCreateMutex();
    s_sinceOkMs = nowMs();
    setTargets(targets);
    xTaskCreate(task, "netwatch", kTaskStack, nullptr, 2, &s_task);
}

void setTargets(const char* targets) {
    if (!s_lock || !targets) return;
    std::vector<Target> parsed = parse(targets);
    if (parsed.empty()) return;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_targets = std::move(parsed);
        xSemaphoreGive(s_lock);
    }
    nudge();
}

bool up() { return s_up; }
bool known() { return s_known; }
int lastRttMs() { return s_rttMs; }

int downForS() {
    const uint32_t since = s_sinceOkMs;
    if (since == 0) return 0;
    return static_cast<int>((nowMs() - since) / 1000);
}

void linkLost() {
    s_fails = kFailsBeforeDown;  // down at once, without waiting for a cycle
    s_up = false;
    nudge();
}

void nudge() {
    if (s_task) xTaskNotifyGive(s_task);
}

}  // namespace NetWatch
