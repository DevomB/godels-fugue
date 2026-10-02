#ifndef CANON_H
#define CANON_H

#include "config.h"

#include <limits.h>

/* Silence in a voice line: before a voice enters, after it ends, or a
 * rest. No transposition or inversion of a real pitch can produce it. */
enum { SOUND_REST = INT_MIN };

int canon_voice_delay(const PieceConfig *config, int voice);
/* Melody index voice `voice` plays at step `time`, or -1 if it is silent. */
int canon_map_source(const PieceConfig *config, int voice, int time);
/* Number of steps from the first entry to the last note of any voice. */
int canon_span_config(const PieceConfig *config);
/* Transposition of a voice: semitones, or scale steps when diatonic. */
int canon_transpose(const PieceConfig *config, int voice);
/* Pitch a voice sounds for a melody pitch; SOUND_REST for a rest. */
int canon_sounding(const PieceConfig *config, int voice, int source_pitch);

#endif
