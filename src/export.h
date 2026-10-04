#ifndef EXPORT_H
#define EXPORT_H

#include "perform.h"
#include "score.h"

#include <stdbool.h>

/* 4/4 bars of quarter- or eighth-note steps; long notes split at barlines
 * with ties, as do lengths no single value writes (five eighths). */
bool export_musicxml(const char *path, const Score *score);
/* Pitch over time for every voice. */
bool export_contour(const char *path, const Score *score);
/* 16-bit stereo at 44.1 kHz, played on an INSTRUMENT_* (config.h): the
 * voices panned from left to right, with reverb and a 1.5 s tail after the
 * last note, peaking at -1 dBFS. These are synths, not the sampled
 * instruments of the score page. */
bool export_wav(const char *path, const Score *score, int instrument);
/* The same, played as the performance says: its timing, velocities,
 * sounding lengths, final hold, and each voice's volume and pan. */
bool export_wav_performed(const char *path, const Score *score, int instrument,
                          const Performance *perf);

#endif
