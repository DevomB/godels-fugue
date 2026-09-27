#include "solver.h"

#include "theory.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { R_SAT, R_FAIL, R_LIMIT };

static const Model *model_of(const SolverState *s) {
    return s->model;
}

bool solver_init(SolverState *s, const Model *m) {
    memset(s, 0, sizeof(*s));
    s->model = m;
    s->failed_variable = -1;
    proof_init(&s->proof);
    for (int v = 0; v < m->nvars; v++) {
        s->domains[v] = m->initial[v];
        s->last_event[v] = -1;
    }
    s->qcap = m->ncons + NOGOOD_CAP + 1;
    s->queue = malloc((size_t)s->qcap * sizeof(int));
    s->queued = calloc((size_t)s->qcap, 1);
    s->frames = malloc((size_t)(VAR_MAX + 2) * sizeof(Frame));
    s->nogoods = malloc((size_t)NOGOOD_CAP * sizeof(Nogood));
    s->residue_base = malloc((size_t)(m->ncons > 0 ? m->ncons : 1) * SCOPE_MAX * sizeof(int));
    if (s->queue == NULL || s->queued == NULL || s->frames == NULL || s->nogoods == NULL ||
        s->residue_base == NULL) {
        solver_free(s);
        return false;
    }
    size_t total = 0;
    for (int c = 0; c < m->ncons; c++) {
        const Constraint *con = &m->cons[c];
        for (int p = 0; p < con->n; p++) {
            s->residue_base[c * SCOPE_MAX + p] = (int)total;
            total += (size_t)domain_count(&m->initial[con->vars[p]]) * (size_t)con->n;
        }
    }
    s->residues = malloc((total > 0 ? total : 1) * sizeof(int));
    if (s->residues == NULL) {
        solver_free(s);
        return false;
    }
    for (size_t i = 0; i < total; i++) s->residues[i] = -1;
    for (int c = 0; c < m->ncons; c++) {
        s->queue[s->qlen++] = c;
        s->queued[c] = 1;
    }
    s->optimize = m->config.optimize;
    s->result = -1;
    for (int v = 0; v < m->nvars; v++) {
        if (domain_count(&s->domains[v]) == 0) {
            s->failed = true;
            s->failed_variable = v;
        }
    }
    return true;
}

void solver_free(SolverState *s) {
    proof_free(&s->proof);
    free(s->queue);
    free(s->queued);
    free(s->frames);
    free(s->nogoods);
    free(s->residues);
    free(s->residue_base);
    for (int v = 0; v < VAR_MAX; v++) free(s->watch[v]);
    memset(s, 0, sizeof(*s));
}

static void enqueue(SolverState *s, int item) {
    if (s->queued[item] || s->qlen >= s->qcap) return;
    s->queued[item] = 1;
    s->queue[(s->qhead + s->qlen) % s->qcap] = item;
    s->qlen++;
}

static int dequeue(SolverState *s) {
    int item = s->queue[s->qhead];
    s->qhead = (s->qhead + 1) % s->qcap;
    s->qlen--;
    s->queued[item] = 0;
    return item;
}

static void clear_queue(SolverState *s) {
    while (s->qlen > 0) dequeue(s);
    s->qhead = 0;
}

void solver_touch(SolverState *s, int var) {
    const Model *m = model_of(s);
    for (int k = m->cons_adj_start[var]; k < m->cons_adj_start[var + 1]; k++)
        enqueue(s, m->cons_adj[k]);
    /* a learned conflict can only prune once one of its literals holds */
    if (domain_count(&s->domains[var]) == 1) {
        for (int k = 0; k < s->watch_n[var]; k++) enqueue(s, m->ncons + s->watch[var][k]);
    }
}

int solver_value(const SolverState *s, int var) {
    return domain_value(&s->domains[var]);
}

void solver_values(const SolverState *s, int *values) {
    for (int v = 0; v < model_of(s)->nvars; v++) values[v] = solver_value(s, v);
}

double solver_entropy(const SolverState *s) {
    return entropy_bits(s->domains, model_of(s)->nvars);
}

static bool fail_on(SolverState *s, int var) {
    s->failed = true;
    s->failed_variable = var;
    return false;
}

/* Records the removal, charges it to the decisions behind `sources`,
 * and queues everything that watches the variable. */
