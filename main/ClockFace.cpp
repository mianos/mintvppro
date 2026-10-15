#include "ClockFace.h"

void drawClockFace(Band& b, const FaceState& s) {
    if (s.face == Face::Nixie) {
        drawNixieFace(b, s);
    } else {
        drawVfdFace(b, s);
    }
}
