#pragma once
#include <ctime>

// Sunset for the local calendar day containing `day`, as an epoch; false in
// polar day/night. NOAA's solar-position approximation, good to about a minute.
bool sunsetFor(time_t day, double latitude, double longitude, time_t& sunset);
