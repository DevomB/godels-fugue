#include "canon.h"
#include "domain.h"
#include "proof.h"
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
    c.w_dissonance = 0;
    c.w_parallel = 0;
    c.strong_chord = 0;
    c.cadence = 0;
    return c;
}

int main(void) {
    PieceConfig config = test_config();

    /* 1. Init domains + empty-row fixpoint */
    {
        SolverState s = {0};
        solver_init(&s, &config);

        for (int i = 0; i < config.length; i++) {
            CHECK(domain_count(&s.domains[i]) == 8);
            CHECK(domain_contains(&s.domains[i], 60));
            CHECK(domain_contains(&s.domains[i], 72));
            CHECK(!domain_contains(&s.domains[i], 61));
        }

        CHECK(propagate_to_fixpoint(&s));
        CHECK(s.backtracks == 0);
        CHECK(entropy_bits(s.domains, config.length) == 36.0);
        CHECK(s.proof.sample_count >= 1);
        CHECK(s.proof.samples[0].bits == 36.0);
        CHECK(s.proof.event_count == 0);

        solver_free(&s);
    }

    /* 2. Forced singleton is not a backtrack */
    {
        SolverState s = {0};
        solver_init(&s, &config);

        domain_clear(&s.domains[3]);
        domain_add(&s.domains[3], 60);
        CHECK(propagate_to_fixpoint(&s));
        CHECK(domain_singleton(&s.domains[3]));
        CHECK(domain_value(&s.domains[3]) == 60);
        CHECK(s.backtracks == 0);

        solver_free(&s);
    }

    /* 3. Contradiction restores */
    {
        SolverState s = {0};
        solver_init(&s, &config);

        MidiDomain copy0 = s.domains[0];
        SolverSnapshot snap;
        int events_before;

        solver_save(&s, &snap);
        events_before = s.proof.event_count;

        domain_clear(&s.domains[0]);
        domain_add(&s.domains[0], 60);
        domain_clear(&s.domains[1]);
        domain_add(&s.domains[1], 72);

        CHECK(!propagate_to_fixpoint(&s));
        CHECK(s.failed);

        solver_restore(&s, &snap);
        CHECK(!s.failed);
        CHECK(s.failed_variable == -1);
        CHECK(domain_equal(&s.domains[0], &copy0));
        CHECK(domain_count(&s.domains[1]) == 8);
        CHECK(domain_contains(&s.domains[1], 64));
        CHECK(s.proof.event_count == events_before);

        solver_free(&s);
    }

    /* 4. Two fresh solves agree */
    {
        SolverState a = {0};
        SolverState b = {0};
        int melody_a[MELODY_MAX];
        int melody_b[MELODY_MAX];
        int bt_a = -1;
        int bt_b = -1;

        solver_init(&a, &config);
        solver_init(&b, &config);

        CHECK(solve(&a, melody_a, &bt_a));
        CHECK(solve(&b, melody_b, &bt_b));
        CHECK(bt_a == bt_b);
        CHECK(memcmp(melody_a, melody_b, (size_t)config.length * sizeof(int)) == 0);
        CHECK(entropy_bits(a.domains, config.length) == 0.0);
        CHECK(entropy_bits(b.domains, config.length) == 0.0);

        solver_free(&a);
        solver_free(&b);
    }

    /* 5. Leap revision drops 60 next to 72; 67 remains */
    {
        SolverState s = {0};
        int melody[MELODY_MAX];
        int backtracks = -1;
        int removed_sixty = 0;

        solver_init(&s, &config);
        domain_clear(&s.domains[1]);
        domain_add(&s.domains[1], 72);
        domain_clear(&s.domains[0]);
        domain_add(&s.domains[0], 60);
        domain_add(&s.domains[0], 67);

        CHECK(solve(&s, melody, &backtracks));
        CHECK(melody[0] == 67);
        CHECK(melody[1] == 72);
        for (int i = 0; i < s.proof.event_count; i++) {
            if (s.proof.events[i].variable_id == 0 &&
                s.proof.events[i].removed_pitch == 60) {
                removed_sixty = 1;
            }
        }
        CHECK(removed_sixty);

        solver_free(&s);
    }

    /* 6. Three-voice identity canon */
    {
        PieceConfig three = test_config();
        three.voices = 3;
        SolverState a = {0};
        SolverState b = {0};
        int melody_a[MELODY_MAX];
        int melody_b[MELODY_MAX];
        int bt_a = -1;
        int bt_b = -1;

        solver_init(&a, &three);
        solver_init(&b, &three);
        CHECK(solve(&a, melody_a, &bt_a));
        CHECK(solve(&b, melody_b, &bt_b));
        CHECK(bt_a == bt_b);
        CHECK(memcmp(melody_a, melody_b, (size_t)three.length * sizeof(int)) == 0);
        CHECK(canon_span_voices(three.length, three.delay, 3) == 20);
        CHECK(canon_melody_index(2, 8, three.delay, three.length) == 0);
        CHECK(entropy_bits(a.domains, three.length) == 0.0);

        solver_free(&a);
        solver_free(&b);
    }

    /* 7. Transposed follower still solves */
    {
        PieceConfig tr = test_config();
        tr.transpose = 7;
        SolverState s = {0};
        int melody[MELODY_MAX];
        int backtracks = -1;
        solver_init(&s, &tr);
        CHECK(solve(&s, melody, &backtracks));
        CHECK(canon_sounding(&tr, 1, melody[0]) == melody[0] + 7);
        solver_free(&s);
    }

    /* 8. Locked 60 next to singleton 72 is killed by leap */
    {
        PieceConfig cfg = test_config();
        SolverState s = {0};
        int melody[MELODY_MAX];
        int backtracks = -1;
        solver_init(&s, &cfg);
        domain_clear(&s.domains[1]);
        domain_add(&s.domains[1], 72);
        solver_lock(&s, 0, 60);
        CHECK(!solve(&s, melody, &backtracks));
        CHECK(s.failed);
        CHECK(s.proof.event_count > 0);
        CHECK(strcmp(s.proof.events[s.proof.event_count - 1].message,
                     "melodic leap") == 0);
        solver_free(&s);
    }

    /* 9. Annealing schedule is deterministic for a fixed seed */
    {
        PieceConfig an = test_config();
        an.energy = 1;
        an.anneal_start = 4;
        an.anneal_end = 1;
        an.anneal_steps = 8;
        an.seed = 3;
        an.w_gravity = 1;
        an.w_leap = 1;
        an.w_curve = 3;
        SolverState a = {0};
        SolverState b = {0};
        int ma[MELODY_MAX];
        int mb[MELODY_MAX];
        int bta = -1;
        int btb = -1;
        solver_init(&a, &an);
        solver_init(&b, &an);
        CHECK(solve(&a, ma, &bta));
        CHECK(solve(&b, mb, &btb));
        CHECK(bta == btb);
        CHECK(memcmp(ma, mb, (size_t)an.length * sizeof(int)) == 0);
        solver_free(&a);
        solver_free(&b);
    }

    printf("ok\n");
    return 0;
}
