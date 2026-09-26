#include "solver.h"

#include "canon.h"
#include "constraint.h"
#include "domain.h"
#include "theory.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static int rhythm_count(unsigned char mask) {
    return ((mask & RHYTHM_REST) != 0) + ((mask & RHYTHM_QUARTER) != 0) +
           ((mask & RHYTHM_HALF) != 0);
}

static int rhythm_duration(unsigned char mask) {
    if (rhythm_count(mask) != 1) return -1;
    if (mask & RHYTHM_REST) return 0;
    if (mask & RHYTHM_QUARTER) return 1;
    return 2;
}

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
        if (config->rhythm) {
            s->rhythm_mask[i] = (unsigned char)(RHYTHM_REST | RHYTHM_QUARTER |
                                                RHYTHM_HALF);
        } else {
            s->rhythm_mask[i] = RHYTHM_QUARTER;
        }
        if (config->rhythm && config->rest_at >= 0 && config->rest_at == i) {
            s->rhythm_mask[i] = RHYTHM_REST;
        }
        s->duration[i] = rhythm_duration(s->rhythm_mask[i]);
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

    if (s->ac3_n == 0) {
        memset(s->ac3_in, 0, sizeof(s->ac3_in));
        for (int i = 0; i < s->config.length; i++) {
            solver_enqueue(s, i);
        }
    }

    while (s->ac3_n > 0) {
        int v = s->ac3_q[--s->ac3_n];
        s->ac3_in[v] = 0;
        if (!constraints_revise_var(s, v)) {
            return false;
        }
    }

    proof_append_entropy(&s->proof, entropy_bits(s->domains, s->config.length));
    return true;
}

void solver_save(const SolverState *s, SolverSnapshot *snap) {
    memcpy(snap->domains, s->domains, sizeof(snap->domains));
    memcpy(snap->rhythm_mask, s->rhythm_mask, sizeof(snap->rhythm_mask));
    memcpy(snap->duration, s->duration, sizeof(snap->duration));
    snap->proof_mark = proof_mark(&s->proof);
    snap->failed = s->failed;
    snap->failed_variable = s->failed_variable;
    snap->rng = s->rng;
    snap->anneal_step = s->anneal_step;
    snap->trail_n = s->trail_n;
}