static bool remove_value(SolverState *s, int var, int value, int rule, int con,
                         const int *sources, int nsources, const LevelSet *extra) {
    if (!domain_contains(&s->domains[var], value)) return true;
    domain_remove(&s->domains[var], value);
    for (int i = 0; i < nsources; i++) {
        if (sources[i] != var) levelset_union(&s->reason[var], &s->reason[sources[i]]);
    }
    if (extra != NULL) levelset_union(&s->reason[var], extra);

    ProofEvent *ev = proof_append(&s->proof, PROOF_REMOVE, var, value);
    if (ev != NULL) {
        ev->rule = rule;
        ev->constraint = con;
        ev->level = s->level;
        ev->reason = s->reason[var];
        snprintf(ev->message, sizeof(ev->message), "%s", rule_name(rule));
        for (int i = 0; i < nsources; i++) {
            int src = sources[i];
            if (src == var) continue;
            proof_add_parent(ev, s->last_event[src], src, solver_value(s, src));
        }
        s->last_event[var] = s->proof.event_count - 1;
    }
    s->stats.removals++;
    if (rule > 0 && rule < CID_MAX) s->stats.removals_by_rule[rule]++;
    solver_touch(s, var);

    int left = domain_count(&s->domains[var]);
    if (left == 0) return fail_on(s, var);
    if (left == 1) {
        int removal = s->last_event[var];
        ProofEvent *f = proof_append(&s->proof, PROOF_FORCED, var, solver_value(s, var));
        if (f != NULL) {
            f->level = s->level;
            f->reason = s->reason[var];
            f->rule = rule;
            proof_add_parent(f, removal, var, value);
            s->last_event[var] = s->proof.event_count - 1;
        }
    }
    return true;
}

static int *residue_slot(const SolverState *s, int ci, int pos, int value) {
    const Model *m = model_of(s);
    const Constraint *c = &m->cons[ci];
    int rank = m->rank[c->vars[pos]][value];
    return s->residues + s->residue_base[ci * SCOPE_MAX + pos] + rank * c->n;
}

/* Generalized arc consistency: a value stays only if some combination
 * of the other variables' current values satisfies the constraint. The
 * last combination found is kept for every value in it and tried first. */
static bool has_support(SolverState *s, int ci, int pos, int value,
                        int values[SCOPE_MAX][128], const int *counts) {
    const Model *m = model_of(s);
    const Constraint *c = &m->cons[ci];
    int *res = residue_slot(s, ci, pos, value);
    if (res[0] >= 0) {
        bool valid = true;
        for (int i = 0; i < c->n && valid; i++) {
            if (i != pos && !domain_contains(&s->domains[c->vars[i]], res[i])) valid = false;
        }
        if (valid) return true;
    }
    int vals[SCOPE_MAX];
    int idx[SCOPE_MAX];
    for (int i = 0; i < c->n; i++) {
        idx[i] = 0;
        if (i != pos && counts[i] == 0) return false;
    }
    vals[pos] = value;
    for (;;) {
        for (int i = 0; i < c->n; i++) {
            if (i != pos) vals[i] = values[i][idx[i]];
        }
        if (constraint_holds(m, c, vals)) {
            for (int i = 0; i < c->n; i++) {
                memcpy(residue_slot(s, ci, i, vals[i]), vals, (size_t)c->n * sizeof(int));
            }
            return true;
        }
        int i = 0;
        for (; i < c->n; i++) {
            if (i == pos) continue;
            if (++idx[i] < counts[i]) break;
            idx[i] = 0;
        }
        if (i == c->n) return false;
    }
}

/* Counting propagator: once `limit` notes are rests, no other note may be. */
static bool revise_max_rests(SolverState *s, int ci) {
    const Model *m = model_of(s);
    int limit = m->cons[ci].param;
    int fixed[MELODY_MAX];
    int nfixed = 0;
    for (int i = 0; i < m->config.length; i++) {
        int v = m->pitch[i];
        if (solver_value(s, v) == PITCH_REST) fixed[nfixed++] = v;
    }
    if (nfixed < limit) return true;
    int nsrc = nfixed < SCOPE_MAX ? nfixed : SCOPE_MAX;
    if (nfixed > limit) {
        /* too many rests: empty the last one's domain */
        return remove_value(s, fixed[nfixed - 1], PITCH_REST, CID_REST, ci, fixed, nsrc,
                            NULL);
    }
    for (int i = 0; i < m->config.length; i++) {
        int v = m->pitch[i];
        if (solver_value(s, v) == PITCH_REST) continue;
        if (!remove_value(s, v, PITCH_REST, CID_REST, ci, fixed, nsrc, NULL)) return false;
    }
    return true;
}

