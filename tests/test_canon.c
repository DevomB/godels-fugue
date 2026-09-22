#include "canon.h"

#include <stdio.h>
#include <stdlib.h>

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

    /* voice 2 returns -1 */
    CHECK(canon_melody_index(2, 4, delay, length) == -1);

    printf("ok\n");
    return 0;
}
