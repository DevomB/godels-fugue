#ifndef THEORY_H
#define THEORY_H

#include <stdbool.h>

bool pitch_in_c_major(int pitch);
int interval_class(int a, int b);
bool same_direction(int delta_a, int delta_b);
bool is_parallel_fifth(int v0_prev, int v1_prev, int v0_now, int v1_now);
bool is_parallel_octave(int v0_prev, int v1_prev, int v0_now, int v1_now);
bool is_second(int a, int b);
bool leap_exceeds(int a, int b);
int invert_pitch(int axis, int pitch);

#endif
