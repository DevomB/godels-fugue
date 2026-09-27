#ifndef MIDI_H
#define MIDI_H

#include "score.h"

#include <stdbool.h>

enum { MIDI_PPQ = 480 };

/* Format 1: a conductor track (tempo, meter, key signatures), then one
 * track per voice on its own channel. */
bool midi_write_score(const char *path, const Score *score);

#endif
