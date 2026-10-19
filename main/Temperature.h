#pragma once

#include "MqttClient.h"
#include "Trend.h"

// Readings from Node-RED's retained topics:
// - Grid: {"power":-786,...} in W, negative when exporting, every 60 s (power metering flow).
// - Outside: {"temperature":22.6,"unit":"C","timestamp":"2026-10-13T03:31:30.123Z"}.
namespace Temperature {
enum class Probe { Grid, Outside };

// Empty topics are skipped.
void attach(MqttClient& mqtt, const std::string& gridTopic, const std::string& outsideTopic);
// Latest reading in tenths (of a kW or a degree) and its 10-minute trend; false
// if none, or too old (grid 3 min, outside 30 min).
bool latest(Probe probe, int& tenths, Trend& trend);
}  // namespace Temperature
