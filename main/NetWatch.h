#pragma once

// Internet reachability, as a TCP connect probe to the configured targets
// ("host:port,host:port"). Distinct from Wi-Fi: the link can be up while the
// internet behind it is not, and that is what the NET lamp and NO NET show.
//
// A cycle runs every kIntervalMs and only counts as down after two consecutive
// failures, so one lost packet does not blink the lamp. Wi-Fi dropping counts at
// once, because that is not a fluke.
namespace NetWatch {
void start(const char* targets);
void setTargets(const char* targets);
bool up();
bool known();      // a probe has finished at least once
int lastRttMs();
int downForS();    // since the last success, or since start while never up
void linkLost();
void nudge();      // probe now rather than at the end of the interval
}  // namespace NetWatch
