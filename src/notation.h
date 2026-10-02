#ifndef NOTATION_H
#define NOTATION_H

#include "score.h"

#include <stdbool.h>

/* Text notation in 4/4 bars, one staff per voice, with the spelling and
 * ties of the MusicXML export. */
bool export_lilypond(const char *path, const Score *score);
bool export_abc(const char *path, const Score *score);

#endif
