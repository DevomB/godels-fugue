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

    printf("ok\n");
    return 0;
}
