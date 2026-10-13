#pragma once

#include <ctime>
#include <string>

#include "Display.h"
#include "Trend.h"

enum class NetState { Unprovisioned, Connecting, Online };

struct FaceState {
    struct tm now = {};
    bool timeValid = false;
    bool use24h = true;
    NetState net = NetState::Connecting;
    bool hasInside = false;
    int insideTenths = 0;
    Trend insideTrend = Trend::Unknown;
    bool hasOutside = false;
    int outsideTenths = 0;
    Trend outsideTrend = Trend::Unknown;
    bool hasSunset = false;
    struct tm sunset = {};
    // Replaces the date line when non-empty.
    std::string message;

    bool operator==(const FaceState& o) const {
        return now.tm_sec == o.now.tm_sec && now.tm_min == o.now.tm_min && now.tm_hour == o.now.tm_hour &&
               now.tm_mday == o.now.tm_mday && timeValid == o.timeValid && use24h == o.use24h &&
               net == o.net && message == o.message && hasInside == o.hasInside &&
               insideTenths == o.insideTenths && hasOutside == o.hasOutside &&
               outsideTenths == o.outsideTenths && insideTrend == o.insideTrend &&
               outsideTrend == o.outsideTrend && hasSunset == o.hasSunset &&
               sunset.tm_hour == o.sunset.tm_hour && sunset.tm_min == o.sunset.tm_min;
    }
};

void drawClockFace(Band& b, const FaceState& s);