static bool revise_constraint(SolverState *s, int ci) {
    const Model *m = model_of(s);
    const Constraint *c = &m->cons[ci];
    if (c->type == C_MAX_RESTS) return revise_max_rests(s, ci);

    int values[SCOPE_MAX][128];
    int counts[SCOPE_MAX];
    for (int i = 0; i < c->n; i++) counts[i] = domain_collect(&s->domains[c->vars[i]], values[i]);
    for (int pos = 0; pos < c->n; pos++) {
        bool removed = false;
        for (int k = 0; k < counts[pos]; k++) {
            int value = values[pos][k];
            if (has_support(s, ci, pos, value, values, counts)) continue;
            if (!remove_value(s, c->vars[pos], value, c->rule, ci, c->vars, c->n, NULL))
                return false;
            removed = true;
        }
        /* only this position's domain changed; later positions must not
         * lean on the values just removed */
        if (removed) counts[pos] = domain_collect(&s->domains[c->vars[pos]], values[pos]);
    }
    return true;
}

static bool revise_nogood(SolverState *s, int index) {
    const Nogood *g = &s->nogoods[index];
    int open = -1;
    for (int i = 0; i < g->n; i++) {
        const MidiDomain *d = &s->domains[g->vars[i]];
        if (!domain_contains(d, g->values[i])) return true; /* already broken */
        if (domain_count(d) == 1) continue;
        if (open >= 0) return true; /* two literals still open */
        open = i;
    }
    /* All literals but `open` hold; if none is open, the last must go. */
    int target = open >= 0 ? open : g->n - 1;
    return remove_value(s, g->vars[target], g->values[target], CID_LEARNED,
                        NOGOOD_CONSTRAINT(index), g->vars, g->n, NULL);
}

bool solver_propagate(SolverState *s) {
    const Model *m = model_of(s);
    if (s->failed) return false;
    while (s->qlen > 0) {
        int item = dequeue(s);
        bool ok;
        if (item < m->ncons) {
            if (s->skip[m->cons[item].rule]) continue;
            ok = revise_constraint(s, item);
        } else {
            ok = revise_nogood(s, item - m->ncons);
        }
        if (!ok) {
            clear_queue(s);
            return false;
        }
    }
    s->stats.propagations++;
    proof_append_entropy(&s->proof, solver_entropy(s));
    return true;
}

void solver_save(const SolverState *s, Snapshot *snap) {
    int n = model_of(s)->nvars;
    memcpy(snap->domains, s->domains, (size_t)n * sizeof(MidiDomain));
    memcpy(snap->reason, s->reason, (size_t)n * sizeof(LevelSet));
    memcpy(snap->last_event, s->last_event, (size_t)n * sizeof(int));
    snap->mark = proof_mark(&s->proof);
    snap->level = s->level;
    snap->failed = s->failed;
    snap->failed_variable = s->failed_variable;
    snap->rng = s->rng;
    snap->anneal_step = s->anneal_step;
}

void solver_restore(SolverState *s, const Snapshot *snap) {
    int n = model_of(s)->nvars;
    memcpy(s->domains, snap->domains, (size_t)n * sizeof(MidiDomain));
    memcpy(s->reason, snap->reason, (size_t)n * sizeof(LevelSet));
    memcpy(s->last_event, snap->last_event, (size_t)n * sizeof(int));
    proof_truncate(&s->proof, snap->mark);
    s->level = snap->level;
    s->failed = snap->failed;
    s->failed_variable = snap->failed_variable;
    s->rng = snap->rng;
    s->anneal_step = snap->anneal_step;
    clear_queue(s);
}

