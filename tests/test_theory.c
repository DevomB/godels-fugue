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

    /* pitch_gravity */
    CHECK(pitch_gravity(60) == 0);
    CHECK(pitch_gravity(64) == 0);
    CHECK(pitch_gravity(72) == 0);
    CHECK(pitch_gravity(67) == 1);
    CHECK(pitch_gravity(62) == 2);
    CHECK(pitch_gravity(71) == 3);
    CHECK(pitch_gravity(61) == 4);

    /* tension_target */
    CHECK(tension_target(0, 12) == 0);
    CHECK(tension_target(2, 12) == 1);
    CHECK(tension_target(5, 12) == 3);
    CHECK(tension_target(11, 12) == 0);

    /* pitch_choice_cost: G at peak cheaper than C */
    CHECK(pitch_choice_cost(2, 12, 67, 60, 1, 0, 0, 1, 1, 3) <
          pitch_choice_cost(2, 12, 60, 60, 1, 0, 0, 1, 1, 3));

    printf("ok\n");
    return 0;
}
