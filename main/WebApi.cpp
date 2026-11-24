#include "WebApi.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "LogBuffer.h"
#include "NetWatch.h"
#include "Text.h"
#include "WebPage.h"
#include "esp_flash.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

namespace {

constexpr char TAG[] = "webapi";
constexpr int kMaxConfigBody = 4 * 1024;
// The partition table is one flash sector at 0x8000: 96 entries of 32 bytes, of
// which the generator writes 0xff terminators past the MD5. The bootloader finds
// the end entry, so the whole 3 KB is what gets written.
constexpr uint32_t kTableAddress = 0x8000;
constexpr size_t kTableSector = 0x1000;
constexpr size_t kTableBytes = 0xC00;

std::string currentHostname() {
  const char* name = nullptr;
  esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (sta) esp_netif_get_hostname(sta, &name);
  return name ? name : "";
}

bool validHostname(std::string_view h) {
  if (h.empty() || h.size() > 32 || h.front() == '-' || h.back() == '-') return false;
  return std::ranges::all_of(h, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-'; });
}

std::string_view resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_SW: return "sw";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    default: return "other";
  }
}

void restartSoon() {
  vTaskDelay(pdMS_TO_TICKS(500));  // let the response flush
  esp_restart();
}

}  // namespace

void WebApi::register_routes() {
  WifiWebServer::register_routes();

  const httpd_uri_t routes[] = {
      {.uri = "/firmware", .method = HTTP_POST, .handler = firmware_post_handler, .user_ctx = this},
      {.uri = "/log", .method = HTTP_GET, .handler = log_get_handler, .user_ctx = this},
      {.uri = "/", .method = HTTP_GET, .handler = page_get_handler, .user_ctx = this},
      {.uri = "/config", .method = HTTP_GET, .handler = config_get_handler, .user_ctx = this},
      {.uri = "/config", .method = HTTP_POST, .handler = config_post_handler, .user_ctx = this},
      {.uri = "/reboot", .method = HTTP_POST, .handler = reboot_post_handler, .user_ctx = this},
      {.uri = "/repartition", .method = HTTP_GET, .handler = repartition_get_handler, .user_ctx = this},
      {.uri = "/repartition", .method = HTTP_POST, .handler = repartition_post_handler, .user_ctx = this},
  };
  // start() has raised the table to 32 slots, and a failure here is logged rather
  // than fatal: a missing endpoint should not cost the ones after it.
  for (const httpd_uri_t& uri : routes) {
    const esp_err_t r = httpd_register_uri_handler(server, &uri);
    if (r != ESP_OK) ESP_LOGE(TAG, "register %s: %s", uri.uri, esp_err_to_name(r));
  }
}

void WebApi::populate_healthz_fields(WebContext*, JsonWrapper& json) {
  json.AddItem("touch_value", static_cast<int>(key_.value()));
  json.AddItem("touch_baseline", static_cast<int>(key_.baseline()));
  const esp_app_desc_t* desc = esp_app_get_description();
  json.AddItem("version", std::string(desc->version));
  json.AddItem("partition", std::string(esp_ota_get_running_partition()->label));
  json.AddItem("uptime_s", static_cast<int>(esp_timer_get_time() / 1000000));
  json.AddItem("heap_free", static_cast<int>(esp_get_free_heap_size()));
  json.AddItem("reset_reason", std::string(resetReasonName(esp_reset_reason())));
  json.AddItem("internet", NetWatch::up());
  json.AddItem("internet_rtt_ms", NetWatch::lastRttMs());
  json.AddItem("internet_down_s", NetWatch::downForS());
  // Lowest free stack ever seen on the clock loop's task, in bytes.
  if (TaskHandle_t main = xTaskGetHandle("main")) {
    json.AddItem("main_stack_min_free", static_cast<int>(uxTaskGetStackHighWaterMark(main)));
  }
  // The probe calls getaddrinfo, so its margin is worth watching too.
  if (TaskHandle_t net = xTaskGetHandle("netwatch")) {
    json.AddItem("net_stack_min_free", static_cast<int>(uxTaskGetStackHighWaterMark(net)));
  }
}

// Raw .bin body streamed into the inactive OTA slot, which becomes the next boot
// partition, then reboot.
// The layout as flash holds it, one line per partition. Used to confirm a
// repartition took, and to see where the images are.
esp_err_t WebApi::repartition_get_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/plain");
  std::string out;
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  for (; it; it = esp_partition_next(it)) {
    const esp_partition_t* p = esp_partition_get(it);
    out += std::string(std::string_view(p->label)) + " " + text::hex(p->type) + "/" + text::hex(p->subtype) +
           " at " + text::hex(p->address) + " size " + text::hex(p->size) + "\n";
  }
  esp_partition_iterator_release(it);
  return httpd_resp_send(req, out.data(), out.size());
}

