#ifndef MIDI_H
#define MIDI_H

#include "perform.h"
#include "score.h"

#include <stdbool.h>

enum { MIDI_PPQ = 480 };

/* Format 1: a conductor track (tempo, meter, key signatures), then one
 * track per voice on its own channel. */
bool midi_write_score(const char *path, const Score *score);
/* The same, played: a tempo change wherever the performance bends time,
 * each note's velocity and sounding length, the final hold, and each
 * track's volume and pan. NULL plays it as written, at velocity 80. */
bool midi_write_performed(const char *path, const Score *score, const Performance *perf);

#endif
