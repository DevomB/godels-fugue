#include "theory.h"
#include "test_util.h"

static void test_scales(void) {
    int c_major = key_id(0, MODE_MAJOR);
    int a_minor = key_id(9, MODE_MINOR);
    int d_dorian = key_id(2, MODE_DORIAN);
    int g_major = key_id(7, MODE_MAJOR);

    for (int p = 60; p <= 72; p++) {
        int pc = p % 12;
        bool white = pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
        CHECK(key_has_pitch(c_major, p) == white);
        CHECK(key_has_pitch(d_dorian, p) == white); /* same notes as C major */
    }
    CHECK(key_has_pitch(g_major, 66));
    CHECK(!key_has_pitch(g_major, 65));
    /* A minor keeps G for the melody and G# as the leading tone */
    CHECK(key_has_pitch(a_minor, 67));
    CHECK(key_has_pitch(a_minor, 68));
    CHECK(!key_has_pitch(a_minor, 70));
    CHECK(key_has_pitch(key_id(0, MODE_LYDIAN), 66));
    CHECK(!key_has_pitch(key_id(0, MODE_MIXOLYDIAN), 71));
    CHECK(key_has_pitch(key_id(0, MODE_MIXOLYDIAN), 70));
    CHECK(key_has_pitch(key_id(0, MODE_PHRYGIAN), 61));
    CHECK(key_has_pitch(key_id(0, MODE_LOCRIAN), 66));
    CHECK(!key_has_pitch(key_id(0, MODE_LOCRIAN), 67));

    CHECK(key_tonic(g_major) == 7);
    CHECK(key_mode(a_minor) == MODE_MINOR);
    CHECK(key_valid(KEY_COUNT - 1));
    CHECK(!key_valid(KEY_COUNT));
    CHECK(key_id(0, MODE_COUNT) == -1);
}

static void test_signatures_and_names(void) {
    CHECK(key_fifths(key_id(0, MODE_MAJOR)) == 0);
    CHECK(key_fifths(key_id(7, MODE_MAJOR)) == 1);
    CHECK(key_fifths(key_id(5, MODE_MAJOR)) == -1);
    CHECK(key_fifths(key_id(10, MODE_MAJOR)) == -2);
    CHECK(key_fifths(key_id(9, MODE_MINOR)) == 0);
    CHECK(key_fifths(key_id(4, MODE_MINOR)) == 1);
    CHECK(key_fifths(key_id(2, MODE_MINOR)) == -1);
    CHECK(key_fifths(key_id(2, MODE_DORIAN)) == 0);
    CHECK(key_fifths(key_id(7, MODE_MIXOLYDIAN)) == 0);

    CHECK(keys_closely_related(key_id(0, MODE_MAJOR), key_id(7, MODE_MAJOR)));
    CHECK(keys_closely_related(key_id(0, MODE_MAJOR), key_id(9, MODE_MINOR)));
    CHECK(keys_closely_related(key_id(0, MODE_MAJOR), key_id(2, MODE_MINOR)));
    CHECK(!keys_closely_related(key_id(0, MODE_MAJOR), key_id(0, MODE_MAJOR)));
    CHECK(!keys_closely_related(key_id(0, MODE_MAJOR), key_id(2, MODE_MAJOR)));
    CHECK(!keys_closely_related(key_id(0, MODE_MAJOR), key_id(0, MODE_MINOR)));
    CHECK(key_distance(key_id(0, MODE_MAJOR), key_id(6, MODE_MAJOR)) == 6);
    CHECK(key_distance(key_id(1, MODE_MAJOR), key_id(11, MODE_MAJOR)) == 2);

    char buf[32];
    key_name(key_id(10, MODE_MAJOR), buf, sizeof(buf));
    CHECK(strcmp(buf, "Bb major") == 0);
    key_name(key_id(6, MODE_MAJOR), buf, sizeof(buf));
    CHECK(strcmp(buf, "F# major") == 0);
    key_name(key_id(2, MODE_DORIAN), buf, sizeof(buf));
    CHECK(strcmp(buf, "D dorian") == 0);
    pitch_name(61, false, buf, sizeof(buf));
    CHECK(strcmp(buf, "C#4") == 0);
    pitch_name(61, true, buf, sizeof(buf));
    CHECK(strcmp(buf, "Db4") == 0);
    pitch_name(72, false, buf, sizeof(buf));
    CHECK(strcmp(buf, "C5") == 0);

    CHECK(parse_tonic("C") == 0);
    CHECK(parse_tonic("c#") == 1);
    CHECK(parse_tonic("Bb") == 10);
    CHECK(parse_tonic("Cb") == 11);
    CHECK(parse_tonic("H") == -1);
    CHECK(parse_tonic("Cx") == -1);
    CHECK(parse_mode("minor") == MODE_MINOR);
    CHECK(parse_mode("aeolian") == MODE_MINOR);
    CHECK(parse_mode("ionian") == MODE_MAJOR);
    CHECK(parse_mode("blues") == -1);
}