void solver_restore(SolverState *s, const SolverSnapshot *snap) {
    int used_trail = 0;
    int delta = s->trail_n - snap->trail_n;
    if (delta > 0 && delta < s->config.length && snap->trail_n >= 0 &&
        snap->trail_n <= s->trail_n && s->trail_n <= TRAIL_MAX) {
        while (s->trail_n > snap->trail_n) {
            s->trail_n -= 1;
            int var = s->trail_var[s->trail_n];
            if (var >= 0 && var < s->config.length) {
                s->domains[var] = s->trail_dom[s->trail_n];
            }
        }
        used_trail = 1;
        for (int i = 0; i < s->config.length; i++) {
            if (!domain_equal(&s->domains[i], &snap->domains[i])) {
                used_trail = 0;
                break;
            }
        }
    }
    if (!used_trail) {
        memcpy(s->domains, snap->domains, sizeof(s->domains));
        s->trail_n = snap->trail_n;
    }
    memcpy(s->rhythm_mask, snap->rhythm_mask, sizeof(s->rhythm_mask));
    memcpy(s->duration, snap->duration, sizeof(s->duration));
    proof_truncate(&s->proof, snap->proof_mark);
    s->failed = snap->failed;
    s->failed_variable = snap->failed_variable;
    s->rng = snap->rng;
    s->anneal_step = snap->anneal_step;
    s->ac3_n = 0;
    memset(s->ac3_in, 0, sizeof(s->ac3_in));
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
    int delay = s->config.delay;
    int follow = -1;
    int has_follow = 0;
    int lead = -1;
    int has_lead = 0;
    if (delay > 0 && index >= delay && domain_singleton(&s->domains[index - delay])) {
        follow = domain_value(&s->domains[index - delay]);
        has_follow = 1;
    }
    if (delay > 0 && index + delay < s->config.length &&
        domain_singleton(&s->domains[index + delay])) {
        lead = domain_value(&s->domains[index + delay]);
        has_lead = 1;
    }
    for (int i = 0; i < n; i++) {
        costs[i] = pitch_choice_cost(index, s->config.length, pitches[i], left, has_left,
                                     right, has_right, s->config.w_gravity, s->config.w_leap,
                                     s->config.w_curve);
        costs[i] += motif_step_cost(index, left, has_left, pitches[i],
                                    s->config.motif_a, s->config.motif_b,
                                    s->config.w_motif);
        if (has_follow) {
            int has_prev = 0;
            int a_prev = 0;
            int b_prev = 0;
            if (index > 0 && index - delay > 0 &&
                domain_singleton(&s->domains[index - 1]) &&
                domain_singleton(&s->domains[index - delay - 1])) {
                has_prev = 1;
                a_prev = domain_value(&s->domains[index - 1]);
                b_prev = domain_value(&s->domains[index - delay - 1]);
            }
            costs[i] += vertical_cost(
                pitches[i], canon_sounding(&s->config, 1, follow), a_prev,
                has_prev ? canon_sounding(&s->config, 1, b_prev) : b_prev,
                has_prev, s->config.w_dissonance, s->config.w_parallel);
        }
        if (has_lead) {
            costs[i] += vertical_cost(lead, canon_sounding(&s->config, 1, pitches[i]),
                                      0, 0, 0, s->config.w_dissonance,
                                      s->config.w_parallel);
        }
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
    int pick_rhythm = 0;

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
                pick_rhythm = 0;
            }
        }
        if (s->config.rhythm) {
            int rcount = rhythm_count(s->rhythm_mask[i]);
            if (rcount == 0) {
                s->failed = true;
                s->failed_variable = i;
                return false;
            }
            if (rcount != 1) {
                all_singleton = false;
                if (pick < 0 || rcount < pick_count) {
                    pick = i;
                    pick_count = rcount;
                    pick_rhythm = 1;
                }
            }
        }
    }

    if (all_singleton) {
        return true;
    }

    if (pick_rhythm) {
        static const unsigned char try_bits[] = {RHYTHM_QUARTER, RHYTHM_REST,
                                                 RHYTHM_HALF};
        for (int k = 0; k < 3; k++) {
            unsigned char bit = try_bits[k];
            if ((s->rhythm_mask[pick] & bit) == 0) continue;
            SolverSnapshot snap;
            solver_save(s, &snap);
            s->rhythm_mask[pick] = bit;
            s->duration[pick] = rhythm_duration(bit);
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

    int order[128];
    int costs[128];
    MidiDomain choices = s->domains[pick];
    int n_order = collect_pitches(&choices, order);

    if (s->config.energy == 1) {
        fill_choice_costs(s, pick, order, n_order, costs);
        int temp = s->config.temperature;
        if (s->config.anneal_steps > 0) {
            int steps = s->config.anneal_steps;
            int k = s->anneal_step;
            if (k > steps) k = steps;
            temp = s->config.anneal_start +
                   (s->config.anneal_end - s->config.anneal_start) * k / steps;
            s->anneal_step += 1;
        }
        if (temp > 0) {
            sample_without_replacement(order, costs, n_order, temp, &s->rng);
        } else {
            sort_by_cost(order, costs, n_order);
        }
    }

    for (int k = 0; k < n_order; k++) {
        int pitch = order[k];
        SolverSnapshot snap;
        solver_save(s, &snap);
        solver_trail_push(s, pick);
        domain_clear(&s->domains[pick]);
        domain_add(&s->domains[pick], pitch);
        solver_enqueue(s, pick);
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
    solver_trail_push(s, index);
    domain_clear(&s->domains[index]);
    domain_add(&s->domains[index], pitch);
    solver_enqueue(s, index);
}

bool solve(SolverState *s, int *melody, int *backtracks) {
    bool ok = false;
    int length = s->config.length;

    if (s->config.energy == 1 &&
        (s->config.temperature > 0 || s->config.anneal_steps > 0)) {
        s->rng = (uint32_t)s->config.seed;
        if (s->rng == 0) {
            s->rng = 1;
        }
        s->anneal_step = 0;
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

bool solver_unsat_core(const PieceConfig *config, int *cids, int max_cids,
                       int *n) {
    if (config == NULL || cids == NULL || n == NULL || max_cids < 1) {
        return false;
    }
    *n = 0;

    SolverState failed = {0};
    solver_init(&failed, config);
    if (solve(&failed, NULL, NULL)) {
        solver_free(&failed);
        return false;
    }

    int cand[16];
    int seen[16];
    int nc = 0;
    memset(seen, 0, sizeof(seen));
    for (int i = 0; i < failed.proof.event_count && nc < 16; i++) {
        int cid = failed.proof.events[i].constraint_id;
        if (cid <= 0 || cid >= 16 || seen[cid]) continue;
        seen[cid] = 1;
        cand[nc++] = cid;
    }
    solver_free(&failed);

    /* Greedy deletion: drop a rule if the instance stays unsat without it. */
    unsigned char drop[16];
    memset(drop, 0, sizeof(drop));
    for (int i = 0; i < nc; i++) {
        SolverState trial = {0};
        solver_init(&trial, config);
        memcpy(trial.skip_cid, drop, sizeof(trial.skip_cid));
        trial.skip_cid[cand[i]] = 1;
        if (!solve(&trial, NULL, NULL)) {
            drop[cand[i]] = 1;
        }
        solver_free(&trial);
    }

    for (int i = 0; i < nc && *n < max_cids; i++) {
        if (!drop[cand[i]]) {
            cids[(*n)++] = cand[i];
        }
    }
    return *n > 0;
}
