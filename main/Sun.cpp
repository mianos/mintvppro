#include "Sun.h"

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRad = kPi / 180.0;

// Days since 1970-01-01 for a civil date (Howard Hinnant), since newlib has no timegm.
long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return long(era) * 146097 + doe - 719468;
}

}  // namespace

bool sunTimesFor(time_t day, double latitude, double longitude, time_t& sunrise, time_t& sunset) {
    struct tm local;
    localtime_r(&day, &local);

    const double g = 2 * kPi / 365.0 * local.tm_yday;  // fractional year at noon
    const double eqTimeMin = 229.18 * (0.000075 + 0.001868 * cos(g) - 0.032077 * sin(g) -
                                       0.014615 * cos(2 * g) - 0.040849 * sin(2 * g));
    const double decl = 0.006918 - 0.399912 * cos(g) + 0.070257 * sin(g) - 0.006758 * cos(2 * g) +
                        0.000907 * sin(2 * g) - 0.002697 * cos(3 * g) + 0.00148 * sin(3 * g);
    const double lat = latitude * kRad;
    // 90.833 degrees: the sun's upper limb on the horizon, allowing for refraction.
    const double cosHa = cos(90.833 * kRad) / (cos(lat) * cos(decl)) - tan(lat) * tan(decl);
    if (cosHa < -1 || cosHa > 1) return false;
    const double haDeg = acos(cosHa) / kRad;

    const time_t utcMidnight = time_t(daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday)) * 86400;
    sunrise = utcMidnight + time_t(lround((720 - 4 * (longitude + haDeg) - eqTimeMin) * 60));
    sunset = utcMidnight + time_t(lround((720 - 4 * (longitude - haDeg) - eqTimeMin) * 60));
    return true;
}
