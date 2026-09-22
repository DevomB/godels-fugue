#include "theory.h"

bool pitch_in_c_major(int pitch) {
    if (pitch < 0 || pitch > 127) return false;
    switch (pitch % 12) {
    case 0:
    case 2:
    case 4:
    case 5:
    case 7:
    case 9:
    case 11:
        return true;
    default:
        return false;
    }
}

int interval_class(int a, int b) {
    int delta = a - b;
    if (delta < 0) delta = -delta;
    return delta % 12;
}

bool same_direction(int delta_a, int delta_b) {
    if (delta_a == 0 || delta_b == 0) return false;
    return (delta_a > 0 && delta_b > 0) || (delta_a < 0 && delta_b < 0);
}

static bool parallel_interval(int v0_prev, int v1_prev, int v0_now, int v1_now, int interval) {
    if (interval_class(v0_prev, v1_prev) != interval) return false;
    if (interval_class(v0_now, v1_now) != interval) return false;
    return same_direction(v0_now - v0_prev, v1_now - v1_prev);
}

bool is_parallel_fifth(int v0_prev, int v1_prev, int v0_now, int v1_now) {
    return parallel_interval(v0_prev, v1_prev, v0_now, v1_now, 7);
}

bool is_parallel_octave(int v0_prev, int v1_prev, int v0_now, int v1_now) {
    return parallel_interval(v0_prev, v1_prev, v0_now, v1_now, 0);
}

bool is_second(int a, int b) {
    int interval = interval_class(a, b);
    return interval == 1 || interval == 2 || interval == 10 || interval == 11;
}

bool leap_exceeds(int a, int b) {
    int delta = a - b;
    if (delta < 0) delta = -delta;
    return delta > 7;
}

int invert_pitch(int axis, int pitch) {
    return 2 * axis - pitch;
}
