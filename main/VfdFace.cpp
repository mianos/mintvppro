#include <cstdlib>
#include <cstring>
#include <string>

#include "ClockFace.h"
#include "VfdArt.h"

extern const uint8_t kVfdBgStart[] asm("_binary_vfd_bg_bin_start");
extern const uint8_t kVfdGlowStart[] asm("_binary_vfd_glow_bin_start");

namespace {

using namespace vfd;

const char* const kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                               "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

void light(Band& b, const Cell& c, const uint8_t (*ramp)[3] = kRamp) {
    b.addGlow(c.x, c.y, c.w, c.h, kVfdGlowStart + c.offset, ramp);
}

// Characters the font lacks (and ' ') stay unlit.
void light(Band& b, const Font& f, int x, char ch, const uint8_t (*ramp)[3] = kRamp) {
    const char* p = ch ? std::strchr(f.chars, ch) : nullptr;
    if (!p) return;
    b.addGlow(x, f.y, f.w, f.h, kVfdGlowStart + f.offset + (p - f.chars) * f.w * f.h, ramp);
}

// Three small digits from kSmallX[first], with the decimal point: -9.9 to 99.9,
// otherwise dashes. Then the ▲ or ▼ for the 10-minute trend. All behind the amber
// filter, like the date.
void lightTemp(Band& b, int first, const Cell& dp, const Cell& up, const Cell& down, bool has, int tenths,
               Trend trend) {
    char d[3] = {'-', '-', '-'};
    if (has && tenths > -100 && tenths < 1000) {
        const int a = std::abs(tenths);
        d[0] = tenths < 0 ? '-' : (a >= 100 ? char('0' + a / 100) : ' ');
        d[1] = char('0' + a / 10 % 10);
        d[2] = char('0' + a % 10);
        light(b, dp, kFilterRamp);
    }
    for (int i = 0; i < 3; ++i) light(b, kSmall, kSmallX[first + i], d[i], kFilterRamp);
    if (has && trend == Trend::Rising) light(b, up, kFilterRamp);
    if (has && trend == Trend::Falling) light(b, down, kFilterRamp);
}

// The date row has six 14-segment characters, so main.cpp's status messages are
// shortened to a word.
const char* shortMessage(const std::string& m) {
    if (m.starts_with("USE ESPTOUCH")) return "SETUP";
    if (m.starts_with("WIFI RESET")) return "RESET";
    if (m.starts_with("WIFI")) return "WIFI";
    if (m.starts_with("NTP")) return "NTP";
    if (m.starts_with("STARTING")) return "START";
    return "";
}

}  // namespace

void drawVfdFace(Band& b, const FaceState& s) {
    b.blit(0, 0, Band::kWidth, Band::kWidth, kVfdBgStart);

    if (s.net == NetState::Online || (s.net == NetState::Connecting && s.now.tm_sec % 2 == 0)) light(b, kWifi);
    if (s.synced) light(b, kSync);
    if (s.hasSunset) {
        light(b, s.isDay ? kSun : kMoon, kFilterRamp);
        const int hour = hour12(s.sunset.tm_hour);  // always evening, so no PM
        const char set[4] = {hour >= 10 ? char('0' + hour / 10) : ' ', char('0' + hour % 10),
                             char('0' + s.sunset.tm_min / 10), char('0' + s.sunset.tm_min % 10)};
        for (int i = 0; i < 4; ++i) light(b, kTiny, kTinyX[i], set[i], kFilterRamp);
        // The colon blinks in the last hour before sunset.
        if (!s.sunsetSoon || s.now.tm_sec % 2 == 0) light(b, kTinyColon, kFilterRamp);
    }

    if (s.timeValid) {
        // 12-hour, as on a VCR: a half digit that only shows 1, and a tiny AM/PM.
        const int hour = hour12(s.now.tm_hour);
        if (hour >= 10) light(b, kHalf, kHalfX[0], '1');
        const char time[3] = {char('0' + hour % 10), char('0' + s.now.tm_min / 10), char('0' + s.now.tm_min % 10)};
        for (int i = 0; i < 3; ++i) light(b, kBig, kBigX[i], time[i]);
        light(b, s.now.tm_hour >= 12 ? kPm : kAm);
        if (s.now.tm_sec % 2 == 0) light(b, kColon);
    }

    lightTemp(b, 0, kDp0, kUpIn, kDownIn, s.hasInside, s.insideTenths, s.insideTrend);
    lightTemp(b, 3, kDp1, kUpOut, kDownOut, s.hasOutside, s.outsideTenths, s.outsideTrend);

    char row[8] = {};
    if (!s.message.empty()) {
        const char* m = shortMessage(s.message);
        snprintf(row, sizeof(row), "%*s%s", int(6 - std::strlen(m)) / 2, "", m);
    } else if (s.timeValid) {
        snprintf(row, sizeof(row), "%2d %s", s.now.tm_mday, kMonths[s.now.tm_mon % 12]);
    }
    for (int i = 0; i < 6; ++i) light(b, kAlnum, kAlnumX[i], row[i], kFilterRamp);
}
