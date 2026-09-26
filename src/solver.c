#include "solver.h"

#include "constraint.h"
#include "domain.h"
#include "theory.h"

#include <math.h>
#include <stdint.h>
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
    snap->rng = s->rng;
}

void solver_restore(SolverState *s, const SolverSnapshot *snap) {
    memcpy(s->domains, snap->domains, sizeof(s->domains));
    proof_truncate(&s->proof, snap->proof_mark);
    s->failed = snap->failed;
    s->failed_variable = snap->failed_variable;
    s->rng = snap->rng;
}

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int collect_pitches(const MidiDomain *d, int *out) {
    int n = 0;
    for (int pitch = domain_next(d, -1); pitch >= 0;
         pitch = domain_next(d, pitch + 1)) {
        out[n++] = pitch;
    }
    return n;
}

static void neighbor_bounds(const SolverState *s, int index, int *left, int *has_left,
                            int *right, int *has_right) {
    *left = 0;
    *has_left = 0;
    *right = 0;
    *has_right = 0;
    if (index > 0 && domain_singleton(&s->domains[index - 1])) {
        *left = domain_value(&s->domains[index - 1]);
        *has_left = 1;
    }
    if (index + 1 < s->config.length && domain_singleton(&s->domains[index + 1])) {
        *right = domain_value(&s->domains[index + 1]);
        *has_right = 1;
    }
}

static void fill_choice_costs(const SolverState *s, int index, const int *pitches, int n,
                              int *costs) {
    int left;
    int has_left;
    int right;
    int has_right;
    neighbor_bounds(s, index, &left, &has_left, &right, &has_right);
    for (int i = 0; i < n; i++) {
        costs[i] = pitch_choice_cost(index, s->config.length, pitches[i], left, has_left,
                                     right, has_right, s->config.w_gravity, s->config.w_leap,
                                     s->config.w_curve);
    }
}

static void sort_by_cost(int *pitches, int *costs, int n) {
    for (int i = 1; i < n; i++) {
        int pitch = pitches[i];
        int cost = costs[i];
        int j = i;
        while (j > 0 &&
               (costs[j - 1] > cost || (costs[j - 1] == cost && pitches[j - 1] > pitch))) {
            pitches[j] = pitches[j - 1];
            costs[j] = costs[j - 1];
            j--;
        }
        pitches[j] = pitch;
        costs[j] = cost;
    }
}

static void sample_without_replacement(int *pitches, const int *costs, int n,
                                       int temperature, uint32_t *rng) {
    int remaining[128];
    double weights[128];
    int left = n;

    memcpy(remaining, pitches, (size_t)n * sizeof(int));
    for (int i = 0; i < n; i++) {
        weights[i] = exp((double)-costs[i] / (double)temperature);
    }

    for (int out = 0; out < n; out++) {
        double total = 0.0;
        for (int i = 0; i < left; i++) {
            total += weights[i];
        }
        double u = (double)xorshift32(rng) / 4294967296.0;
        double target = u * total;
        double acc = 0.0;
        int pick = left - 1;
        for (int i = 0; i < left; i++) {
            acc += weights[i];
            if (target < acc) {
                pick = i;
                break;
            }
        }
        pitches[out] = remaining[pick];
        remaining[pick] = remaining[left - 1];
        weights[pick] = weights[left - 1];
        left--;
    }
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

    int order[128];
    int costs[128];
    MidiDomain choices = s->domains[pick];
    int n_order = collect_pitches(&choices, order);

    if (s->config.energy == 1) {
        fill_choice_costs(s, pick, order, n_order, costs);
        if (s->config.temperature > 0) {
            sample_without_replacement(order, costs, n_order, s->config.temperature,
                                       &s->rng);
        } else {
            sort_by_cost(order, costs, n_order);
        }
    }

    for (int k = 0; k < n_order; k++) {
        int pitch = order[k];
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

void solver_lock(SolverState *s, int index, int pitch) {
    if (s->failed) {
        return;
    }
    if (index < 0 || index >= s->config.length || pitch < 0 || pitch > 127) {
        s->failed = true;
        s->failed_variable = index;
        return;
    }
    domain_clear(&s->domains[index]);
    domain_add(&s->domains[index], pitch);
}

bool solve(SolverState *s, int *melody, int *backtracks) {
    bool ok = false;
    int length = s->config.length;

    if (s->config.energy == 1 && s->config.temperature > 0) {
        s->rng = (uint32_t)s->config.seed;
        if (s->rng == 0) {
            s->rng = 1;
        }
    }

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
