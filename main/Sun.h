#pragma once
#include <ctime>

// Sunrise and sunset for the local calendar day containing `day`, as epochs; false
// in polar day/night. NOAA's solar-position approximation, good to about a minute.
bool sunTimesFor(time_t day, double latitude, double longitude, time_t& sunrise, time_t& sunset);
