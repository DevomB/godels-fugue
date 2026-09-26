#include "canon.h"

#include "types.h"

int canon_melody_index(int voice, int time, int delay, int length) {
    if (length <= 0 || voice < 0 || voice >= VOICE_MAX) return -1;
    int index = time - voice * delay;
    if (index < 0 || index >= length) return -1;
    return index;
}

int canon_span(int length, int delay) {
    return canon_span_voices(length, delay, 2);
}

int canon_span_voices(int length, int delay, int voices) {
    if (voices < 1) voices = 1;
    if (voices > VOICE_MAX) voices = VOICE_MAX;
    return length + (voices - 1) * delay;
}

int canon_source_index(int voice, int time, int delay, int length, int retrograde) {
    if (voice < 0 || voice >= VOICE_MAX) return -1;
    if (length <= 0) return -1;
    int base = time - voice * delay;
    if (base < 0 || base >= length) return -1;
    if (voice > 0 && retrograde) return length - 1 - base;
    return base;
}