static void test_chords(void) {
    int c = key_id(0, MODE_MAJOR);
    int am = key_id(9, MODE_MINOR);
    CHECK(key_triad_has(c, DEGREE_I, 60));
    CHECK(key_triad_has(c, DEGREE_I, 64));
    CHECK(key_triad_has(c, DEGREE_I, 67));
    CHECK(!key_triad_has(c, DEGREE_I, 62));
    CHECK(key_triad_has(c, DEGREE_V, 71));
    CHECK(key_triad_has(c, DEGREE_V, 62));
    CHECK(key_triad_has(c, DEGREE_VII, 65));
    /* the minor dominant has the raised leading tone */
    CHECK(key_triad_has(am, DEGREE_V, 68));
    CHECK(!key_triad_has(am, DEGREE_V, 67));
    CHECK(key_triad_has(am, DEGREE_III, 67));

    char buf[16];
    degree_name(c, DEGREE_I, buf, sizeof(buf));
    CHECK(strcmp(buf, "I") == 0);
    degree_name(c, DEGREE_II, buf, sizeof(buf));
    CHECK(strcmp(buf, "ii") == 0);
    degree_name(c, DEGREE_VII, buf, sizeof(buf));
    CHECK(strcmp(buf, "viio") == 0);
    degree_name(am, DEGREE_I, buf, sizeof(buf));
    CHECK(strcmp(buf, "i") == 0);
    degree_name(am, DEGREE_V, buf, sizeof(buf));
    CHECK(strcmp(buf, "V") == 0);
    degree_name(am, DEGREE_II, buf, sizeof(buf));
    CHECK(strcmp(buf, "iio") == 0);

    CHECK(progression_allowed(DEGREE_V, DEGREE_I));
    CHECK(progression_allowed(DEGREE_V, DEGREE_VI));
    CHECK(!progression_allowed(DEGREE_V, DEGREE_IV));
    CHECK(progression_allowed(DEGREE_II, DEGREE_V));
    CHECK(!progression_allowed(DEGREE_II, DEGREE_I));
    CHECK(progression_allowed(DEGREE_I, DEGREE_III));
    CHECK(progression_allowed(DEGREE_IV, DEGREE_IV));
}

static void test_intervals(void) {
    CHECK(is_strong_time(4, 0));
    CHECK(!is_strong_time(3, 0));
    CHECK(is_strong_time(3, 1));
    CHECK(interval_class(60, 67) == 7);
    CHECK(interval_class(67, 60) == 7);

    CHECK(same_direction(2, 5));
    CHECK(!same_direction(2, -2));
    CHECK(!same_direction(0, 4));

    CHECK(is_parallel_fifth(60, 67, 64, 71));
    CHECK(!is_parallel_fifth(60, 67, 67, 60));
    CHECK(!is_parallel_fifth(60, 67, 60, 79));
    CHECK(is_parallel_octave(60, 72, 62, 74));
    CHECK(!is_parallel_octave(60, 72, 60, 84));
    CHECK(is_direct_perfect(60, 64, 62, 69));
    CHECK(!is_direct_perfect(60, 64, 62, 67));
    CHECK(!is_direct_perfect(64, 60, 67, 55));

    CHECK(!leap_exceeds(60, 67, 7));
    CHECK(leap_exceeds(60, 68, 7));

    int third[2] = {60, 64};
    int fourth[2] = {60, 65};
    int second[2] = {60, 62};
    int tritone[2] = {65, 71};
    int unison[2] = {60, 60};
    int octave[2] = {60, 72};
    CHECK(sonority_consonant(third, 2, false, false));
    CHECK(sonority_consonant(octave, 2, false, false));
    CHECK(!sonority_consonant(fourth, 2, false, false));
    CHECK(sonority_consonant(fourth, 2, true, false));
    CHECK(!sonority_consonant(second, 2, true, true));
    CHECK(!sonority_consonant(tritone, 2, true, true));
    CHECK(!sonority_consonant(unison, 2, false, false));
    CHECK(sonority_consonant(unison, 2, false, true));
    /* G-C above a low C: the fourth is between upper voices */
    int six_four[3] = {48, 67, 72};
    CHECK(sonority_consonant(six_four, 3, false, false));
    /* C-F over the bass: the fourth touches the lowest voice */
    int bare[3] = {60, 65, 72};
    CHECK(!sonority_consonant(bare, 3, false, false));

    CHECK(dissonance_grade(60, 64) == 0);
    CHECK(dissonance_grade(60, 65) == 1);
    CHECK(dissonance_grade(60, 62) == 2);
    CHECK(dissonance_grade(60, 66) == 2);
    CHECK(dissonance_grade(60, 61) == 3);
    CHECK(dissonance_grade(60, 71) == 3);
}

static void test_transforms_and_costs(void) {
    CHECK(invert_pitch(67, 62) == 72);
    CHECK(invert_pitch(67, invert_pitch(67, 62)) == 62);
    CHECK(invert_pitch_mod12(62, 60) % 12 == 4); /* D axis maps C to E */
    CHECK(invert_pitch_mod12(62, invert_pitch_mod12(62, 64)) % 12 == 4);
    CHECK(invert_pitch_mod12(67, 72) != invert_pitch(67, 72));

    int c = key_id(0, MODE_MAJOR);
    int g = key_id(7, MODE_MAJOR);
    CHECK(pitch_gravity(60, c) == 0);
    CHECK(pitch_gravity(64, c) == 0);
    CHECK(pitch_gravity(67, c) == 1);
    CHECK(pitch_gravity(62, c) == 2);
    CHECK(pitch_gravity(71, c) == 3);
    CHECK(pitch_gravity(61, c) == 4);
    CHECK(pitch_gravity(67, g) == 0); /* the tonic of G */
    CHECK(pitch_gravity(66, g) == 3); /* its leading tone */

    CHECK(tension_target(0, 12) == 0);
    CHECK(tension_target(5, 12) == 3);
    CHECK(tension_target(11, 12) == 0);
}

int main(void) {
    test_scales();
    test_signatures_and_names();
    test_chords();
    test_intervals();
    test_transforms_and_costs();
    printf("ok\n");
    return 0;
}
