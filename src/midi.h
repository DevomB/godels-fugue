#ifndef MIDI_H
#define MIDI_H

#include <stdbool.h>

bool midi_write_canon(const char *path, const int *lead, const int *follow,
		      int length, int delay);

#endif