void solver_choice_costs(const SolverState *s, int var, const int *values, int n,
                         int *costs, int *breakdown_per_value) {
    const Model *m = model_of(s);
    for (int k = 0; k < n; k++) costs[k] = 0;
    if (breakdown_per_value != NULL)
        memset(breakdown_per_value, 0, (size_t)n * TERM_COUNT * sizeof(int));
    for (int a = m->terms_adj_start[var]; a < m->terms_adj_start[var + 1]; a++) {
        const Constraint *t = &m->terms[m->terms_adj[a]];
        int vals[SCOPE_MAX];
        int pos = -1;
        bool ready = true;
        for (int i = 0; i < t->n; i++) {
            if (t->vars[i] == var) {
                pos = i;
                continue;
            }
            vals[i] = solver_value(s, t->vars[i]);
            if (vals[i] < 0) ready = false;
        }
        if (!ready || pos < 0) continue;
        for (int k = 0; k < n; k++) {
            vals[pos] = values[k];
            int cost = term_cost(m, t, vals);
            costs[k] += cost;
            if (breakdown_per_value != NULL) breakdown_per_value[k * TERM_COUNT + t->rule] += cost;
        }
    }
}

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void sort_by_cost(int *values, int *costs, int n) {
    for (int i = 1; i < n; i++) {
        int value = values[i];
        int cost = costs[i];
        int j = i;
        while (j > 0 && costs[j - 1] > cost) {
            values[j] = values[j - 1];
            costs[j] = costs[j - 1];
            j--;
        }
        values[j] = value;
        costs[j] = cost;
    }
}

/* Draw an order without replacement, weighting each value by
 * exp(-(cost - cheapest) / temperature). */
static void sample_order(int *values, int *costs, int n, int temperature, uint32_t *rng) {
    int left_values[128];
    int left_costs[128];
    double weights[128];
    int left = n;
    int min_cost = n > 0 ? costs[0] : 0;
    for (int i = 1; i < n; i++) {
        if (costs[i] < min_cost) min_cost = costs[i];
    }
    memcpy(left_values, values, (size_t)n * sizeof(int));
    memcpy(left_costs, costs, (size_t)n * sizeof(int));
    for (int i = 0; i < n; i++)
        weights[i] = exp((double)(min_cost - costs[i]) / (double)temperature);
    for (int out = 0; out < n; out++) {
        double total = 0.0;
        for (int i = 0; i < left; i++) total += weights[i];
        double target = (double)xorshift32(rng) / 4294967296.0 * total;
        double acc = 0.0;
        int pick = left - 1;
        for (int i = 0; i < left; i++) {
            acc += weights[i];
            if (target < acc) {
                pick = i;
                break;
            }
        }
        values[out] = left_values[pick];
        costs[out] = left_costs[pick];
        left_values[pick] = left_values[left - 1];
        left_costs[pick] = left_costs[left - 1];
        weights[pick] = weights[left - 1];
        left--;
    }
}

static int current_temperature(SolverState *s) {
    const PieceConfig *c = &model_of(s)->config;
    int temp = c->temperature;
    if (c->anneal_ratio >= 1 && c->anneal_ratio <= 99) {
        temp = c->anneal_start;
        for (int i = 0; i < s->anneal_step && temp > 0; i++) temp = temp * c->anneal_ratio / 100;
        s->anneal_step++;
    } else if (c->anneal_steps > 0) {
        int k = s->anneal_step < c->anneal_steps ? s->anneal_step : c->anneal_steps;
        temp = c->anneal_start + (c->anneal_end - c->anneal_start) * k / c->anneal_steps;
        s->anneal_step++;
    }
    return temp;
}

/* Values in ascending order with a rest last, then by cost when energy
 * ordering is on. Returns the temperature used. */
