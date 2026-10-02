#include "canon.h"
#include "theory.h"
#include "test_util.h"

int main(void) {
    PieceConfig c = test_config();

    /* plain canon at delay 4 */
    CHECK(canon_voice_delay(&c, 0) == 0);
    CHECK(canon_voice_delay(&c, 1) == 4);
    CHECK(canon_voice_delay(&c, 2) == 8);
    CHECK(canon_map_source(&c, 0, 0) == 0);
    CHECK(canon_map_source(&c, 0, 11) == 11);
    CHECK(canon_map_source(&c, 0, 12) == -1);
    CHECK(canon_map_source(&c, 1, 3) == -1);
    CHECK(canon_map_source(&c, 1, 4) == 0);
    CHECK(canon_map_source(&c, 1, 15) == 11);
    CHECK(canon_map_source(&c, 1, 16) == -1);
    CHECK(canon_map_source(&c, 4, 4) == -1);
    CHECK(canon_map_source(&c, -1, 4) == -1);
    CHECK(canon_span_config(&c) == 16);

    c.voices = 3;
    CHECK(canon_map_source(&c, 2, 7) == -1);
    CHECK(canon_map_source(&c, 2, 8) == 0);
    CHECK(canon_span_config(&c) == 20);
    c.voices = 2;

    /* retrograde: the follower starts from the melody's end */
    c.retrograde = 1;
    CHECK(canon_map_source(&c, 1, 4) == 11);
    CHECK(canon_map_source(&c, 1, 15) == 0);
    CHECK(canon_map_source(&c, 0, 4) == 4);
    c.retrograde = 0;

    /* transposition and inversion change only the sounding pitch */
    c.transpose = 7;
    CHECK(canon_sounding(&c, 1, 60) == 67);
    CHECK(canon_sounding(&c, 0, 60) == 60);
    CHECK(canon_sounding(&c, 1, PITCH_REST) == SOUND_REST);
    c.transpose = 0;
    c.invert = 1;
    c.axis = 67;
    CHECK(canon_sounding(&c, 1, 62) == 72);
    c.invert_mod12 = 1;
    c.axis = 62;
    CHECK(canon_sounding(&c, 1, 60) == 64);
    c.invert = 0;
    c.invert_mod12 = 0;

    /* each follower may take its own transposition; same falls back */
    c.voices = 4;
    c.transpose = 5;
    c.voice_transpose[1] = 7;
    c.voice_transpose[2] = 0;
    CHECK(canon_transpose(&c, 0) == 0);
    CHECK(canon_sounding(&c, 1, 60) == 67);
    CHECK(canon_sounding(&c, 2, 60) == 60);
    CHECK(canon_sounding(&c, 3, 60) == 65);
    /* diatonic counts scale steps of the key, after inversion */
    c.diatonic = 1;
    c.key = 0;
    c.mode = MODE_MAJOR;
    c.voice_transpose[1] = 2;
    CHECK(canon_sounding(&c, 1, 60) == 64);
    CHECK(canon_sounding(&c, 1, 62) == 65);
    CHECK(canon_sounding(&c, 2, 62) == 62);
    CHECK(canon_sounding(&c, 1, PITCH_REST) == SOUND_REST);
    c.invert = 1;
    c.axis = 62;
    CHECK(canon_sounding(&c, 1, 67) == 60); /* G mirrors to A around D, then up to C */
    c.invert = 0;
    c.diatonic = 0;
    c.transpose = 0;
    c.voices = 2;
    for (int v = 1; v < VOICE_MAX; v++) c.voice_transpose[v] = TRANSPOSE_SAME;

    /* augmentation holds each note; diminution skips notes */
    c.augment = 2;
    CHECK(canon_map_source(&c, 1, 4) == 0);
    CHECK(canon_map_source(&c, 1, 5) == 0);
    CHECK(canon_map_source(&c, 1, 6) == 1);
    CHECK(canon_span_config(&c) == 28);
    c.augment = 0;
    c.diminish = 2;
    CHECK(canon_map_source(&c, 1, 4) == 0);
    CHECK(canon_map_source(&c, 1, 5) == 2);
    CHECK(canon_map_source(&c, 1, 10) == -1);
    CHECK(canon_span_config(&c) == 12);
    c.diminish = 0;

    /* phase and per-voice delays */
    c.phase = 1;
    CHECK(canon_voice_delay(&c, 1) == 5);
    CHECK(canon_map_source(&c, 1, 4) == -1);
    CHECK(canon_map_source(&c, 1, 5) == 0);
    c.phase = 0;
    c.voice_delay[1] = 2;
    CHECK(canon_voice_delay(&c, 1) == 2);
    CHECK(canon_span_config(&c) == 14);
    c.voice_delay[1] = 0;

    /* a cyclic canon wraps and lasts one melody length */
    c.cyclic = 1;
    CHECK(canon_map_source(&c, 1, 0) == 8);
    CHECK(canon_map_source(&c, 1, 3) == 11);
    CHECK(canon_map_source(&c, 1, 4) == 0);
    CHECK(canon_span_config(&c) == 12);

    printf("ok\n");
    return 0;
}
