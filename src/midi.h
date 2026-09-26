#ifndef MIDI_H
#define MIDI_H

#include <stdbool.h>

bool midi_write_canon(const char *path, const int *lead, const int *follow,
		      int length, int delay);
bool midi_write_voices(const char *path, const int *const *lines,
		       const int *start_ticks, int n_voices, int length);

#endif