static int order_values(SolverState *s, int var, Frame *f) {
    int *values = f->values;
    int *costs = f->costs;
    const Model *m = model_of(s);
    int n = domain_collect(&s->domains[var], values);
    if (m->vars[var].kind == VAR_PITCH && n > 0 && values[0] == PITCH_REST) {
        memmove(values, values + 1, (size_t)(n - 1) * sizeof(int));
        values[n - 1] = PITCH_REST;
    }
    solver_choice_costs(s, var, values, n, costs, NULL);
    f->n = n;
    int temp = 0;
    if (m->config.energy) {
        temp = current_temperature(s);
        if (temp > 0) {
            sample_order(values, costs, n, temp, &s->rng);
        } else {
            sort_by_cost(values, costs, n);
        }
    }
    if (s->guided) {
        /* replaying the best piece: its value goes first */
        for (int k = 1; k < n; k++) {
            if (values[k] != s->best[var]) continue;
            int value = values[k];
            int cost = costs[k];
            memmove(values + 1, values, (size_t)k * sizeof(int));
            memmove(costs + 1, costs, (size_t)k * sizeof(int));
            values[0] = value;
            costs[0] = cost;
        }
    }
    /* the explanation shows these parts next to the costs they add up to */
    int top = n < CAND_BREAKDOWN ? n : CAND_BREAKDOWN;
    int check[CAND_BREAKDOWN];
    solver_choice_costs(s, var, values, top, check, &f->breakdown[0][0]);
    return temp;
}

static int kind_tier(int kind) {
    switch (kind) {
    case VAR_KEY:
        return 0;
    case VAR_CHORD:
        return 1;
    default:
        return 2;
    }
}

static double choice_entropy(const SolverState *s, int var) {
    int values[128];
    int costs[128];
    int n = domain_collect(&s->domains[var], values);
    solver_choice_costs(s, var, values, n, costs, NULL);
    int min_cost = costs[0];
    for (int i = 1; i < n; i++) {
        if (costs[i] < min_cost) min_cost = costs[i];
    }
    int temp = model_of(s)->config.temperature > 0 ? model_of(s)->config.temperature : 1;
    double total = 0.0;
    double w[128];
    for (int i = 0; i < n; i++) {
        w[i] = exp((double)(min_cost - costs[i]) / (double)temp);
        total += w[i];
    }
    double h = 0.0;
    for (int i = 0; i < n; i++) {
        double p = w[i] / total;
        if (p > 0.0) h -= p * log2(p);
    }
    return h;
}

/* Expected entropy left after collapsing var, averaged over its values.
 * A value that fails propagation leaves nothing and counts as zero. */
static double expected_collapse(SolverState *s, int var) {
    Frame *scratch = &s->frames[VAR_MAX + 1];
    int values[128];
    int n = domain_collect(&s->domains[var], values);
    double sum = 0.0;
    solver_save(s, &scratch->snap);
    for (int k = 0; k < n; k++) {
        domain_clear(&s->domains[var]);
        domain_add(&s->domains[var], values[k]);
        solver_touch(s, var);
        if (solver_propagate(s)) sum += solver_entropy(s);
        solver_restore(s, &scratch->snap);
    }
    return n > 0 ? sum / n : 0.0;
}

static bool better_mrv(const SolverState *s, int a, int b) {
    int ca = domain_count(&s->domains[a]);
    int cb = domain_count(&s->domains[b]);
    if (ca != cb) return ca < cb;
    int da = model_of(s)->degree[a];
    int db = model_of(s)->degree[b];
    if (da != db) return da > db;
    return a < b;
}

static int pick_variable(SolverState *s) {
    const Model *m = model_of(s);
    int tier = 3;
    if (m->config.hierarchy) {
        for (int v = 0; v < m->nvars; v++) {
            if (domain_count(&s->domains[v]) > 1 && kind_tier(m->vars[v].kind) < tier)
                tier = kind_tier(m->vars[v].kind);
        }
    }
    int cand[VAR_MAX];
    int n = 0;
    for (int v = 0; v < m->nvars; v++) {
        if (domain_count(&s->domains[v]) <= 1) continue;
        if (m->config.hierarchy && kind_tier(m->vars[v].kind) != tier) continue;
        cand[n++] = v;
    }
    if (n == 0) return -1;

    int order = m->config.var_order;
    if (order == ORDER_INDEX) return cand[0];

    int best = cand[0];
    for (int i = 1; i < n; i++) {
        if (better_mrv(s, cand[i], best)) best = cand[i];
    }
    if (order == ORDER_MRV) return best;

    if (order == ORDER_ENTROPY) {
        double best_h = choice_entropy(s, best);
        for (int i = 0; i < n; i++) {
            double h = choice_entropy(s, cand[i]);
            if (h < best_h - 1e-9 || (fabs(h - best_h) <= 1e-9 && better_mrv(s, cand[i], best))) {
                best = cand[i];
                best_h = h;
            }
        }
        return best;
    }

    /* ORDER_COLLAPSE: look ahead on the few most constrained variables. */
    enum { LOOKAHEAD = 6 };
    int pool = n < LOOKAHEAD ? n : LOOKAHEAD;
    for (int i = 0; i < pool; i++) {
        for (int j = i + 1; j < n; j++) {
            if (better_mrv(s, cand[j], cand[i])) {
                int tmp = cand[i];
                cand[i] = cand[j];
                cand[j] = tmp;
            }
        }
    }
    double best_e = expected_collapse(s, cand[0]);
    best = cand[0];
    for (int i = 1; i < pool; i++) {
        double e = expected_collapse(s, cand[i]);
        if (e < best_e - 1e-9) {
            best = cand[i];
            best_e = e;
        }
    }
    return best;
}