// Replaces the partition table with a .bin built by gen_esp32part.py. No cable:
// nvs, otadata and the ota_0 offset stay put, so an interrupted write still
// boots the image that is running. The guards are that the table verifies, and
// that its ota_0 is where this image is actually running from — a table whose
// ota_0 moved would restart into nothing.
esp_err_t WebApi::repartition_post_handler(httpd_req_t* req) {
  const size_t len = req->content_len;
  if (len != kTableBytes) {
    return sendJsonError(req, 400, "body must be the 3072 byte partition-table.bin");
  }
  // On the heap: the buffers together are 10 KB and the httpd task stack is 4 KB.
  std::vector<uint8_t> table(kTableBytes);
  for (int got = 0; got < static_cast<int>(len);) {
    const int n = httpd_req_recv(req, reinterpret_cast<char*>(table.data()) + got, len - got);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) return sendJsonError(req, 400, "body truncated");
    got += n;
  }

  int count = 0;
  if (esp_partition_table_verify(reinterpret_cast<const esp_partition_info_t*>(table.data()), true, &count) != ESP_OK) {
    return sendJsonError(req, 400, "table does not verify: bad magic or MD5");
  }
  const auto* entries = reinterpret_cast<const esp_partition_info_t*>(table.data());
  const esp_partition_info_t* newOta0 = nullptr;
  for (int i = 0; i < count; ++i) {
    if (entries[i].type == ESP_PARTITION_TYPE_APP && entries[i].subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) newOta0 = &entries[i];
  }
  if (!newOta0) return sendJsonError(req, 400, "table has no ota_0");
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (newOta0->pos.offset != running->address) {
    return sendJsonError(req, 409, "ota_0 moves to " + text::hex(newOta0->pos.offset) + " but this image runs from " +
                                       text::hex(running->address));
  }

  std::vector<uint8_t> current(kTableSector);
  esp_flash_read(esp_flash_default_chip, current.data(), kTableAddress, current.size());
  if (std::memcmp(current.data(), table.data(), table.size()) == 0) {
    JsonWrapper resp;
    resp.AddItem("status", std::string("already installed"));
    return sendJson(req, resp);
  }

  // Point the boot flag at the running slot first: if anything below fails, the
  // device restarts into the same image under whichever table is left.
  const esp_partition_t* ota0 =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  if (!ota0) return sendJsonError(req, 500, "no ota_0 partition to boot");
  esp_err_t err = esp_ota_set_boot_partition(ota0);
  if (err != ESP_OK) return sendJsonError(req, 500, esp_err_to_name(err));

  ESP_LOGW(TAG, "partition table from %s: %d partitions, ota_0 at %s", callerAddress(req).c_str(), count,
           text::hex(newOta0->pos.offset).c_str());

  err = esp_flash_erase_region(esp_flash_default_chip, kTableAddress, kTableSector);
  if (err == ESP_OK) err = esp_flash_write(esp_flash_default_chip, table.data(), kTableAddress, table.size());

  std::vector<uint8_t> check(kTableBytes);
  if (err == ESP_OK) err = esp_flash_read(esp_flash_default_chip, check.data(), kTableAddress, check.size());
  if (err == ESP_OK && std::memcmp(check.data(), table.data(), table.size()) != 0) err = ESP_ERR_INVALID_CRC;
  if (err != ESP_OK) {
    // Put the previous table back; this still runs, so the retry needs no cable.
    esp_flash_erase_region(esp_flash_default_chip, kTableAddress, kTableSector);
    esp_flash_write(esp_flash_default_chip, current.data(), kTableAddress, table.size());
    ESP_LOGE(TAG, "table write failed (%s), previous table restored", esp_err_to_name(err));
    return sendJsonError(req, 500, std::string("table write failed and was rolled back: ") + esp_err_to_name(err));
  }

  JsonWrapper resp;
  resp.AddItem("status", std::string("ok, rebooting into the new layout"));
  resp.AddItem("partitions", count);
  const esp_err_t r = sendJson(req, resp);
  vTaskDelay(pdMS_TO_TICKS(500));  // let the response flush
  esp_restart();
  return r;
}

