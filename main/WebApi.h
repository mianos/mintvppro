#pragma once
#include <cstdint>

#include "esp_attr.h"
#include "Settings.h"
#include "TouchKey.h"
#include "WifiWebServer.h"

// mianesp's WebServer (GET /healthz, POST /reset, POST /set_hostname) plus
//   POST /firmware   OTA: curl --data-binary @build/mintvpro.bin http://nmtv-clock.mianos.com/firmware
//   GET  /log        recent ESP_LOG output; ?prev for the previous boot's tail
//   GET  /           settings page
//   GET/POST /config settings as JSON (plus "hostname"); POST applies any subset
//   POST /reboot     plain restart (mianesp's /reset also clears Wi-Fi)
//   GET/POST /repartition  current partition layout; install a new table
// Where the last repartition attempt got to, kept across the reset that follows a
// fault; app_main logs it and clears it. Diagnostic only.
extern RTC_NOINIT_ATTR uint32_t g_repartition_crumb;
extern RTC_NOINIT_ATTR uint32_t g_repartition_magic;

class WebApi : public WifiWebServer {
 public:
  WebApi(WifiWebContext* ctx, Settings& settings, const TouchKey& key)
      : WifiWebServer(ctx), settings_(settings), key_(key) {}

 protected:
  void register_routes() override;
  void populate_healthz_fields(WebContext* ctx, JsonWrapper& json) override;

 private:
  static esp_err_t firmware_post_handler(httpd_req_t* req);
  static esp_err_t log_get_handler(httpd_req_t* req);
  static esp_err_t page_get_handler(httpd_req_t* req);
  static esp_err_t config_get_handler(httpd_req_t* req);
  static esp_err_t config_post_handler(httpd_req_t* req);
  static esp_err_t reboot_post_handler(httpd_req_t* req);
  static esp_err_t repartition_get_handler(httpd_req_t* req);
  static esp_err_t repartition_post_handler(httpd_req_t* req);

  Settings& settings_;
  const TouchKey& key_;
};
