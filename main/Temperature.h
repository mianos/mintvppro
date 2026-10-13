#pragma once

#include "MqttClient.h"
#include "Trend.h"

// Inside/outside temperatures from Node-RED's retained topics, which carry
// {"temperature":22.6,"unit":"C","timestamp":"2026-10-13T03:31:30.123Z"}.
namespace Temperature {
enum class Probe { Inside, Outside };

// Empty topics are skipped.
void attach(MqttClient& mqtt, const std::string& insideTopic, const std::string& outsideTopic);
// Latest reading in tenths of a degree and its 10-minute trend; false if none,
// or sampled over 30 min ago.
bool latest(Probe probe, int& tenths, Trend& trend);
}  // namespace Temperature