static bool limit_reached(SolverState *s) {
    const PieceConfig *c = &model_of(s)->config;
    if (s->guided) return false; /* a replay walks straight to a known piece */
    if (s->optimizing && s->stats.nodes > s->window_end) return true;
    if (c->max_nodes > 0 && s->stats.nodes > c->max_nodes) {
        s->limit_hit = true;
        return true;
    }
    if (c->time_limit > 0 && (s->stats.nodes & 63) == 0) {
        double ms = 1000.0 * (double)(clock() - s->start) / CLOCKS_PER_SEC;
        if (ms > c->time_limit) {
            s->limit_hit = true;
            return true;
        }
    }
    return false;
}

static void decide(SolverState *s, int var, int value, int level, const Frame *f, int temp) {
    Decision *d = &s->decisions[level];
    d->var = var;
    d->value = value;
    d->temperature = temp;
    d->guided = s->guided;
    d->ncand = f->n < CAND_MAX ? f->n : CAND_MAX;
    memcpy(d->cand_values, f->values, (size_t)d->ncand * sizeof(int));
    memcpy(d->cand_costs, f->costs, (size_t)d->ncand * sizeof(int));
    memcpy(d->cand_breakdown, f->breakdown, sizeof(d->cand_breakdown));
    d->event = -1;

    domain_clear(&s->domains[var]);
    domain_add(&s->domains[var], value);
    levelset_add(&s->reason[var], level);
    s->level = level;

    ProofEvent *ev = proof_append(&s->proof, PROOF_DECIDE, var, value);
    if (ev != NULL) {
        ev->level = level;
        ev->reason = s->reason[var];
        snprintf(ev->message, sizeof(ev->message), "decision");
        d->event = s->proof.event_count - 1;
        s->last_event[var] = d->event;
    }
    s->stats.decisions++;
    solver_touch(s, var);
}

static void learn(SolverState *s, const LevelSet *conflict, int var, int value) {
    if (!model_of(s)->config.learn || s->nnogoods >= NOGOOD_CAP) return;
    Nogood g;
    g.n = 0;
    for (int l = 1; l <= s->level; l++) {
        if (!levelset_has(conflict, l)) continue;
        if (g.n >= NOGOOD_LITS - 1) return;
        g.vars[g.n] = s->decisions[l].var;
        g.values[g.n] = s->decisions[l].value;
        g.n++;
    }
    g.vars[g.n] = var;
    g.values[g.n] = value;
    g.n++;
    int index = s->nnogoods++;
    s->nogoods[index] = g;
    for (int i = 0; i < g.n; i++) {
        int v = g.vars[i];
        if (s->watch_n[v] == s->watch_cap[v]) {
            int cap = s->watch_cap[v] == 0 ? 8 : s->watch_cap[v] * 2;
            int *grown = realloc(s->watch[v], (size_t)cap * sizeof(int));
            if (grown == NULL) return;
            s->watch[v] = grown;
            s->watch_cap[v] = cap;
        }
        s->watch[v][s->watch_n[v]++] = index;
    }
    s->stats.learned++;
}

/* Cost of the soft terms whose variables are all collapsed. Costs are
 * never negative, so no completion can cost less. */
static int partial_energy(const SolverState *s) {
    const Model *m = model_of(s);
    int total = 0;
    for (int k = 0; k < m->nterms; k++) {
        const Constraint *t = &m->terms[k];
        int vals[SCOPE_MAX];
        bool ready = true;
        for (int i = 0; i < t->n && ready; i++) {
            vals[i] = solver_value(s, t->vars[i]);
            ready = vals[i] >= 0;
        }
        if (ready) total += term_cost(m, t, vals);
    }
    return total;
}

