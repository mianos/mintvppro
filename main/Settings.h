#pragma once
#include <string>

#include "SettingsBase.h"

// Runtime settings, persisted as one JSON blob in NVS (mianesp settingsbase) and
// edited via GET/POST /config or the page at /. Member initialisers are the
// defaults for a fresh device. SettingsBase supports only std::string and int,
// so booleans are 0/1 ints and coordinates are text.
struct Settings : SettingsBase {
    std::string tz = "AEST-10AEDT,M10.1.0,M4.1.0/3";  // POSIX TZ, Sydney
    int use24h = 0;
    std::string latitude = "-33.8688";
    std::string longitude = "151.2093";

    // MQTT subscriptions are made once at startup, so changing these reboots.
    std::string mqttServer = "mqtt2.mianos.com";
    int mqttPort = 1883;
    std::string insideTopic = "home/temperature/office";
    std::string outsideTopic = "home/temperature/outside";

    explicit Settings(NvsStorageManager& nvs) : SettingsBase(nvs) {
        field("tz", tz);
        field("use_24h", use24h);
        field("latitude", latitude);
        field("longitude", longitude);
        field("mqtt_server", mqttServer);
        field("mqtt_port", mqttPort);
        field("inside_topic", insideTopic);
        field("outside_topic", outsideTopic);
        load();  // must follow every field() registration
    }

    std::string mqttUri() const { return "mqtt://" + mqttServer + ":" + std::to_string(mqttPort); }
};
