#include "canon.h"
#include "domain.h"
#include "proof.h"
#include "solver.h"
#include "theory.h"
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
    return c;
}

static void set_singleton(MidiDomain *d, int pitch) {
    domain_clear(d);
    domain_add(d, pitch);
}

int main(void) {
    PieceConfig config = test_config();
    const int length = config.length;
    const int delay = config.delay;

    /* 1. Restore after failed fixpoint */
    {
        SolverState s = {0};
        MidiDomain copy0;
        int saved_events;
        SolverSnapshot snap;

        solver_init(&s, &config);
        copy0 = s.domains[0];
        saved_events = s.proof.event_count;
        solver_save(&s, &snap);

        set_singleton(&s.domains[0], 60);
        set_singleton(&s.domains[1], 72);

        CHECK(!propagate_to_fixpoint(&s));
        CHECK(s.failed);

        solver_restore(&s, &snap);
        CHECK(!s.failed);
        CHECK(s.failed_variable == -1);
        CHECK(domain_equal(&s.domains[0], &copy0));
        CHECK(domain_count(&s.domains[1]) == 8);
        CHECK(domain_contains(&s.domains[1], 60));
        CHECK(domain_contains(&s.domains[1], 72));
        CHECK(s.proof.event_count == saved_events);

        solver_free(&s);
    }

    /* 2. Solved melody obeys musical properties */
    {
        SolverState s = {0};
        int melody[12];
        int backtracks = -1;
        int span;

        solver_init(&s, &config);
        if (!solve(&s, melody, &backtracks)) {
            fprintf(stderr, "unsat failed_variable=%d\n", s.failed_variable);
            solver_free(&s);
            exit(1);
        }

        for (int i = 0; i < length; i++) {
            CHECK(domain_singleton(&s.domains[i]));
            CHECK(melody[i] == domain_value(&s.domains[i]));
            CHECK(pitch_in_c_major(melody[i]));
            CHECK(melody[i] >= 60 && melody[i] <= 72);
        }

        for (int i = 0; i < length - 1; i++) {
            CHECK(!leap_exceeds(melody[i], melody[i + 1]));
        }

        span = canon_span(length, delay);
        CHECK(span == 16);

        for (int t = 0; t < span; t++) {
            int v0 = canon_melody_index(0, t, delay, length);
            int v1 = canon_melody_index(1, t, delay, length);

            if (t < length) {
                CHECK(v0 == t);
            } else {
                CHECK(v0 == -1);
            }

            if (t < delay) {
                CHECK(v1 == -1);
            } else {
                CHECK(v1 == t - delay);
                CHECK(melody[t - delay] == melody[v1]);
            }

            if (t % 4 == 0 && v0 >= 0 && v1 >= 0) {
                CHECK(!is_second(melody[v0], melody[v1]));
            }
        }

        for (int t = 0; t + 1 < span; t++) {
            int lead = canon_melody_index(0, t, delay, length);
            int follow = canon_melody_index(1, t, delay, length);
            int lead_next = canon_melody_index(0, t + 1, delay, length);
            int follow_next = canon_melody_index(1, t + 1, delay, length);

            if (lead >= 0 && follow >= 0 && lead_next >= 0 && follow_next >= 0) {
                int v0_prev = melody[lead];
                int v1_prev = melody[follow];
                int v0_now = melody[lead_next];
                int v1_now = melody[follow_next];
                CHECK(!is_parallel_fifth(v0_prev, v1_prev, v0_now, v1_now));
                CHECK(!is_parallel_octave(v0_prev, v1_prev, v0_now, v1_now));
            }
        }

        CHECK(entropy_bits(s.domains, length) == 0.0);

        /* 3. Second fresh solve matches melody, backtracks, and proof events */
        {
            SolverState s2 = {0};
            int melody2[12];
            int backtracks2 = -1;

            solver_init(&s2, &config);
            if (!solve(&s2, melody2, &backtracks2)) {
                fprintf(stderr, "unsat failed_variable=%d\n", s2.failed_variable);
                solver_free(&s2);
                solver_free(&s);
                exit(1);
            }

            CHECK(backtracks == backtracks2);
            for (int i = 0; i < length; i++) {
                CHECK(melody[i] == melody2[i]);
            }

            CHECK(s.proof.event_count == s2.proof.event_count);
            for (int e = 0; e < s.proof.event_count; e++) {
                CHECK(s.proof.events[e].variable_id ==
                      s2.proof.events[e].variable_id);
                CHECK(s.proof.events[e].removed_pitch ==
                      s2.proof.events[e].removed_pitch);
                CHECK(strcmp(s.proof.events[e].message,
                             s2.proof.events[e].message) == 0);
            }

            solver_free(&s2);
        }

        solver_free(&s);
    }

    /* 4. Inverted canon solve */
    {
        PieceConfig inv = test_config();
        inv.invert = 1;
        inv.axis = 67;
        SolverState s = {0};
        int melody[12];
        int backtracks = -1;
        int span;

        solver_init(&s, &inv);
        if (!solve(&s, melody, &backtracks)) {
            fprintf(stderr, "unsat failed_variable=%d\n", s.failed_variable);
            solver_free(&s);
            exit(1);
        }

        {
            SolverState s2 = {0};
            int melody2[12];
            int backtracks2 = -1;

            solver_init(&s2, &inv);
            if (!solve(&s2, melody2, &backtracks2)) {
                fprintf(stderr, "unsat failed_variable=%d\n", s2.failed_variable);
                solver_free(&s2);
                solver_free(&s);
                exit(1);
            }

            CHECK(backtracks == backtracks2);
            for (int i = 0; i < length; i++) {
                CHECK(melody[i] == melody2[i]);
            }
            CHECK(entropy_bits(s2.domains, length) == 0.0);
            solver_free(&s2);
        }

        CHECK(entropy_bits(s.domains, length) == 0.0);

        for (int i = 0; i < length; i++) {
            CHECK(pitch_in_c_major(melody[i]));
            CHECK(melody[i] >= 60 && melody[i] <= 72);
        }
        for (int i = 0; i < length - 1; i++) {
            CHECK(!leap_exceeds(melody[i], melody[i + 1]));
        }

        span = canon_span(length, delay);
        for (int t = 0; t < span; t++) {
            int v1 = canon_melody_index(1, t, delay, length);
            if (t < delay) {
                CHECK(v1 == -1);
            } else {
                int follower = invert_pitch(67, melody[t - delay]);
                CHECK(v1 == t - delay);
                CHECK(follower == invert_pitch(67, melody[v1]));
                CHECK(pitch_in_c_major(follower));
                CHECK(follower >= 60 && follower <= 72);
            }

            if (t % 4 == 0) {
                int v0 = canon_melody_index(0, t, delay, length);
                if (v0 >= 0 && v1 >= 0) {
                    CHECK(!is_second(melody[t],
                                     invert_pitch(67, melody[t - delay])));
                }
            }
        }

        for (int t = 0; t + 1 < span; t++) {
            int lead = canon_melody_index(0, t, delay, length);
            int follow = canon_melody_index(1, t, delay, length);
            int lead_next = canon_melody_index(0, t + 1, delay, length);
            int follow_next = canon_melody_index(1, t + 1, delay, length);

            if (lead >= 0 && follow >= 0 && lead_next >= 0 && follow_next >= 0) {
                int v0_prev = melody[lead];
                int v1_prev = invert_pitch(67, melody[follow]);
                int v0_now = melody[lead_next];
                int v1_now = invert_pitch(67, melody[follow_next]);
                CHECK(!is_parallel_fifth(v0_prev, v1_prev, v0_now, v1_now));
                CHECK(!is_parallel_octave(v0_prev, v1_prev, v0_now, v1_now));
            }
        }

        solver_free(&s);
    }

    /* 5. Retrograde canon solve */
    {
        PieceConfig ret = test_config();
        ret.retrograde = 1;
        ret.invert = 0;
        SolverState s = {0};
        int melody[12];
        int backtracks = -1;
        int span;

        solver_init(&s, &ret);
        if (!solve(&s, melody, &backtracks)) {
            fprintf(stderr, "unsat failed_variable=%d\n", s.failed_variable);
            solver_free(&s);
            exit(1);
        }

        {
            SolverState s2 = {0};
            int melody2[12];
            int backtracks2 = -1;

            solver_init(&s2, &ret);
            if (!solve(&s2, melody2, &backtracks2)) {
                fprintf(stderr, "unsat failed_variable=%d\n", s2.failed_variable);
                solver_free(&s2);
                solver_free(&s);
                exit(1);
            }

            CHECK(backtracks == backtracks2);
            for (int i = 0; i < length; i++) {
                CHECK(melody[i] == melody2[i]);
            }
            CHECK(entropy_bits(s2.domains, length) == 0.0);
            solver_free(&s2);
        }

        CHECK(entropy_bits(s.domains, length) == 0.0);

        for (int i = 0; i < length; i++) {
            CHECK(pitch_in_c_major(melody[i]));
            CHECK(melody[i] >= 60 && melody[i] <= 72);
        }
        for (int i = 0; i < length - 1; i++) {
            CHECK(!leap_exceeds(melody[i], melody[i + 1]));
        }

        span = canon_span(length, delay);
        for (int t = 0; t < span; t++) {
            int v1 = canon_melody_index(1, t, delay, length);
            if (t < delay) {
                CHECK(v1 == -1);
            } else {
                int follower = melody[length - 1 - (t - delay)];
                CHECK(pitch_in_c_major(follower));
                CHECK(follower >= 60 && follower <= 72);
            }

            if (t % 4 == 0) {
                int v0 = canon_melody_index(0, t, delay, length);
                if (v0 >= 0 && v1 >= 0) {
                    CHECK(!is_second(melody[t],
                                     melody[length - 1 - (t - delay)]));
                }
            }
        }

        for (int t = 0; t + 1 < span; t++) {
            int lead = canon_melody_index(0, t, delay, length);
            int follow = canon_melody_index(1, t, delay, length);
            int lead_next = canon_melody_index(0, t + 1, delay, length);
            int follow_next = canon_melody_index(1, t + 1, delay, length);

            if (lead >= 0 && follow >= 0 && lead_next >= 0 && follow_next >= 0) {
                int v0_prev = melody[lead];
                int v1_prev = melody[11 - (t - delay)];
                int v0_now = melody[lead_next];
                int v1_now = melody[11 - (t + 1 - delay)];
                CHECK(!is_parallel_fifth(v0_prev, v1_prev, v0_now, v1_now));
                CHECK(!is_parallel_octave(v0_prev, v1_prev, v0_now, v1_now));
            }
        }

        solver_free(&s);
    }

    /* 6. Energy-ordered search */
    {
        PieceConfig cold = test_config();
        cold.energy = 1;
        cold.temperature = 0;
        cold.seed = 1;
        cold.w_gravity = 1;
        cold.w_leap = 1;
        cold.w_curve = 3;
        cold.invert = 0;
        cold.retrograde = 0;
        cold.length = 12;
        cold.range_low = 60;
        cold.range_high = 72;
        cold.max_leap = 7;

        SolverState s = {0};
        int melody[12];
        int backtracks = -1;
        int all_equal;

        solver_init(&s, &cold);
        if (!solve(&s, melody, &backtracks)) {
            fprintf(stderr, "unsat failed_variable=%d\n", s.failed_variable);
            solver_free(&s);
            exit(1);
        }

        {
            SolverState s2 = {0};
            int melody2[12];
            int backtracks2 = -1;

            solver_init(&s2, &cold);
            if (!solve(&s2, melody2, &backtracks2)) {
                fprintf(stderr, "unsat failed_variable=%d\n", s2.failed_variable);
                solver_free(&s2);
                solver_free(&s);
                exit(1);
            }

            CHECK(backtracks == backtracks2);
            for (int i = 0; i < length; i++) {
                CHECK(melody[i] == melody2[i]);
            }
            CHECK(entropy_bits(s2.domains, length) == 0.0);
            solver_free(&s2);
        }

        CHECK(entropy_bits(s.domains, length) == 0.0);

        all_equal = 1;
        for (int i = 1; i < length; i++) {
            if (melody[i] != melody[0]) {
                all_equal = 0;
                break;
            }
        }
        CHECK(!all_equal);

        for (int i = 0; i < length; i++) {
            CHECK(pitch_in_c_major(melody[i]));
            CHECK(melody[i] >= 60 && melody[i] <= 72);
        }
        for (int i = 0; i < length - 1; i++) {
            CHECK(!leap_exceeds(melody[i], melody[i + 1]));
        }

        solver_free(&s);

        {
            PieceConfig warm = cold;
            warm.temperature = 1;
            SolverState w = {0};
            int warm_melody[12];
            int warm_backtracks = -1;

            solver_init(&w, &warm);
            if (!solve(&w, warm_melody, &warm_backtracks)) {
                fprintf(stderr, "unsat failed_variable=%d\n", w.failed_variable);
                solver_free(&w);
                exit(1);
            }

            {
                SolverState w2 = {0};
                int warm_melody2[12];
                int warm_backtracks2 = -1;

                solver_init(&w2, &warm);
                if (!solve(&w2, warm_melody2, &warm_backtracks2)) {
                    fprintf(stderr, "unsat failed_variable=%d\n",
                            w2.failed_variable);
                    solver_free(&w2);
                    solver_free(&w);
                    exit(1);
                }

                CHECK(warm_backtracks == warm_backtracks2);
                for (int i = 0; i < length; i++) {
                    CHECK(warm_melody[i] == warm_melody2[i]);
                }
                CHECK(entropy_bits(w2.domains, length) == 0.0);
                solver_free(&w2);
            }

            CHECK(entropy_bits(w.domains, length) == 0.0);

            for (int i = 0; i < length; i++) {
                CHECK(pitch_in_c_major(warm_melody[i]));
                CHECK(warm_melody[i] >= 60 && warm_melody[i] <= 72);
            }
            for (int i = 0; i < length - 1; i++) {
                CHECK(!leap_exceeds(warm_melody[i], warm_melody[i + 1]));
            }

            solver_free(&w);
        }
    }

    printf("ok\n");
    return 0;
}
