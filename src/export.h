#ifndef EXPORT_H
#define EXPORT_H

#include "score.h"

#include <stdbool.h>

/* 4/4 bars of quarter-note steps; long notes split at barlines with ties. */
bool export_musicxml(const char *path, const Score *score);
/* Pitch over time for every voice. */
bool export_contour(const char *path, const Score *score);
/* 16-bit stereo at 44.1 kHz, played on an INSTRUMENT_* (config.h): the
 * voices panned from left to right, with reverb and a 1.5 s tail after the
 * last note, peaking at -1 dBFS. */
bool export_wav(const char *path, const Score *score, int instrument);

#endif