static void every_level(const SolverState *s, LevelSet *conflict) {
    levelset_clear(conflict);
    for (int l = 1; l <= s->level; l++) levelset_add(conflict, l);
}

static void note_first_piece(SolverState *s) {
    SearchCounts *f = &s->stats.first;
    f->nodes = s->stats.nodes;
    f->decisions = s->stats.decisions;
    f->backtracks = s->stats.backtracks;
    f->backjumps = s->stats.backjumps;
    f->learned = s->stats.learned;
}

static void record_piece(SolverState *s) {
    const Model *m = model_of(s);
    int values[VAR_MAX];
    solver_values(s, values);
    int energy = model_energy(m, values, NULL);
    if (s->stats.solutions == 0) {
        s->stats.first_energy = energy;
        note_first_piece(s);
    }
    s->stats.solutions++;
    s->bound = energy;
    memcpy(s->best, values, (size_t)m->nvars * sizeof(int));
}

/* Depth-first search with conflict-directed backjumping. On failure,
 * *conflict holds the decision levels the failure depends on. */
static int search(SolverState *s, LevelSet *conflict) {
    s->stats.nodes++;
    if (limit_reached(s)) return R_LIMIT;
    if (!solver_propagate(s)) {
        *conflict = s->reason[s->failed_variable];
        return R_FAIL;
    }
    if (s->optimizing && partial_energy(s) >= s->bound) {
        every_level(s, conflict); /* the bound rests on every decision */
        return R_FAIL;
    }
    int var = pick_variable(s);
    if (var < 0) {
        if (!s->optimizing) return R_SAT;
        record_piece(s);
        every_level(s, conflict);
        return R_FAIL;
    }

    int level = s->level + 1;
    Frame *f = &s->frames[level];
    int temp = order_values(s, var, f);
    for (int k = 0; k < f->n; k++) {
        int value = f->values[k];
        if (!domain_contains(&s->domains[var], value)) continue;
        solver_save(s, &f->snap);
        decide(s, var, value, level, f, temp);
        LevelSet child;
        levelset_clear(&child);
        int r = search(s, &child);
        if (r == R_SAT) return R_SAT;
        solver_restore(s, &f->snap);
        if (r == R_LIMIT) return R_LIMIT;
        s->stats.backtracks++;
        if (model_of(s)->config.backjump && !levelset_has(&child, level)) {
            s->stats.backjumps++;
            *conflict = child;
            return R_FAIL;
        }
        levelset_remove(&child, level);
        learn(s, &child, var, value);
        /* The value failed given the decisions in child: remove it here. */
        if (!remove_value(s, var, value, CID_REFUTED, -1, NULL, 0, &child) ||
            !solver_propagate(s)) {
            *conflict = s->reason[s->failed_variable];
            return R_FAIL;
        }
    }
    *conflict = s->reason[var];
    return R_FAIL;
}

static void enqueue_all(SolverState *s) {
    for (int c = 0; c < model_of(s)->ncons; c++) enqueue(s, c);
}

static void restart(SolverState *s, const Snapshot *root) {
    solver_restore(s, root);
    s->nnogoods = 0; /* some were learned under fixings or a bound */
    for (int v = 0; v < model_of(s)->nvars; v++) s->watch_n[v] = 0;
    enqueue_all(s);
}

/* Is var part of the window of melody notes [start, start + width),
 * wrapping around the end? Chords of the bars those notes start are
 * free too; keys never are. */
static bool in_window(const Model *m, int var, int start, int width) {
    const ModelVar *v = &m->vars[var];
    int length = m->config.length;
    if (v->kind == VAR_KEY) return false;
    for (int k = 0; k < width; k++) {
        int i = (start + k) % length;
        if (v->kind == VAR_CHORD ? v->index == i / 4 : v->index == i) return true;
    }
    return false;
}

/* Large neighbourhood search. The first piece becomes the best; then a
 * window of notes slides along the melody, everything outside it is
 * fixed to the best piece, and branch and bound looks for a strictly
 * cheaper piece inside. It stops when a full pass of windows finds
 * nothing or the budget runs out, then replays the best piece from the
 * root so the proof log and the decisions describe it. */
