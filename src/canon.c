#include "canon.h"

int canon_melody_index(int voice, int time, int delay, int length) {
    if (length <= 0 || voice < 0 || voice > 1) return -1;
    int index = time - voice * delay;
    if (index < 0 || index >= length) return -1;
    return index;
}

int canon_span(int length, int delay) {
    return length + delay;
}

int canon_source_index(int voice, int time, int delay, int length, int retrograde) {
    if (voice != 0 && voice != 1) return -1;
    if (length <= 0) return -1;
    if (voice == 0) return canon_melody_index(0, time, delay, length);

    int base = time - delay;
    if (base < 0 || base >= length) return -1;
    if (retrograde) return length - 1 - base;
    return base;
}
