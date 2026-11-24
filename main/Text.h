#pragma once

// Small conversions, so the code does not go through sscanf/snprintf/atoi.
// from_chars and to_chars are <charconv>: no locale, no thrown exceptions (this
// target is built with -fno-exceptions), and bounds come from the string_view
// rather than from a terminator.
#include <array>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>

namespace text {

inline std::string_view trim(std::string_view s) {
    const size_t first = s.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}

inline std::optional<int> toInt(std::string_view s) {
    int value = 0;
    const auto [end, err] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (err != std::errc() || end != s.data() + s.size()) return std::nullopt;
    return value;
}

// Hand-rolled rather than from_chars<double>: that overload drags in about 60 KB
// of float conversion, and the only callers are two coordinates on the settings
// page. Plain digits either side of one optional point.
inline std::optional<double> toDouble(std::string_view s) {
    bool negative = false;
    if (!s.empty() && (s.front() == '-' || s.front() == '+')) {
        negative = s.front() == '-';
        s.remove_prefix(1);
    }
    static constexpr long long kPow10[] = {1, 10, 100, 1000, 10000, 100000,
                                           1000000, 10000000, 100000000, 1000000000};
    long long whole = 0, fraction = 0;
    int before = 0, after = 0;
    bool point = false;
    for (char c : s) {
        if (c == '.') {
            if (point) return std::nullopt;
            point = true;
            continue;
        }
        if (c < '0' || c > '9') return std::nullopt;
        if (point) {
            if (after == 9) return std::nullopt;
            fraction = fraction * 10 + (c - '0');
            ++after;
        } else {
            if (before == 9) return std::nullopt;
            whole = whole * 10 + (c - '0');
            ++before;
        }
    }
    if (before == 0 && after == 0) return std::nullopt;
    const double value = static_cast<double>(whole) + static_cast<double>(fraction) / kPow10[after];
    return negative ? -value : value;
}

// A tenths value with one decimal, e.g. (-8) -> "-0.8", as "%1.f" of tenths/10.0
// produced. Integer maths, so no float conversion is linked in for the display.
inline std::string fixed1(int tenths) {
    const int magnitude = tenths < 0 ? -tenths : tenths;
    std::string out = tenths < 0 ? "-" : "";
    out += std::to_string(magnitude / 10);
    out += '.';
    out += static_cast<char>('0' + magnitude % 10);
    return out;
}

// Space-padded to `width`, as "%2d" did. Numbers wider than the width are not
// truncated, they simply take the space.
inline std::string padded(int value, int width) {
    std::string out = std::to_string(value);
    if (static_cast<int>(out.size()) < width) out.insert(0, width - out.size(), ' ');
    return out;
}

inline std::string hex(uint32_t value) {
    std::array<char, 12> buf{};
    const auto [end, err] = std::to_chars(buf.data(), buf.data() + buf.size(), value, 16);
    std::string out = "0x";
    if (err == std::errc()) out.append(buf.data(), end);
    return out;
}

// `s` centred in a `width` field and cut to it: the date row is six characters
// wide and a status word has to sit in the middle of it.
inline std::string centred(std::string_view s, int width) {
    if (static_cast<int>(s.size()) >= width) return std::string(s.substr(0, width));
    const size_t left = static_cast<size_t>(width - static_cast<int>(s.size())) / 2;
    std::string out(left, ' ');
    out += s;
    out.append(static_cast<size_t>(width) - left - s.size(), ' ');  // always exactly width
    return out;
}

}  // namespace text
