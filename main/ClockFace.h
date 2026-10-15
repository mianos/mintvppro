#pragma once

#include <ctime>
#include <string>

#include "Display.h"
#include "Trend.h"

enum class NetState { Unprovisioned, Connecting, Online };
// Values match the "face" setting; anything else shows the VFD.
enum class Face { Nixie = 1, Vfd = 2 };

struct FaceState {
    struct tm now = {};
    bool timeValid = false;
    NetState net = NetState::Connecting;
    bool hasInside = false;
    int insideTenths = 0;
    Trend insideTrend = Trend::Unknown;
    bool hasOutside = false;
    int outsideTenths = 0;
    Trend outsideTrend = Trend::Unknown;
    bool hasSunset = false;
    struct tm sunset = {};
    bool isDay = false;       // between sunrise and sunset
    bool sunsetSoon = false;  // within the hour before sunset
    bool synced = false;      // internet time received recently
    // Status while starting up; replaces the date when non-empty.
    std::string message;
    Face face = Face::Vfd;

    bool operator==(const FaceState& o) const {
        return now.tm_sec == o.now.tm_sec && now.tm_min == o.now.tm_min && now.tm_hour == o.now.tm_hour &&
               now.tm_mday == o.now.tm_mday && timeValid == o.timeValid &&
               face == o.face && net == o.net && message == o.message && hasInside == o.hasInside &&
               insideTenths == o.insideTenths && hasOutside == o.hasOutside &&
               outsideTenths == o.outsideTenths && insideTrend == o.insideTrend &&
               outsideTrend == o.outsideTrend && hasSunset == o.hasSunset &&
               sunset.tm_hour == o.sunset.tm_hour && sunset.tm_min == o.sunset.tm_min && isDay == o.isDay &&
               sunsetSoon == o.sunsetSoon && synced == o.synced;
    }
};

// The clock is 12-hour only: 0-23 to 1-12.
inline int hour12(int hour) { return hour % 12 == 0 ? 12 : hour % 12; }

void drawClockFace(Band& b, const FaceState& s);
void drawNixieFace(Band& b, const FaceState& s);
void drawVfdFace(Band& b, const FaceState& s);
