#include "ClockFace.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "Font5x7.h"

namespace {

constexpr uint16_t kBg = rgb565(0, 0, 0);
constexpr uint16_t kLit = rgb565(0x40, 0xE0, 0xFF);
constexpr uint16_t kGhost = rgb565(0x08, 0x1C, 0x24);
constexpr uint16_t kText = rgb565(0xD0, 0xD0, 0xD0);
constexpr uint16_t kDim = rgb565(0x70, 0x70, 0x70);
constexpr uint16_t kOk = rgb565(0x30, 0xD0, 0x50);
constexpr uint16_t kWarn = rgb565(0xFF, 0xA0, 0x00);
constexpr uint16_t kErr = rgb565(0xFF, 0x30, 0x30);

constexpr int kDigitW = 46;
constexpr int kDigitH = 96;
constexpr int kSegT = 10;
constexpr int kSegGap = 2;
constexpr int kDigitSpace = 8;
constexpr int kColonW = 20;
constexpr int kDigitsY = 58;

const char* const kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                               "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// Bit order: a b c d e f g (bit 0 = a).
constexpr uint8_t kSegments[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
constexpr uint8_t kDash = 0x40;
constexpr uint8_t kBlank = 0x00;

int textWidth(const std::string& s, int scale) {
    return s.empty() ? 0 : int(s.size()) * 6 * scale - scale;
}

void drawText(Band& b, int x, int y, int scale, const std::string& s, uint16_t color) {
    for (char raw : s) {
        char c = char(std::toupper(static_cast<unsigned char>(raw)));
        if (c < kFontFirst || c > kFontLast) c = ' ';
        const uint8_t* glyph = kFont5x7[c - kFontFirst];
        for (int col = 0; col < 5; ++col) {
            for (int row = 0; row < 7; ++row) {
                if (glyph[col] & (1 << row)) b.fillRect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
        x += 6 * scale;
    }
}

void drawTextCentered(Band& b, int y, int scale, const std::string& s, uint16_t color) {
    drawText(b, (Band::kWidth - textWidth(s, scale)) / 2, y, scale, s, color);
}

// Hexagonal segments: each end tapers to a 45-degree point so neighbours miter.
void hSegment(Band& b, int x0, int x1, int y, uint16_t c) {
    for (int r = 0; r < kSegT; ++r) {
        int inset = std::abs(2 * r - (kSegT - 1)) / 2;
        b.fillRect(x0 + inset, y + r, x1 - x0 - 2 * inset, 1, c);
    }
}

void vSegment(Band& b, int x, int y0, int y1, uint16_t c) {
    const int half = kSegT / 2;
    for (int y = y0; y < y1; ++y) {
        int d = std::min(y - y0, y1 - 1 - y);
        int inset = d < half ? half - d : 0;
        b.fillRect(x + inset, y, kSegT - 2 * inset, 1, c);
    }
}

void drawDigit(Band& b, int x, int y, uint8_t mask) {
    auto col = [mask](int bit) { return (mask & (1 << bit)) ? kLit : kGhost; };
    const int hx0 = x + kSegT / 2 + kSegGap, hx1 = x + kDigitW - kSegT / 2 - kSegGap;
    const int mid = y + kDigitH / 2;
    const int topY0 = y + kSegT / 2 + kSegGap, topY1 = mid - kSegGap;
    const int botY0 = mid + kSegGap, botY1 = y + kDigitH - kSegT / 2 - kSegGap;
    const int rightX = x + kDigitW - kSegT;

    hSegment(b, hx0, hx1, y, col(0));                       // a
    vSegment(b, rightX, topY0, topY1, col(1));              // b
    vSegment(b, rightX, botY0, botY1, col(2));              // c
    hSegment(b, hx0, hx1, y + kDigitH - kSegT, col(3));     // d
    vSegment(b, x, botY0, botY1, col(4));                   // e
    vSegment(b, x, topY0, topY1, col(5));                   // f
    hSegment(b, hx0, hx1, mid - kSegT / 2, col(6));         // g
}

uint16_t netColor(NetState n) {
    switch (n) {
        case NetState::Online: return kOk;
        case NetState::Connecting: return kWarn;
        default: return kErr;
    }
}

std::string formatTemp(bool has, int tenths) {
    if (!has) return "--";
    char buf[12];
    snprintf(buf, sizeof(buf), "%.1f", tenths / 10.0);
    return buf;
}

// Solid isosceles triangle, apex up when rising, filling a size x size box.
void drawTrendArrow(Band& b, int x, int y, int size, Trend t) {
    if (t != Trend::Rising && t != Trend::Falling) return;
    const bool up = t == Trend::Rising;
    for (int row = 0; row < size; ++row) {
        const int half = (up ? row : size - 1 - row) / 2;
        b.fillRect(x + size / 2 - half, y + row, 2 * half + 1, 1, up ? kWarn : kLit);
    }
}

// "IN 24.1^ OUT 19.5v" with a fixed-width arrow slot after each number, so the
// line doesn't shift when an arrow appears or disappears.
void drawTempLine(Band& b, int y, const FaceState& s) {
    constexpr int kScale = 2, kSlot = 6 * kScale, kArrow = 7 * kScale - 4;
    const std::string in = "IN " + formatTemp(s.hasInside, s.insideTenths);
    const std::string out = " OUT " + formatTemp(s.hasOutside, s.outsideTenths);
    const int inW = int(in.size()) * 6 * kScale, outW = int(out.size()) * 6 * kScale;
    int x = (Band::kWidth - (inW + kSlot + outW + kSlot)) / 2;
    drawText(b, x, y, kScale, in, kText);
    x += inW;
    drawTrendArrow(b, x, y + 2, kArrow, s.hasInside ? s.insideTrend : Trend::Unknown);
    x += kSlot;
    drawText(b, x, y, kScale, out, kText);
    x += outW;
    drawTrendArrow(b, x, y + 2, kArrow, s.hasOutside ? s.outsideTrend : Trend::Unknown);
}

}  // namespace

void drawClockFace(Band& b, const FaceState& s) {
    b.fill(kBg);

    b.fillRect(8, 8, 6, 6, netColor(s.net));

    uint8_t digits[4] = {kDash, kDash, kDash, kDash};
    if (s.timeValid) {
        int hour = s.now.tm_hour;
        if (!s.use24h) {
            hour %= 12;
            if (hour == 0) hour = 12;
            drawText(b, Band::kWidth - 8 - textWidth("PM", 2), 8, 2, s.now.tm_hour < 12 ? "AM" : "PM", kDim);
        }
        digits[0] = (!s.use24h && hour < 10) ? kBlank : kSegments[hour / 10];
        digits[1] = kSegments[hour % 10];
        digits[2] = kSegments[s.now.tm_min / 10];
        digits[3] = kSegments[s.now.tm_min % 10];
    }

    drawTempLine(b, 30, s);

    const int totalW = 4 * kDigitW + 2 * kDigitSpace + kColonW;
    int x = (Band::kWidth - totalW) / 2;
    drawDigit(b, x, kDigitsY, digits[0]);
    x += kDigitW + kDigitSpace;
    drawDigit(b, x, kDigitsY, digits[1]);
    x += kDigitW;

    const bool colonOn = !s.timeValid || (s.now.tm_sec % 2 == 0);
    const uint16_t colon = colonOn ? kLit : kGhost;
    const int dot = 8, cx = x + (kColonW - dot) / 2;
    b.fillRect(cx, kDigitsY + kDigitH / 3 - dot / 2, dot, dot, colon);
    b.fillRect(cx, kDigitsY + 2 * kDigitH / 3 - dot / 2, dot, dot, colon);
    x += kColonW;

    drawDigit(b, x, kDigitsY, digits[2]);
    x += kDigitW + kDigitSpace;
    drawDigit(b, x, kDigitsY, digits[3]);

    constexpr int barX = 20, barW = 200, barY = 170, barH = 4;
    b.fillRect(barX, barY, barW, barH, kGhost);
    if (s.timeValid) b.fillRect(barX, barY, barW * (s.now.tm_sec + 1) / 60, barH, kLit);

    if (!s.message.empty()) {
        drawTextCentered(b, 192, 2, s.message, kWarn);
    } else if (s.timeValid) {
        char date[32];
        int n = snprintf(date, sizeof(date), "%d %s", s.now.tm_mday, kMonths[s.now.tm_mon % 12]);
        if (s.hasSunset && n > 0) {
            int hour = s.sunset.tm_hour;
            if (!s.use24h) hour = hour % 12 == 0 ? 12 : hour % 12;  // always evening, so no PM
            snprintf(date + n, sizeof(date) - n, "  SUNSET %d:%02d", hour, s.sunset.tm_min);
        }
        drawTextCentered(b, 192, 2, date, kText);
    }
}
