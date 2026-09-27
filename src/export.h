#ifndef EXPORT_H
#define EXPORT_H

#include "score.h"

#include <stdbool.h>

/* 4/4 bars of quarter-note steps; long notes split at barlines with ties. */
bool export_musicxml(const char *path, const Score *score);
/* Pitch over time for every voice. */
bool export_contour(const char *path, const Score *score);
/* 16-bit mono at 44.1 kHz; sample = 1 uses a triangle wavetable. */
bool export_wav(const char *path, const Score *score, int sample);

#endif
