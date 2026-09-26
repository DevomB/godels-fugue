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

int in_c_triad(int pitch) {
    int pc = pitch % 12;
    if (pc < 0) pc += 12;
    return pc == 0 || pc == 4 || pc == 7;
}

int in_c_dominant(int pitch) {
    int pc = pitch % 12;
    if (pc < 0) pc += 12;
    return pc == 7 || pc == 11 || pc == 2;
}

static int iabs(int x) {
    return x < 0 ? -x : x;
}

int pitch_gravity(int pitch) {
    if (pitch < 0 || pitch > 127) return 4;
    switch (pitch % 12) {
    case 0:
    case 4:
        return 0;
    case 7:
        return 1;
    case 2:
    case 5:
    case 9:
        return 2;
    case 11:
        return 3;
    default:
        return 4;
    }
}

int tension_target(int index, int length) {
    static const int arch[12] = {0, 0, 1, 2, 3, 3, 2, 1, 1, 0, 0, 0};
    if (length <= 1) return 0;
    if (index < 0) index = 0;
    if (index > length - 1) index = length - 1;
    return arch[index * 11 / (length - 1)];
}

int motif_step_cost(int index, int left, int has_left, int pitch, int motif_a,
                    int motif_b, int w_motif) {
    if (w_motif == 0 || !has_left) return 0;
    int expect = ((index - 1) % 2 == 0) ? motif_a : motif_b;
    return (pitch - left) == expect ? 0 : w_motif;
}

int pitch_choice_cost(int index, int length, int pitch, int left, int has_left,
                      int right, int has_right, int w_gravity, int w_leap,
                      int w_curve) {
    int gravity = pitch_gravity(pitch);
    int cost = w_gravity * gravity +
               w_curve * iabs(gravity - tension_target(index, length));
    if (has_left) cost += w_leap * (iabs(pitch - left) / 4);
    if (has_right) cost += w_leap * (iabs(pitch - right) / 4);
    return cost;
}

int melody_energy(const int *melody, int length, int w_gravity, int w_leap,
                  int w_curve) {
    int total = 0;
    for (int i = 0; i < length; i++) {
        int left = 0;
        int has_left = 0;
        int right = 0;
        int has_right = 0;
        if (i > 0) {
            left = melody[i - 1];
            has_left = 1;
        }
        if (i + 1 < length) {
            right = melody[i + 1];
            has_right = 1;
        }
        total += pitch_choice_cost(i, length, melody[i], left, has_left, right,
                                   has_right, w_gravity, w_leap, w_curve);
    }
    return total;
}

int vertical_cost(int a, int b, int a_prev, int b_prev, int has_prev,
                  int w_dissonance, int w_parallel) {
    int cost = 0;
    int ic = interval_class(a, b);
    if (is_second(a, b) || ic == 6) {
        cost += w_dissonance;
    }
    if (has_prev && w_parallel > 0) {
        if (is_parallel_fifth(a_prev, b_prev, a, b) ||
            is_parallel_octave(a_prev, b_prev, a, b)) {
            cost += w_parallel;
        }
    }
    return cost;
}

static int follower_sounding(int pitch, int invert, int axis, int transpose) {
    if (invert) pitch = invert_pitch(axis, pitch);
    return pitch + transpose;
}

int melody_energy_full(const int *melody, int length, int delay, int w_gravity,
                       int w_leap, int w_curve, int w_dissonance, int w_parallel,
                       int w_motif, int motif_a, int motif_b, int invert,
                       int axis, int transpose) {
    int total = melody_energy(melody, length, w_gravity, w_leap, w_curve);
    if (w_motif > 0) {
        for (int i = 1; i < length; i++) {
            total += motif_step_cost(i, melody[i - 1], 1, melody[i], motif_a,
                                     motif_b, w_motif);
        }
    }
    if (w_dissonance == 0 && w_parallel == 0) {
        return total;
    }
    for (int t = 0; t < length; t++) {
        int follow = t - delay;
        if (follow < 0) continue;
        int has_prev = 0;
        int a_prev = 0;
        int b_prev = 0;
        if (t > 0 && follow > 0) {
            has_prev = 1;
            a_prev = melody[t - 1];
            b_prev = follower_sounding(melody[follow - 1], invert, axis, transpose);
        }
        total += vertical_cost(melody[t],
                               follower_sounding(melody[follow], invert, axis,
                                                 transpose),
                               a_prev, b_prev, has_prev, w_dissonance,
                               w_parallel);
    }
    return total;
}
