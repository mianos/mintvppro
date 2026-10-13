#pragma once
#include <cstdint>

// Which way a temperature has moved over the trend window (same rule as
// einkweather). Only Rising and Falling draw an arrow; Unknown (no basis yet,
// or a gap in the data) and Steady are both blank.
enum class Trend : int8_t { Unknown, Steady, Rising, Falling };
