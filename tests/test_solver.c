#include "canon.h"
#include "constraint.h"
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

    /* 8b. Primary lock: illegal pitch is unsat; legal pitch stays */
    {
        PieceConfig bad = test_config();
        SolverState s = {0};
        int melody[MELODY_MAX];
        int backtracks = -1;
        solver_init(&s, &bad);
        solver_lock(&s, 0, 61);
        CHECK(!solve(&s, melody, &backtracks));
        CHECK(s.failed);
        {
            int linked = 0;
            for (int i = 0; i < s.proof.event_count; i++) {
                const ProofEvent *ev = &s.proof.events[i];
                if (ev->variable_id == 0 && ev->removed_pitch == 61 &&
                    ev->parent_count == 1 && ev->parent_events[0] == -1 &&
                    ev->parent_vars[0] == 0 && ev->parent_pitches[0] == 61) {
                    linked = 1;
                }
            }
            CHECK(linked);
        }
        solver_free(&s);

        PieceConfig good = test_config();
        solver_init(&s, &good);
        solver_lock(&s, 0, 60);
        CHECK(solve(&s, melody, &backtracks));
        CHECK(melody[0] == 60);
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
        {
            static const int linear[] = {60, 60, 67, 64, 60, 62,
                                         62, 64, 67, 60, 64, 64};
            CHECK(memcmp(ma, linear, sizeof(linear)) == 0);
            PieceConfig ratio0 = an;
            ratio0.anneal_ratio = 0;
            SolverState z = {0};
            int mz[MELODY_MAX];
            int btz = -1;
            solver_init(&z, &ratio0);
            CHECK(solve(&z, mz, &btz));
            CHECK(memcmp(mz, linear, sizeof(linear)) == 0);
            solver_free(&z);
        }
        solver_free(&a);
        solver_free(&b);
    }

    /* 9b. Geometric ratio cools T *= ratio/100; fixed seed matches */
    {
        PieceConfig geo = test_config();
        geo.energy = 1;
        geo.anneal_start = 8;
        geo.anneal_ratio = 75;
        geo.seed = 5;
        geo.w_gravity = 1;
        geo.w_leap = 1;
        geo.w_curve = 3;
        SolverState a = {0};
        SolverState b = {0};
        int ma[MELODY_MAX];
        int mb[MELODY_MAX];
        int bta = -1;
        int btb = -1;
        solver_init(&a, &geo);
        solver_init(&b, &geo);
        CHECK(solve(&a, ma, &bta));
        CHECK(solve(&b, mb, &btb));
        CHECK(bta == btb);
        CHECK(memcmp(ma, mb, (size_t)geo.length * sizeof(int)) == 0);
        solver_free(&a);
        solver_free(&b);
    }

    /* 10. Rest occupies a rhythm domain slot; other slots are searched */
    {
        PieceConfig cfg = test_config();
        cfg.rhythm = 1;
        cfg.rest_at = 3;
        SolverState s = {0};
        int melody[MELODY_MAX];
        int backtracks = -1;
        solver_init(&s, &cfg);
        CHECK(s.rhythm_mask[3] == RHYTHM_REST);
        CHECK(s.duration[3] == 0);
        CHECK(s.duration[0] == -1);
        CHECK((s.rhythm_mask[0] & RHYTHM_REST) != 0);
        CHECK((s.rhythm_mask[0] & RHYTHM_QUARTER) != 0);
        CHECK((s.rhythm_mask[0] & RHYTHM_HALF) != 0);
        CHECK(solve(&s, melody, &backtracks));
        CHECK(s.duration[3] == 0);
        CHECK(s.duration[0] == 1);
        CHECK(s.rhythm_mask[0] == RHYTHM_QUARTER);
        solver_free(&s);
    }

    /* 10b. rest_at 0 is a real index; open slots collapse to quarters first */
    {
        PieceConfig cfg = test_config();
        cfg.rhythm = 1;
        cfg.rest_at = 0;
        SolverState a = {0};
        SolverState b = {0};
        int ma[MELODY_MAX];
        int mb[MELODY_MAX];
        int bta = -1;
        int btb = -1;
        solver_init(&a, &cfg);
        CHECK(a.rhythm_mask[0] == RHYTHM_REST);
        CHECK(a.duration[0] == 0);
        CHECK(a.duration[1] == -1);
        solver_init(&b, &cfg);
        CHECK(solve(&a, ma, &bta));
        CHECK(solve(&b, mb, &btb));
        CHECK(bta == btb);
        CHECK(memcmp(ma, mb, (size_t)cfg.length * sizeof(int)) == 0);
        CHECK(a.duration[0] == 0);
        for (int i = 1; i < cfg.length; i++) {
            CHECK(a.duration[i] == 1);
            CHECK(a.rhythm_mask[i] == RHYTHM_QUARTER);
        }
        solver_free(&a);
        solver_free(&b);
    }

    /* 11. Cadence plus a singleton C range has cadence in the unsat core */
    {
        PieceConfig cfg = test_config();
        cfg.cadence = 1;
        cfg.range_low = 60;
        cfg.range_high = 60;
        int core[8];
        int n = 0;
        CHECK(solver_unsat_core(&cfg, core, 8, &n));
        int has_cadence = 0;
        for (int i = 0; i < n; i++) {
            if (core[i] == CID_CADENCE) has_cadence = 1;
        }
        CHECK(has_cadence);
        int has_leap = 0;
        for (int i = 0; i < n; i++) {
            if (core[i] == CID_LEAP) has_leap = 1;
        }
        CHECK(!has_leap);
        for (int i = 0; i < n; i++) {
            SolverState drop = {0};
            int melody[MELODY_MAX];
            int bt = -1;
            solver_init(&drop, &cfg);
            drop.skip_cid[core[i]] = 1;
            CHECK(solve(&drop, melody, &bt));
            solver_free(&drop);
        }
        cfg.cadence = 0;
        SolverState s = {0};
        int melody[MELODY_MAX];
        int bt = -1;
        solver_init(&s, &cfg);
        CHECK(solve(&s, melody, &bt));
        solver_free(&s);
    }

    printf("ok\n");
    return 0;
}