static int optimize(SolverState *s) {
    enum { WINDOW = 6, WINDOW_NODES = 1500 };
    const Model *m = model_of(s);
    Snapshot *root = &s->frames[0].snap;
    solver_save(s, root);
    LevelSet conflict;
    levelset_clear(&conflict);
    int r = search(s, &conflict);
    if (r != R_SAT) return r;
    record_piece(s);

    int length = m->config.length;
    int width = length < WINDOW ? length : WINDOW;
    int stride = width / 2 > 0 ? width / 2 : 1;
    int passes_needed = (length + stride - 1) / stride;
    int quiet = 0;
    for (int start = 0; quiet < passes_needed; start = (start + stride) % length) {
        if (s->stats.nodes - s->stats.first.nodes > s->optimize || s->limit_hit) break;
        restart(s, root);
        for (int v = 0; v < m->nvars; v++) {
            if (in_window(m, v, start, width) || !domain_contains(&s->domains[v], s->best[v]))
                continue;
            domain_clear(&s->domains[v]);
            domain_add(&s->domains[v], s->best[v]);
        }
        long before = s->stats.solutions;
        s->optimizing = true;
        s->window_end = s->stats.nodes + WINDOW_NODES;
        search(s, &conflict);
        s->optimizing = false;
        s->stats.windows++;
        quiet = s->stats.solutions > before ? 0 : quiet + 1;
    }
    s->stats.converged = quiet >= passes_needed;

    s->limit_hit = false;
    restart(s, root);
    s->guided = true;
    r = search(s, &conflict);
    s->guided = false;
    return r;
}

SolveStatus solver_solve(SolverState *s) {
    const PieceConfig *c = &model_of(s)->config;
    s->start = clock();
    s->rng = (uint32_t)c->seed;
    if (s->rng == 0) s->rng = 1;
    s->anneal_step = 0;
    s->limit_hit = false;
    LevelSet conflict;
    levelset_clear(&conflict);
    int r;
    if (s->failed) {
        r = R_FAIL;
    } else if (s->optimize > 0) {
        r = optimize(s);
    } else {
        r = search(s, &conflict);
        if (r == R_SAT) {
            int values[VAR_MAX];
            solver_values(s, values);
            s->stats.first_energy = model_energy(model_of(s), values, NULL);
            s->stats.solutions = 1;
            note_first_piece(s);
        }
    }
    s->stats.seconds = (double)(clock() - s->start) / CLOCKS_PER_SEC;
    s->result = r == R_SAT ? SOLVE_SAT : (r == R_LIMIT ? SOLVE_LIMIT : SOLVE_UNSAT);
    return (SolveStatus)s->result;
}

bool solver_unsat_core(const Model *m, int *rules, int max_rules, int *n,
                       bool *approximate) {
    *n = 0;
    *approximate = false;
    SolverState s;
    if (!solver_init(&s, m)) return false;
    s.optimize = 0;
    SolveStatus first = solver_solve(&s);
    solver_free(&s);
    if (first != SOLVE_UNSAT) return false;

    int cand[CID_MAX];
    int nc = 0;
    for (int r = 1; r < CID_MAX; r++) {
        if (r == CID_REFUTED || r == CID_LEARNED) continue;
        if (model_rule_used(m, r)) cand[nc++] = r;
    }

    /* Removing rules only adds solutions, so a rule found necessary stays
     * necessary; one deletion pass leaves every kept rule necessary. */
    unsigned char drop[CID_MAX];
    memset(drop, 0, sizeof(drop));
    for (int i = 0; i < nc; i++) {
        if (!solver_init(&s, m)) return false;
        s.optimize = 0; /* only satisfiability matters here */
        memcpy(s.skip, drop, sizeof(drop));
        s.skip[cand[i]] = 1;
        SolveStatus st = solver_solve(&s);
        solver_free(&s);
        if (st == SOLVE_UNSAT) {
            drop[cand[i]] = 1;
        } else if (st == SOLVE_LIMIT) {
            *approximate = true;
        }
    }
    for (int i = 0; i < nc && *n < max_rules; i++) {
        if (!drop[cand[i]]) rules[(*n)++] = cand[i];
    }
    return *n > 0;
}