esp_err_t WebApi::firmware_post_handler(httpd_req_t* req) {
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  if (!target) return sendJsonError(req, 500, "no OTA partition available");
  if (req->content_len <= 0) return sendJsonError(req, 400, "empty body (POST the .bin)");
  if (static_cast<size_t>(req->content_len) > target->size)
    return sendJsonError(req, 413, "image larger than the OTA partition");

  ESP_LOGW(TAG, "OTA: %d bytes -> %s from %s", req->content_len, target->label,
           callerAddress(req).c_str());

  esp_ota_handle_t handle = 0;
  esp_err_t err = esp_ota_begin(target, req->content_len, &handle);
  if (err != ESP_OK) return sendJsonError(req, 500, esp_err_to_name(err));

  std::array<char, 1024> buf{};
  int remaining = req->content_len;
  // recv() returns a timeout rather than blocking, so a client that dies mid-body
  // would spin here for ever; give up once it stops making progress.
  TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(90000);
  while (remaining > 0) {
    const int got = httpd_req_recv(req, buf.data(), std::min<int>(remaining, buf.size()));
    if (got == HTTPD_SOCK_ERR_TIMEOUT) {
      if (xTaskGetTickCount() > deadline) {
        esp_ota_abort(handle);
        return sendJsonError(req, 408, "upload stalled");
      }
      continue;
    }
    if (got <= 0) {
      esp_ota_abort(handle);
      return sendJsonError(req, 400, "upload aborted");
    }
    deadline = xTaskGetTickCount() + pdMS_TO_TICKS(90000);
    err = esp_ota_write(handle, buf.data(), got);
    if (err != ESP_OK) {
      esp_ota_abort(handle);
      return sendJsonError(req, 500, esp_err_to_name(err));
    }
    remaining -= got;
  }

  err = esp_ota_end(handle);
  if (err != ESP_OK) return sendJsonError(req, 500, esp_err_to_name(err));
  err = esp_ota_set_boot_partition(target);
  if (err != ESP_OK) return sendJsonError(req, 500, esp_err_to_name(err));

  JsonWrapper resp;
  resp.AddItem("status", std::string("ok, rebooting"));
  resp.AddItem("partition", std::string(target->label));
  const esp_err_t r = sendJson(req, resp);
  ESP_LOGW(TAG, "OTA complete, rebooting into %s", target->label);
  vTaskDelay(pdMS_TO_TICKS(500));  // let the response flush
  esp_restart();
  return r;
}

esp_err_t WebApi::log_get_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/plain");
  std::array<char, 16> query{};
  httpd_req_get_url_query_str(req, query.data(), query.size());
  const std::string log =
      std::string_view(query.data()).starts_with("prev") ? LogBuffer::previousBoot() : LogBuffer::snapshot();
  return httpd_resp_send(req, log.data(), log.size());
}

esp_err_t WebApi::page_get_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kSettingsPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebApi::config_get_handler(httpd_req_t* req) {
  auto* self = static_cast<WebApi*>(req->user_ctx);
  // The page reads this on every load; a cached copy would show stale settings.
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  JsonWrapper resp = self->settings_.toJson();
  resp.AddItem("hostname", currentHostname());
  return sendJson(req, resp);
}

// Applies any subset of the settings keys and persists. Time and location take
// effect via onChange hooks in main.cpp; an MQTT or hostname change restarts the
// clock (the hostname is only sent to DHCP when the lease is taken).
esp_err_t WebApi::config_post_handler(httpd_req_t* req) {
  auto* self = static_cast<WebApi*>(req->user_ctx);
  Settings& settings = self->settings_;
  JsonWrapper json;
  if (!readJsonBody(req, json, kMaxConfigBody)) return ESP_FAIL;

  std::string hostname;
  const bool hostnameChanged = json.GetField("hostname", hostname) && hostname != currentHostname();
  if (hostnameChanged && !validHostname(hostname)) {
    return sendJsonError(req, 400, "hostname: 1-32 letters, digits or '-', not starting/ending with '-'");
  }
  if (hostnameChanged) static_cast<WifiWebContext*>(self->webContext)->wifiManager->configSetHostName(hostname);

  ESP_LOGI(TAG, "POST /config from %s", callerAddress(req).c_str());
  const std::string mqttBefore = settings.mqttUri() + settings.gridTopic + "|" + settings.outsideTopic;
  settings.loadFromJson(json);
  settings.save();
  settings.log();
  const bool reboot =
      hostnameChanged || settings.mqttUri() + settings.gridTopic + "|" + settings.outsideTopic != mqttBefore;

  JsonWrapper resp = settings.toJson();
  resp.AddItem("hostname", hostnameChanged ? hostname : currentHostname());
  resp.AddItem("rebooting", reboot);
  const esp_err_t r = sendJson(req, resp);
  if (reboot) {
    ESP_LOGW(TAG, "MQTT or hostname changed, restarting");
    restartSoon();
  }
  return r;
}

esp_err_t WebApi::reboot_post_handler(httpd_req_t* req) {
  JsonWrapper resp;
  resp.AddItem("status", std::string("rebooting"));
  const esp_err_t r = sendJson(req, resp);
  ESP_LOGW(TAG, "restart requested over HTTP from %s", callerAddress(req).c_str());
  restartSoon();
  return r;
}
