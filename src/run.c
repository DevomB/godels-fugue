#include "run.h"

#include "canon.h"

#include <stdio.h>
#include <string.h>

/* Solves one config on its own model and reports status and energy. */
static SolveStatus trial(const PieceConfig *config, int *energy, long *nodes,
                         int *pitches) {
    Model m;
    char err[160];
    *energy = 0;
    *nodes = 0;
    if (!model_build(&m, config, err, sizeof(err))) return SOLVE_UNSAT;
    SolverState s;
    SolveStatus st = SOLVE_UNSAT;
    if (solver_init(&s, &m)) {
        st = solver_solve(&s);
        *nodes = s.stats.nodes;
        if (st == SOLVE_SAT) {
            int values[VAR_MAX];
            solver_values(&s, values);
            *energy = model_energy(&m, values, NULL);
            if (pitches != NULL) {
                for (int i = 0; i < config->length; i++) pitches[i] = values[m.pitch[i]];
            }
        }
        solver_free(&s);
    }
    model_free(&m);
    return st;
}

/* The delay is the outermost variable: every value is solved and the
 * lowest-energy first solution wins, ties going to the shorter delay. */
static void search_delay(Run *run) {
    PieceConfig c = run->config;
    int best = -1;
    int best_energy = 0;
    int first_tried = -1;
    for (int d = c.delay_min; d <= c.delay_max; d++) {
        PieceConfig t = c;
        t.delay = d;
        t.delay_search = 0;
        t.optimize = 0; /* compare first solutions; only the winner is optimized */
        if (canon_span_config(&t) > SPAN_MAX) continue;
        if (t.modulate_at >= canon_span_config(&t)) continue;
        DelayTrial *dt = &run->delays[run->ndelays++];
        dt->delay = d;
        if (first_tried < 0) first_tried = d;
        dt->status = trial(&t, &dt->energy, &dt->nodes, NULL);
        if (dt->status == SOLVE_SAT && (best < 0 || dt->energy < best_energy)) {
            best = d;
            best_energy = dt->energy;
        }
    }
    /* nothing solved: report on a searched delay, not the unsearched one */
    run->config.delay = best >= 0 ? best : first_tried;
}

/* Does the config fix any melody note, by lock or by melody? */
static bool has_given_notes(const PieceConfig *c) {
    if (c->lock) return true;
    for (int i = 0; i < c->length && i < MELODY_MAX; i++) {
        if (c->melody[i] >= 0) return true;
    }
    return false;
}

bool run_piece(Run *run, const PieceConfig *config, char *err, size_t cap) {
    memset(run, 0, sizeof(*run));
    run->config = *config;
    if (config->delay_search) search_delay(run);

    if (!model_build(&run->model, &run->config, err, cap)) return false;
    if (!solver_init(&run->state, &run->model)) {
        snprintf(err, cap, "out of memory");
        model_free(&run->model);
        return false;
    }
    run->status = solver_solve(&run->state);
    solver_values(&run->state, run->values);
    if (run->status == SOLVE_SAT) {
        run->energy = model_energy(&run->model, run->values, run->breakdown);
        score_build(&run->score, &run->model, run->values);
    } else if (run->status == SOLVE_UNSAT) {
        solver_unsat_core(&run->model, run->core, CID_MAX, &run->core_n,
                          &run->core_approximate);
        run->nviolations = check_given(&run->model, run->violations, CHECK_MAX);
    }

    if (has_given_notes(&run->config)) {
        PieceConfig unlocked = run->config;
        unlocked.lock = 0;
        for (int i = 0; i < MELODY_MAX; i++) unlocked.melody[i] = -1;
        int energy;
        long nodes;
        run->counterfactual = true;
        run->unlocked_status = trial(&unlocked, &energy, &nodes, run->unlocked_pitch);
    }
    return true;
}

void run_free(Run *run) {
    solver_free(&run->state);
    model_free(&run->model);
}
