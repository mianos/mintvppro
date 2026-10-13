#pragma once
#include "Settings.h"
#include "TouchKey.h"
#include "WebServer.h"

// mianesp's WebServer (GET /healthz, POST /reset, POST /set_hostname) plus
//   POST /firmware   OTA: curl --data-binary @build/mintvpro.bin http://mintvpro.mianos.com/firmware
//   GET  /log        recent ESP_LOG output; ?prev for the previous boot's tail
//   GET  /           settings page
//   GET/POST /config settings as JSON (plus "hostname"); POST applies any subset
//   POST /reboot     plain restart (mianesp's /reset also clears Wi-Fi)
class WebApi : public WebServer {
 public:
  WebApi(WebContext* ctx, Settings& settings, const TouchKey& key)
      : WebServer(ctx), settings_(settings), key_(key) {}

  esp_err_t start() override;

 protected:
  void populate_healthz_fields(WebContext* ctx, JsonWrapper& json) override;

 private:
  static esp_err_t firmware_post_handler(httpd_req_t* req);
  static esp_err_t log_get_handler(httpd_req_t* req);
  static esp_err_t page_get_handler(httpd_req_t* req);
  static esp_err_t config_get_handler(httpd_req_t* req);
  static esp_err_t config_post_handler(httpd_req_t* req);
  static esp_err_t reboot_post_handler(httpd_req_t* req);

  Settings& settings_;
  const TouchKey& key_;
};
