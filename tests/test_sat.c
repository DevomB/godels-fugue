#include "sat.h"
#include "solver.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "fail %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

static PieceConfig test_config(void) {
    PieceConfig c;
    c.length = 12;
    c.voices = 2;
    c.delay = 4;
    c.range_low = 60;
    c.range_high = 72;
    c.max_leap = 7;
    c.invert = 0;
    c.invert_mod12 = 0;
    c.axis = 67;
    c.retrograde = 0;
    c.energy = 0;
    c.temperature = 0;
    c.seed = 1;
    c.w_gravity = 0;
    c.w_leap = 0;
    c.w_curve = 0;
    c.transpose = 0;
    c.augment = 0;
    c.diminish = 0;
    c.phase = 0;
    for (int v = 0; v < VOICE_MAX; v++) {
        c.voice_delay[v] = 0;
    }
    c.lock = 0;
    c.lock_index = 0;
    c.lock_pitch = 0;
    c.anneal_start = 0;
    c.anneal_end = 0;
    c.anneal_steps = 0;
    c.anneal_ratio = 0;
    c.w_dissonance = 0;
    c.w_parallel = 0;
    c.strong_chord = 0;
    c.cadence = 0;
    c.rhythm = 0;
    c.rest_at = -1;
    c.cyclic = 0;
    c.w_motif = 0;
    c.motif_a = 0;
    c.motif_b = 0;
    c.motif_c = -128;
    c.motif_d = -128;
    c.modulate_at = -1;
    c.key_second = 0;
    c.w_modulate = 0;
    c.poly_meter = 0;
    for (int i = 0; i < 12; i++) {
        c.pc_weight[i] = 0;
    }
    c.sample = 0;
    return c;
}

int main(void) {
    {
        PieceConfig cfg = test_config();
        int melody[MELODY_MAX];
        CHECK(sat_solve(&cfg, melody) == 1);
        for (int i = 0; i < cfg.length; i++) {
            CHECK(melody[i] == 60);
        }
        SolverState s = {0};
        int csp[MELODY_MAX];
        int bt = -1;
        solver_init(&s, &cfg);
        CHECK(solve(&s, csp, &bt));
        CHECK(memcmp(melody, csp, (size_t)cfg.length * sizeof(int)) == 0);
        solver_free(&s);
    }

    {
        PieceConfig cfg = test_config();
        cfg.cadence = 1;
        cfg.range_low = 60;
        cfg.range_high = 60;
        int melody[MELODY_MAX];
        CHECK(sat_solve(&cfg, melody) == 0);
    }

    {
        PieceConfig cfg = test_config();
        cfg.length = 12;
        cfg.range_low = 48;
        cfg.range_high = 84;
        int melody[MELODY_MAX];
        CHECK(sat_solve(&cfg, melody) == -1);
    }

    printf("ok\n");
    return 0;
}
