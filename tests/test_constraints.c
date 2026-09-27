#include "constraint.h"
#include "theory.h"

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

static void fill_c_major_range(MidiDomain *d, int low, int high) {
    domain_clear(d);
    for (int p = low; p <= high; p++) {
        if (pitch_in_c_major(p)) domain_add(d, p);
    }
}

static void setup_state(SolverState *s) {
    memset(s, 0, sizeof(*s));
    s->config.length = 12;
    s->config.voices = 2;
    s->config.delay = 4;
    s->config.range_low = 60;
    s->config.range_high = 72;
    s->config.max_leap = 7;
    s->config.modulate_at = -1;
    s->failed_variable = -1;
    proof_init(&s->proof);
    for (int i = 0; i < s->config.length; i++)
        fill_c_major_range(&s->domains[i], 60, 72);
}

static bool proof_has(const ProofLog *log, const char *message, int pitch) {
    for (int i = 0; i < log->event_count; i++) {
        if (log->events[i].removed_pitch == pitch &&
            strcmp(log->events[i].message, message) == 0)
            return true;
    }
    return false;
}

static bool proof_has_second_for(const ProofLog *log, int pitch) {
    return proof_has(log, "second", pitch);
}

int main(void) {
    SolverState s;

    /* 1. scale + range */
    setup_state(&s);
    domain_add(&s.domains[0], 61);
    domain_add(&s.domains[0], 48);
    CHECK(constraints_revise(&s) == true);
    CHECK(!domain_contains(&s.domains[0], 61));
    CHECK(!domain_contains(&s.domains[0], 48));
    CHECK(domain_count(&s.domains[0]) == 8);
    CHECK(proof_has(&s.proof, "scale", 61));
    CHECK(proof_has(&s.proof, "range", 48));
    {
        int unary_parents = 0;
        for (int i = 0; i < s.proof.event_count; i++) {
            if ((s.proof.events[i].removed_pitch == 61 ||
                 s.proof.events[i].removed_pitch == 48) &&
                s.proof.events[i].parent_count > 0) {
                unary_parents = 1;
            }
        }
        CHECK(!unary_parents);
    }
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* 2. melodic leap */
    setup_state(&s);
    domain_clear(&s.domains[0]);
    domain_add(&s.domains[0], 60);
    domain_clear(&s.domains[1]);
    domain_add(&s.domains[1], 60);
    domain_add(&s.domains[1], 72);
    CHECK(constraints_revise(&s) == true);
    CHECK(!domain_contains(&s.domains[1], 72));
    CHECK(domain_contains(&s.domains[0], 60));
    CHECK(domain_contains(&s.domains[1], 60));
    CHECK(proof_has(&s.proof, "melodic leap", 72));
    CHECK(!s.failed);
    {
        int found_parent = 0;
        for (int i = 0; i < s.proof.event_count; i++) {
            const ProofEvent *ev = &s.proof.events[i];
            if (ev->variable_id == 1 && ev->removed_pitch == 72 &&
                strcmp(ev->message, "melodic leap") == 0) {
                CHECK(ev->parent_count >= 1);
                CHECK(ev->parent_vars[0] == 0);
                CHECK(ev->parent_pitches[0] == 60);
                found_parent = 1;
            }
        }
        CHECK(found_parent);
    }
    proof_free(&s.proof);

    /* 3. strong-beat second */
    setup_state(&s);
    domain_clear(&s.domains[0]);
    domain_add(&s.domains[0], 60);
    domain_clear(&s.domains[4]);
    domain_add(&s.domains[4], 62);
    domain_add(&s.domains[4], 64);
    CHECK(constraints_revise(&s) == true);
    CHECK(!domain_contains(&s.domains[4], 62));
    CHECK(domain_contains(&s.domains[4], 64));
    CHECK(proof_has(&s.proof, "second", 62));
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* 4. weak-beat second unconstrained */
    setup_state(&s);
    domain_clear(&s.domains[1]);
    domain_add(&s.domains[1], 60);
    domain_clear(&s.domains[5]);
    domain_add(&s.domains[5], 62);
    CHECK(constraints_revise(&s) == true);
    CHECK(domain_contains(&s.domains[1], 60));
    CHECK(domain_contains(&s.domains[5], 62));
    CHECK(!proof_has_second_for(&s.proof, 60));
    CHECK(!proof_has_second_for(&s.proof, 62));
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* 5. parallel fifth */
    setup_state(&s);
    domain_clear(&s.domains[4]);
    domain_add(&s.domains[4], 60);
    domain_clear(&s.domains[0]);
    domain_add(&s.domains[0], 67);
    domain_clear(&s.domains[1]);
    domain_add(&s.domains[1], 71);
    domain_clear(&s.domains[5]);
    domain_add(&s.domains[5], 64);
    domain_add(&s.domains[5], 60);
    CHECK(constraints_revise(&s) == true);
    CHECK(!domain_contains(&s.domains[5], 64));
    CHECK(domain_contains(&s.domains[5], 60));
    CHECK(proof_has(&s.proof, "parallel fifth", 64));
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* 6. contrary motion fifths stay */
    setup_state(&s);
    domain_clear(&s.domains[4]);
    domain_add(&s.domains[4], 60);
    domain_clear(&s.domains[5]);
    domain_add(&s.domains[5], 67);
    domain_clear(&s.domains[0]);
    domain_add(&s.domains[0], 67);
    domain_clear(&s.domains[1]);
    domain_add(&s.domains[1], 60);
    CHECK(constraints_revise(&s) == true);
    CHECK(domain_contains(&s.domains[4], 60));
    CHECK(domain_contains(&s.domains[5], 67));
    CHECK(domain_contains(&s.domains[0], 67));
    CHECK(domain_contains(&s.domains[1], 60));
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* 6b. partner domain wider than 8 still keeps a supported pitch */
    setup_state(&s);
    domain_clear(&s.domains[4]);
    domain_add(&s.domains[4], 60);
    domain_clear(&s.domains[5]);
    domain_add(&s.domains[5], 67);
    domain_fill_range(&s.domains[0], 60, 72);
    domain_clear(&s.domains[1]);
    domain_add(&s.domains[1], 60);
    CHECK(domain_count(&s.domains[0]) > 8);
    CHECK(constraints_revise(&s) == true);
    CHECK(domain_contains(&s.domains[5], 67));
    CHECK(domain_contains(&s.domains[1], 60));
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* 7. parallel octave */
    setup_state(&s);
    domain_clear(&s.domains[4]);
    domain_add(&s.domains[4], 60);
    domain_clear(&s.domains[0]);
    domain_add(&s.domains[0], 60);
    domain_clear(&s.domains[1]);
    domain_add(&s.domains[1], 62);
    domain_clear(&s.domains[5]);
    domain_add(&s.domains[5], 62);
    domain_add(&s.domains[5], 60);
    CHECK(constraints_revise(&s) == true);
    CHECK(!domain_contains(&s.domains[5], 62));
    CHECK(domain_contains(&s.domains[5], 60));
    CHECK(proof_has(&s.proof, "parallel octave", 62));
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* 8. empty domain failure */
    setup_state(&s);
    domain_clear(&s.domains[0]);
    domain_add(&s.domains[0], 60);
    domain_clear(&s.domains[1]);
    domain_add(&s.domains[1], 72);
    CHECK(constraints_revise(&s) == false);
    CHECK(s.failed);
    CHECK(s.failed_variable >= 0);
    proof_free(&s.proof);

    /* A. mirror filter */
    setup_state(&s);
    s.config.invert = 1;
    s.config.axis = 67;
    CHECK(constraints_revise(&s) == true);
    for (int i = 0; i < s.config.length; i++) {
        CHECK(!domain_contains(&s.domains[i], 60));
        CHECK(!domain_contains(&s.domains[i], 64));
        CHECK(!domain_contains(&s.domains[i], 71));
        CHECK(domain_contains(&s.domains[i], 62));
    }
    CHECK(proof_has(&s.proof, "range", 60));
    CHECK(proof_has(&s.proof, "scale", 64));
    CHECK(proof_has(&s.proof, "scale", 71));
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* B. inverted strong beat */
    setup_state(&s);
    s.config.invert = 1;
    s.config.axis = 67;
    domain_clear(&s.domains[0]);
    domain_add(&s.domains[0], 62);
    domain_clear(&s.domains[4]);
    domain_add(&s.domains[4], 62);
    domain_add(&s.domains[4], 65);
    CHECK(constraints_revise(&s) == true);
    CHECK(!domain_contains(&s.domains[4], 62));
    CHECK(domain_contains(&s.domains[4], 65));
    CHECK(proof_has(&s.proof, "second", 62));
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* C. retrograde strong beat */
    setup_state(&s);
    s.config.retrograde = 1;
    s.config.invert = 0;
    domain_clear(&s.domains[11]);
    domain_add(&s.domains[11], 62);
    domain_clear(&s.domains[4]);
    domain_add(&s.domains[4], 60);
    domain_add(&s.domains[4], 65);
    CHECK(constraints_revise(&s) == true);
    CHECK(!domain_contains(&s.domains[4], 60));
    CHECK(domain_contains(&s.domains[4], 65));
    CHECK(proof_has(&s.proof, "second", 60));
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* E. three-voice strong-beat second against voice 3 */
    setup_state(&s);
    s.config.voices = 3;
    domain_clear(&s.domains[0]);
    domain_add(&s.domains[0], 60);
    domain_clear(&s.domains[8]);
    domain_add(&s.domains[8], 62);
    domain_add(&s.domains[8], 64);
    CHECK(constraints_revise(&s) == true);
    CHECK(!domain_contains(&s.domains[8], 62));
    CHECK(domain_contains(&s.domains[8], 64));
    CHECK(proof_has(&s.proof, "second", 62));
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* G. cadence on last strong beat */
    setup_state(&s);
    s.config.cadence = 1;
    domain_clear(&s.domains[8]);
    domain_add(&s.domains[8], 60);
    domain_add(&s.domains[8], 67);
    CHECK(constraints_revise(&s) == true);
    CHECK(!domain_contains(&s.domains[8], 60));
    CHECK(domain_contains(&s.domains[8], 67));
    CHECK(proof_has(&s.proof, "cadence", 60));
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* H. strong-beat C triad */
    setup_state(&s);
    s.config.strong_chord = 1;
    domain_clear(&s.domains[4]);
    domain_add(&s.domains[4], 62);
    domain_add(&s.domains[4], 64);
    CHECK(constraints_revise(&s) == true);
    CHECK(!domain_contains(&s.domains[4], 62));
    CHECK(domain_contains(&s.domains[4], 64));
    CHECK(proof_has(&s.proof, "chord", 62));
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* H2. delay 2: source 2 sounds on a strong beat in the follower */
    setup_state(&s);
    s.config.strong_chord = 1;
    s.config.delay = 2;
    domain_clear(&s.domains[2]);
    domain_add(&s.domains[2], 62);
    domain_add(&s.domains[2], 64);
    CHECK(constraints_revise(&s) == true);
    CHECK(!domain_contains(&s.domains[2], 62));
    CHECK(domain_contains(&s.domains[2], 64));
    CHECK(proof_has(&s.proof, "chord", 62));
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* F. transposed follower makes a strong-beat second */
    setup_state(&s);
    s.config.transpose = 2;
    domain_clear(&s.domains[0]);
    domain_add(&s.domains[0], 60);
    domain_clear(&s.domains[4]);
    domain_add(&s.domains[4], 60);
    domain_add(&s.domains[4], 65);
    CHECK(constraints_revise(&s) == true);
    CHECK(!domain_contains(&s.domains[4], 60));
    CHECK(domain_contains(&s.domains[4], 65));
    CHECK(proof_has(&s.proof, "second", 60));
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* D. retrograde parallel fifth */
    setup_state(&s);
    s.config.retrograde = 1;
    s.config.invert = 0;
    domain_clear(&s.domains[4]);
    domain_add(&s.domains[4], 60);
    domain_clear(&s.domains[11]);
    domain_add(&s.domains[11], 67);
    domain_clear(&s.domains[10]);
    domain_add(&s.domains[10], 71);
    domain_clear(&s.domains[5]);
    domain_add(&s.domains[5], 64);
    domain_add(&s.domains[5], 60);
    CHECK(constraints_revise(&s) == true);
    CHECK(!domain_contains(&s.domains[5], 64));
    CHECK(domain_contains(&s.domains[5], 60));
    CHECK(proof_has(&s.proof, "parallel fifth", 64));
    CHECK(!s.failed);
    proof_free(&s.proof);

    /* G. delay 1 shares x1 between both voices: D-A-E rises in fifths */
    setup_state(&s);
    s.config.length = 3;
    s.config.delay = 1;
    s.config.range_low = 48;
    s.config.range_high = 84;
    for (int i = 0; i < 3; i++) fill_c_major_range(&s.domains[i], 48, 84);
    domain_clear(&s.domains[0]);
    domain_add(&s.domains[0], 50);
    domain_clear(&s.domains[1]);
    domain_add(&s.domains[1], 57);
    CHECK(constraints_revise(&s) == true);
    CHECK(!domain_contains(&s.domains[2], 64));
    CHECK(domain_contains(&s.domains[2], 62));
    CHECK(proof_has(&s.proof, "parallel fifth", 64));
    proof_free(&s.proof);

    return 0;
}
