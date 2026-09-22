#include "solver.h"

#include "constraint.h"
#include "domain.h"
#include "theory.h"

#include <string.h>

void solver_init(SolverState *s, const PieceConfig *config) {
    proof_free(&s->proof);
    memset(s, 0, sizeof(*s));
    s->config = *config;
    s->failed_variable = -1;
    proof_init(&s->proof);

    if (config->length < 1 || config->length > MELODY_MAX) {
        s->failed = true;
        return;
    }

    for (int i = 0; i < config->length; i++) {
        domain_clear(&s->domains[i]);
        for (int pitch = config->range_low; pitch <= config->range_high; pitch++) {
            if (pitch_in_c_major(pitch)) {
                domain_add(&s->domains[i], pitch);
            }
        }
    }
}

void solver_free(SolverState *s) {
    proof_free(&s->proof);
    memset(s, 0, sizeof(*s));
}

bool propagate_to_fixpoint(SolverState *s) {
    if (s->failed) {
        return false;
    }

    for (;;) {
        MidiDomain before[MELODY_MAX];
        memcpy(before, s->domains, sizeof(before));

        if (!constraints_revise(s)) {
            return false;
        }

        bool unchanged = true;
        for (int i = 0; i < s->config.length; i++) {
            if (!domain_equal(&before[i], &s->domains[i])) {
                unchanged = false;
                break;
            }
        }
        if (unchanged) {
            proof_append_entropy(&s->proof, entropy_bits(s->domains, s->config.length));
            return true;
        }
    }
}

void solver_save(const SolverState *s, SolverSnapshot *snap) {
    memcpy(snap->domains, s->domains, sizeof(snap->domains));
    snap->proof_mark = proof_mark(&s->proof);
    snap->failed = s->failed;
    snap->failed_variable = s->failed_variable;
}

void solver_restore(SolverState *s, const SolverSnapshot *snap) {
    memcpy(s->domains, snap->domains, sizeof(s->domains));
    proof_truncate(&s->proof, snap->proof_mark);
    s->failed = snap->failed;
    s->failed_variable = snap->failed_variable;
}

/* Ceiling is MELODY_MAX 32; snapshots copy the domain array plus the proof
 * mark; switch to a trail if snapshots show up in a profile. */
static bool search(SolverState *s) {
    if (!propagate_to_fixpoint(s)) {
        return false;
    }

    int length = s->config.length;
    bool all_singleton = true;
    int pick = -1;
    int pick_count = 0;

    for (int i = 0; i < length; i++) {
        int count = domain_count(&s->domains[i]);
        if (count == 0) {
            s->failed = true;
            s->failed_variable = i;
            return false;
        }
        if (count != 1) {
            all_singleton = false;
            /* MRV; strict < so lower index wins ties */
            if (pick < 0 || count < pick_count) {
                pick = i;
                pick_count = count;
            }
        }
    }

    if (all_singleton) {
        return true;
    }

    MidiDomain choices = s->domains[pick];
    for (int pitch = domain_next(&choices, -1); pitch >= 0;
         pitch = domain_next(&choices, pitch)) {
        SolverSnapshot snap;
        solver_save(s, &snap);
        domain_clear(&s->domains[pick]);
        domain_add(&s->domains[pick], pitch);
        if (search(s)) {
            return true;
        }
        solver_restore(s, &snap);
        s->backtracks += 1;
    }

    s->failed = true;
    s->failed_variable = pick;
    return false;
}

bool solve(SolverState *s, int *melody, int *backtracks) {
    bool ok = false;
    int length = s->config.length;

    if (!s->failed && length >= 1 && length <= MELODY_MAX) {
        ok = search(s);
        if (ok && melody != NULL) {
            for (int i = 0; i < length; i++) {
                melody[i] = domain_value(&s->domains[i]);
            }
        }
    }

    if (backtracks != NULL) {
        *backtracks = s->backtracks;
    }
    return ok;
}
