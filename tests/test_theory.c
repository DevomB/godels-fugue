#include "theory.h"

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
    /* C major */
    CHECK(pitch_in_c_major(60));
    CHECK(pitch_in_c_major(62));
    CHECK(pitch_in_c_major(64));
    CHECK(pitch_in_c_major(65));
    CHECK(pitch_in_c_major(67));
    CHECK(pitch_in_c_major(69));
    CHECK(pitch_in_c_major(71));
    CHECK(pitch_in_c_major(72));
    CHECK(!pitch_in_c_major(61));
    CHECK(!pitch_in_c_major(66));
    CHECK(!pitch_in_c_major(-1));
    CHECK(!pitch_in_c_major(128));

    /* interval_class */
    CHECK(interval_class(60, 67) == 7);
    CHECK(interval_class(67, 60) == 7);
    CHECK(interval_class(60, 65) == 5);

    /* is_second */
    CHECK(is_second(60, 62));
    CHECK(is_second(60, 71));
    CHECK(is_second(71, 72));
    CHECK(!is_second(60, 67));

    /* same_direction */
    CHECK(same_direction(2, 5));
    CHECK(same_direction(-3, -1));
    CHECK(!same_direction(2, -2));
    CHECK(!same_direction(0, 4));
    CHECK(!same_direction(4, 0));

    /* parallel fifth / contrary / oblique */
    CHECK(is_parallel_fifth(60, 67, 64, 71));
    CHECK(!is_parallel_fifth(60, 67, 67, 60));
    CHECK(!is_parallel_fifth(60, 67, 60, 79));

    /* parallel octave / oblique */
    CHECK(is_parallel_octave(60, 72, 62, 74));
    CHECK(!is_parallel_octave(60, 72, 60, 84));

    /* leap_exceeds */
    CHECK(!leap_exceeds(60, 67));
    CHECK(leap_exceeds(60, 68));
    CHECK(leap_exceeds(72, 60));

    /* invert_pitch */
    CHECK(invert_pitch(67, 62) == 72);
    CHECK(invert_pitch(67, 72) == 62);
    CHECK(invert_pitch(67, 67) == 67);
    CHECK(invert_pitch(67, invert_pitch(67, 62)) == 62);

    printf("ok\n");
    return 0;
}
