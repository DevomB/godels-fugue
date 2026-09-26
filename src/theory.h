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
int in_c_triad(int pitch);
int in_c_dominant(int pitch);
int pitch_gravity(int pitch);
int tension_target(int index, int length);
int motif_step_cost(int index, int left, int has_left, int pitch, int motif_a,
                    int motif_b, int w_motif);
int pitch_choice_cost(int index, int length, int pitch, int left, int has_left,
                      int right, int has_right, int w_gravity, int w_leap,
                      int w_curve);
int melody_energy(const int *melody, int length, int w_gravity, int w_leap,
                  int w_curve);
int vertical_cost(int a, int b, int a_prev, int b_prev, int has_prev,
                  int w_dissonance, int w_parallel);
int melody_energy_full(const int *melody, int length, int delay, int w_gravity,
                       int w_leap, int w_curve, int w_dissonance, int w_parallel,
                       int w_motif, int motif_a, int motif_b);

#endif
