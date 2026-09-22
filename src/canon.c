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
