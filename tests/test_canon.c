#include "canon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);  \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

int main(void) {
    const int delay = 4;
    const int length = 12;

    /* voice 1 silent at t < 4 */
    CHECK(canon_melody_index(1, 0, delay, length) == -1);
    CHECK(canon_melody_index(1, 1, delay, length) == -1);
    CHECK(canon_melody_index(1, 2, delay, length) == -1);
    CHECK(canon_melody_index(1, 3, delay, length) == -1);

    /* voice 0 silent at t >= 12 */
    CHECK(canon_melody_index(0, 12, delay, length) == -1);
    CHECK(canon_melody_index(0, 13, delay, length) == -1);
    CHECK(canon_melody_index(0, 14, delay, length) == -1);
    CHECK(canon_melody_index(0, 15, delay, length) == -1);

    /* t=4: voice0 index 4, voice1 index 0 */
    CHECK(canon_melody_index(0, 4, delay, length) == 4);
    CHECK(canon_melody_index(1, 4, delay, length) == 0);

    /* span is length + delay */
    CHECK(canon_span(length, delay) == 16);

    /* t=15 voice 1 is 11; t=11 voice 0 is 11 */
    CHECK(canon_melody_index(1, 15, delay, length) == 11);
    CHECK(canon_melody_index(0, 11, delay, length) == 11);

    /* voice 2 (third part) silent at t < 8, then x0 */
    CHECK(canon_melody_index(2, 4, delay, length) == -1);
    CHECK(canon_melody_index(2, 7, delay, length) == -1);
    CHECK(canon_melody_index(2, 8, delay, length) == 0);
    CHECK(canon_melody_index(2, 19, delay, length) == 11);
    CHECK(canon_span_voices(length, delay, 3) == 20);
    CHECK(canon_melody_index(3, 12, delay, length) == 0);
    CHECK(canon_melody_index(4, 4, delay, length) == -1);

    /* canon_source_index: delay 4, length 12, retrograde 1 */
    CHECK(canon_source_index(1, 0, delay, length, 1) == -1);
    CHECK(canon_source_index(1, 1, delay, length, 1) == -1);
    CHECK(canon_source_index(1, 2, delay, length, 1) == -1);
    CHECK(canon_source_index(1, 3, delay, length, 1) == -1);
    CHECK(canon_source_index(1, 4, delay, length, 1) == 11);
    CHECK(canon_source_index(1, 8, delay, length, 1) == 7);
    CHECK(canon_source_index(1, 15, delay, length, 1) == 0);
    CHECK(canon_source_index(1, 4, delay, length, 0) == 0);
    CHECK(canon_source_index(0, 4, delay, length, 1) == 4);
    CHECK(canon_source_index(2, 4, delay, length, 1) == -1);
    CHECK(canon_source_index(2, 8, delay, length, 0) == 0);
    CHECK(canon_source_index(2, 8, delay, length, 1) == 11);
    CHECK(canon_source_index(3, 12, delay, length, 0) == 0);
    CHECK(canon_source_index(4, 16, delay, length, 0) == -1);

    {
        PieceConfig c;
        memset(&c, 0, sizeof(c));
        c.length = 12;
        c.voices = 2;
        c.delay = 4;
        CHECK(canon_map_source(&c, 1, 4) == 0);
        CHECK(canon_map_source(&c, 1, 3) == -1);
        CHECK(canon_span_config(&c) == 16);
        CHECK(canon_sounding(&c, 1, 60) == 60);

        c.transpose = 7;
        CHECK(canon_sounding(&c, 1, 60) == 67);
        CHECK(canon_sounding(&c, 0, 60) == 60);

        c.transpose = 0;
        c.augment = 2;
        CHECK(canon_map_source(&c, 1, 4) == 0);
        CHECK(canon_map_source(&c, 1, 5) == 0);
        CHECK(canon_map_source(&c, 1, 6) == 1);
        CHECK(canon_span_config(&c) == 28);

        c.augment = 0;
        c.diminish = 2;
        CHECK(canon_map_source(&c, 1, 4) == 0);
        CHECK(canon_map_source(&c, 1, 5) == 2);
        CHECK(canon_span_config(&c) == 12);

        c.diminish = 0;
        c.phase = 1;
        CHECK(canon_voice_delay(&c, 1) == 5);
        CHECK(canon_map_source(&c, 1, 4) == -1);
        CHECK(canon_map_source(&c, 1, 5) == 0);

        c.phase = 0;
        c.voice_delay[1] = 2;
        CHECK(canon_voice_delay(&c, 1) == 2);
        CHECK(canon_map_source(&c, 1, 2) == 0);
        CHECK(canon_span_config(&c) == 14);
    }

    printf("ok\n");
    return 0;
}
