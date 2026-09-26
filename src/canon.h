#ifndef CANON_H
#define CANON_H

#include "types.h"

int canon_melody_index(int voice, int time, int delay, int length);
int canon_span(int length, int delay);
int canon_span_voices(int length, int delay, int voices);
int canon_source_index(int voice, int time, int delay, int length, int retrograde);
int canon_voice_delay(const PieceConfig *config, int voice);
int canon_map_source(const PieceConfig *config, int voice, int time);
int canon_span_config(const PieceConfig *config);
int canon_sounding(const PieceConfig *config, int voice, int source_pitch);

#endif
