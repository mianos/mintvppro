#include <cstdio>
#include <string>

#include "ClockFace.h"
#include "NixieArt.h"

extern const uint8_t kNixieBgStart[] asm("_binary_nixie_bg_bin_start");
extern const uint8_t kNixieGlowStart[] asm("_binary_nixie_glow_bin_start");
extern const uint8_t kNixieTextStart[] asm("_binary_nixie_text_bin_start");

namespace {

const char* const kMonthNames[] = {"January", "February", "March",     "April",   "May",      "June",
                                   "July",    "August",   "September", "October", "November", "December"};
constexpr uint16_t kNixieText = rgb565(nixie::kTextRgb[0], nixie::kTextRgb[1], nixie::kTextRgb[2]);
constexpr uint16_t kNixieAlert = rgb565(0x90, 0x10, 0x00);  // dark red, readable on the brass
constexpr uint16_t kLampError = rgb565(0xFF, 0x30, 0x30);

const nixie::Glyph& nixieGlyph(char c) {
    if (c < nixie::kFirstGlyph || c > nixie::kDegree) c = '?';
    return nixie::kGlyphs[c - nixie::kFirstGlyph];
}

int nixieTextWidth(const std::string& s) {
    int w = 0;
    for (char c : s) w += nixieGlyph(c).advance;
    return w;
}

void drawNixieText(Band& b, int y, const std::string& s, uint16_t color) {
    int x = (Band::kWidth - nixieTextWidth(s)) / 2;
    for (char c : s) {
        const nixie::Glyph& g = nixieGlyph(c);
        if (g.w) b.blend(x + g.left, y, g.w, nixie::kTextH, kNixieTextStart + g.offset, color);
        x += g.advance;
    }
}

std::string nixieTemp(bool has, int tenths) {
    if (!has) return "--";
    char buf[12];
    snprintf(buf, sizeof(buf), "%.1f%c", tenths / 10.0, nixie::kDegree);
    return buf;
}

}  // namespace

// The whole screen is one tube (art from tools/nixie): the glass with unlit
// cathodes, then lit digits added as glow. The first digit is unlit before 10,
// like a 3.5-digit clock.
void drawNixieFace(Band& b, const FaceState& s) {
    using namespace nixie;
    b.blit(0, 0, Band::kWidth, Band::kWidth, kNixieBgStart);

    if (s.timeValid) {
        const int hour = hour12(s.now.tm_hour);
        const int digits[4] = {hour < 10 ? -1 : hour / 10, hour % 10, s.now.tm_min / 10, s.now.tm_min % 10};
        for (int i = 0; i < 4; ++i) {
            if (digits[i] < 0) continue;
            const uint8_t* glow = kNixieGlowStart + digits[i] * kCellW * kCellH;
            b.addGlow(kDigitCx[i] - kCellW / 2, kCellY, kCellW, kCellH, glow, kRamp);
        }
        if (s.now.tm_sec % 2 == 0) {
            b.addGlow(kColonX, kColonY, kColonW, kColonH, kNixieGlowStart + kColonOffset, kRamp);
        }
    }

    // Wi-Fi lamp: steady when connected, blinking while connecting, red if not set up.
    const uint8_t* lamp = kNixieGlowStart + kLampOffset;
    if (s.net == NetState::Online || (s.net == NetState::Connecting && s.now.tm_sec % 2 == 0)) {
        b.addGlow(0, 0, kLamp, kLamp, lamp, kRamp);
    } else if (s.net == NetState::Unprovisioned) {
        b.blend(0, 0, kLamp, kLamp, lamp, kLampError);
    }

    // Brass plate: temperatures, then the date, or a status message while starting.
    const std::string temps =
        "In " + nixieTemp(s.hasInside, s.insideTenths) + "   Out " + nixieTemp(s.hasOutside, s.outsideTenths);
    drawNixieText(b, kTextY[0], temps, kNixieText);
    if (!s.message.empty()) {
        drawNixieText(b, kTextY[1], s.message, kNixieAlert);
    } else if (s.timeValid) {
        char date[24];
        snprintf(date, sizeof(date), "%d %s", s.now.tm_mday, kMonthNames[s.now.tm_mon % 12]);
        drawNixieText(b, kTextY[1], date, kNixieText);
    }
}

