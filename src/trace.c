#include "trace.h"

#include "canon.h"
#include "explain.h"
#include "parts.h"
#include "theory.h"

#include <string.h>

const char *solve_status_name(SolveStatus status) {
    switch (status) {
    case SOLVE_SAT:
        return "solved";
    case SOLVE_UNSAT:
        return "unsat";
    default:
        return "limit";
    }
}

static const char *event_type_name(int type) {
    switch (type) {
    case PROOF_DECIDE:
        return "decide";
    case PROOF_FORCED:
        return "forced";
    default:
        return "remove";
    }
}

static void event_line(FILE *f, const Run *run, const ProofEvent *ev) {
    char name[16];
    char value[32];
    var_label(&run->model, ev->variable_id, name, sizeof(name));
    explain_label(&run->state, ev->variable_id, ev->value, value, sizeof(value));
    fprintf(f, "%s %s %s", event_type_name(ev->type), name, value);
    if (ev->type == PROOF_REMOVE) fprintf(f, " %s", rule_name(ev->rule));
    if (ev->type != PROOF_REMOVE || ev->level > 0) fprintf(f, " depth %d", ev->level);
}

bool trace_write_text(const char *path, const Run *run) {
    const ProofLog *log = &run->state.proof;
    FILE *f = fopen(path, "w");
    if (f == NULL) return false;
    int si = 0;
    for (int ei = 0; ei <= log->event_count; ei++) {
        while (si < log->sample_count && log->samples[si].after_event == ei) {
            fprintf(f, "entropy %.6f\n", log->samples[si].bits);
            si++;
        }
        if (ei == log->event_count) break;
        event_line(f, run, &log->events[ei]);
        fprintf(f, "\n");
    }
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

bool trace_write_dag(const char *path, const Run *run) {
    const ProofLog *log = &run->state.proof;
    FILE *f = fopen(path, "w");
    if (f == NULL) return false;
    for (int i = 0; i < log->event_count; i++) {
        const ProofEvent *ev = &log->events[i];
        fprintf(f, "event %d ", i);
        event_line(f, run, ev);
        fprintf(f, "\n");
        for (int p = 0; p < ev->parent_count; p++) {
            char name[16];
            var_label(&run->model, ev->parent_vars[p], name, sizeof(name));
            if (ev->parent_events[p] >= 0) fprintf(f, "  parent event %d\n", ev->parent_events[p]);
            if (ev->parent_values[p] >= 0) {
                char value[32];
                explain_label(&run->state, ev->parent_vars[p], ev->parent_values[p], value,
                              sizeof(value));
                fprintf(f, "  parent assign %s = %s\n", name, value);
            }
        }
    }
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

bool trace_write_entropy(const char *path, const Run *run) {
    const ProofLog *log = &run->state.proof;
    FILE *f = fopen(path, "w");
    if (f == NULL) return false;
    for (int i = 0; i < log->sample_count; i++) fprintf(f, "%.6f\n", log->samples[i].bits);
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

static void write_config(FILE *f, const PieceConfig *config) {
    fprintf(f, "\"config\":");
    config_write_json(f, config);
}

void trace_write_plan(FILE *f, const Model *m) {
    const PieceConfig *c = &m->config;
    fprintf(f, "\"players\":[");
    for (int v = 0; v < m->voices; v++) {
        const Part *p = part_get(m->part[v]);
        fprintf(f, "%s{\"voice\":%d,\"part\":", v ? "," : "", v + 1);
        json_string(f, p != NULL ? p->id : "auto");
        fprintf(f, ",\"name\":");
        char name[48];
        if (p != NULL) {
            snprintf(name, sizeof(name), "%s", p->name);
        } else {
            snprintf(name, sizeof(name), "Voice %d", v + 1);
        }
        json_string(f, name);
        fprintf(f, ",\"sample\":");
        json_string(f, p != NULL ? p->sample : "");
        fprintf(f, ",\"program\":%d,\"gain\":%d,\"struck\":%s,\"low\":%d,\"high\":%d,"
                   "\"transpose\":%d,\"delay\":%d}",
                p != NULL ? p->program : 0, p != NULL ? p->gain : 0,
                p == NULL || p->struck ? "true" : "false", m->low[v], m->high[v],
                canon_transpose(c, v), canon_voice_delay(c, v));
    }
    const FormPlan *form = &m->form;
    fprintf(f, "],\"form\":{\"head\":%d,\"climax\":%d,\"phrases\":[", form->head,
            form->climax);
    for (int k = 0; k < form->count; k++) {
        fprintf(f, "%s{\"start\":%d,\"end\":%d,\"role\":\"%s\"}", k ? "," : "",
                form->start[k], form->start[k + 1], role_name(form->role[k]));
    }
    fprintf(f, "]},\"instruments\":[");
    for (int id = 1; id < PART_COUNT; id++) {
        const Part *p = part_get(id);
        fprintf(f, "%s{\"id\":", id > 1 ? "," : "");
        json_string(f, p->id);
        fprintf(f, ",\"name\":");
        json_string(f, p->name);
        fprintf(f, ",\"low\":%d,\"high\":%d}", p->low, p->high);
    }
    fprintf(f, "],\"keepsNotes\":[");
    bool first = true;
    for (int i = 0; i < config_key_count(); i++) {
        if (!config_keeps_notes(config_key_name(i))) continue;
        fprintf(f, "%s", first ? "" : ",");
        json_string(f, config_key_name(i));
        first = false;
    }
    fprintf(f, "]");
}

/* The root of a chord as a pitch class: the triad note a third and a fifth
 * sit above. */
static int chord_root(int key, int degree) {
    int mask = key_triad_mask(key, degree);
    for (int r = 0; r < 12; r++) {
        if (!((mask >> r) & 1)) continue;
        bool third = ((mask >> ((r + 3) % 12)) & 1) || ((mask >> ((r + 4) % 12)) & 1);
        bool fifth = ((mask >> ((r + 6) % 12)) & 1) || ((mask >> ((r + 7) % 12)) & 1) ||
                     ((mask >> ((r + 8) % 12)) & 1);
        if (third && fifth) return r;
    }
    return -1;
}

static void write_times(FILE *f, const char *name, const double *t, int n) {
    fprintf(f, "\"%s\":[", name);
    for (int i = 0; i < n; i++) fprintf(f, "%s%.4f", i ? "," : "", t[i]);
    fprintf(f, "]");
}

/* How the piece is played (perform.h): the mood, the time map with and
 * without the closing broadening, the hold, and each voice's mix. Each
 * note's velocity and sounding share sit with the note in "score". */
static void write_perform(FILE *f, const Run *run) {
    const Performance *p = &run->perf;
    int span = run->model.span;
    fprintf(f, ",\"perform\":{\"mood\":\"%s\",\"hold\":%.4f,\"climax\":%d,",
            perform_mood_name(p->mood), p->hold, p->climax);
    write_times(f, "times", p->times, span + 1);
    fprintf(f, ",");
    write_times(f, "loopTimes", p->loop_times, span + 1);
    fprintf(f, ",\"volume\":[");
    for (int v = 0; v < run->model.voices; v++) fprintf(f, "%s%d", v ? "," : "", p->volume[v]);
    fprintf(f, "],\"gain\":[");
    for (int v = 0; v < run->model.voices; v++)
        fprintf(f, "%s%.4f", v ? "," : "", perform_gain(p, v));
    fprintf(f, "],\"pan\":[");
    for (int v = 0; v < run->model.voices; v++) fprintf(f, "%s%d", v ? "," : "", p->pan[v]);
    fprintf(f, "]}");
}

static void write_keys(FILE *f, const Run *run) {
    const Model *m = &run->model;
    fprintf(f, "\"keys\":[");
    for (int sec = 0; sec < m->nsections; sec++) {
        int key = run->values[m->key[sec]];
        char name[32];
        if (key >= 0) {
            key_name(key, name, sizeof(name));
        } else {
            snprintf(name, sizeof(name), "?");
        }
        fprintf(f, "%s{\"from\":%d,\"name\":", sec ? "," : "",
                sec == 0 ? 0 : run->config.modulate_at);
        json_string(f, name);
        fprintf(f, ",\"fifths\":%d}", key >= 0 ? key_fifths(key) : 0);
    }
    fprintf(f, "]");
}

static void write_score(FILE *f, const Run *run) {
    const Score *score = &run->score;
    const Model *m = &run->model;
    fprintf(f, "\"score\":[");
    for (int v = 0; v < score->voices && run->status == SOLVE_SAT; v++) {
        fprintf(f, "%s[", v ? "," : "");
        for (int k = 0; k < score->voice[v].count; k++) {
            const ScoreNote *n = &score->voice[v].notes[k];
            char name[16] = "rest";
            if (n->pitch != SOUND_REST) {
                int key = score->key[score->nsections > 1 && n->start >= score->modulate_at];
                key_pitch_name(key, n->pitch, name, sizeof(name));
            }
            const PerformNote *pn = &run->perf.note[v][k];
            fprintf(f, "%s{\"start\":%d,\"length\":%d,\"pitch\":%d,\"source\":%d,\"var\":%d,"
                       "\"vel\":%d,\"sound\":%.3f,\"label\":",
                    k ? "," : "", n->start, n->length,
                    n->pitch == SOUND_REST ? -1 : n->pitch, n->source,
                    n->source >= 0 ? m->pitch[n->source] : -1, pn->velocity, pn->sounding);
            json_string(f, name);
            fprintf(f, "}");
        }
        fprintf(f, "]");
    }
    fprintf(f, "]");
}

static void write_events(FILE *f, const Run *run) {
    const SolverState *s = &run->state;
    const ProofLog *log = &s->proof;
    fprintf(f, "\"events\":[");
    for (int i = 0; i < log->event_count; i++) {
        const ProofEvent *ev = &log->events[i];
        char name[16];
        char value[32];
        char why[EXPLAIN_TEXT_MAX] = "";
        var_label(&run->model, ev->variable_id, name, sizeof(name));
        explain_label(s, ev->variable_id, ev->value, value, sizeof(value));
        if (ev->type == PROOF_REMOVE) explain_removal(s, ev, why, sizeof(why));
        fprintf(f, "%s{\"id\":%d,\"type\":\"%s\",\"var\":", i ? "," : "", i,
                event_type_name(ev->type));
        json_string(f, name);
        fprintf(f, ",\"varId\":%d,\"value\":%d,\"label\":", ev->variable_id, ev->value);
        json_string(f, value);
        fprintf(f, ",\"rule\":");
        json_string(f, ev->type == PROOF_DECIDE ? "decision" : rule_name(ev->rule));
        fprintf(f, ",\"why\":");
        json_string(f, why);
        fprintf(f, ",\"depth\":%d,\"parents\":[", ev->level);
        for (int p = 0; p < ev->parent_count; p++) {
            var_label(&run->model, ev->parent_vars[p], name, sizeof(name));
            fprintf(f, "%s{\"event\":%d,\"var\":", p ? "," : "", ev->parent_events[p]);
            json_string(f, name);
            fprintf(f, ",\"varId\":%d,\"value\":%d}", ev->parent_vars[p], ev->parent_values[p]);
        }
        fprintf(f, "]}");
    }
    fprintf(f, "]");
}

static long forced_count(const ProofLog *log) {
    long n = 0;
    for (int i = 0; i < log->event_count; i++) {
        if (log->events[i].type == PROOF_FORCED) n++;
    }
    return n;
}

static void write_violations(FILE *f, const Run *run) {
    const Model *m = &run->model;
    fprintf(f, ",\"violations\":[");
    for (int i = 0; i < check_stored(run->nviolations); i++) {
        const Violation *v = &run->violations[i];
        const Constraint *c = &m->cons[v->con];
        char why[256];
        constraint_describe(m, c, why, sizeof(why));
        fprintf(f, "%s{\"rule\":", i ? "," : "");
        json_string(f, rule_name(c->rule));
        fprintf(f, ",\"why\":");
        json_string(f, why);
        fprintf(f, ",\"vars\":[");
        for (int k = 0; k < v->nvars; k++) {
            char name[16];
            var_label(m, v->vars[k], name, sizeof(name));
            fprintf(f, "%s", k ? "," : "");
            json_string(f, name);
        }
        fprintf(f, "]}");
    }
    /* the list stops at CHECK_MAX; this many more were found */
    fprintf(f, "],\"violationsMore\":%d", run->nviolations - check_stored(run->nviolations));
}

static void write_stats(FILE *f, const Run *run) {
    const SolverStats *st = &run->state.stats;
    fprintf(f,
            "\"stats\":{\"nodes\":%ld,\"decisions\":%ld,\"backtracks\":%ld,\"backjumps\":%ld,"
            "\"learned\":%ld,\"propagations\":%ld,\"removals\":%ld,\"forced\":%ld,"
            "\"seconds\":%.4f,\"entropy\":%.6f,\"variables\":%d,\"constraints\":%d,"
            "\"terms\":%d,\"pieces\":%ld,\"firstEnergy\":%d,\"firstNodes\":%ld,"
            "\"firstBacktracks\":%ld,\"windows\":%ld,\"converged\":%s,\"limited\":%s,"
            "\"rules\":{",
            st->nodes, st->decisions, st->backtracks, st->backjumps, st->learned,
            st->propagations, st->removals, forced_count(&run->state.proof), st->seconds,
            solver_entropy(&run->state), run->model.nvars, run->model.ncons, run->model.nterms,
            st->solutions, st->first_energy,
            st->first.nodes, st->first.backtracks, st->windows,
            st->converged ? "true" : "false", st->limited ? "true" : "false");
    bool first = true;
    for (int r = 1; r < CID_MAX; r++) {
        if (st->removals_by_rule[r] == 0) continue;
        fprintf(f, "%s", first ? "" : ",");
        json_string(f, rule_name(r));
        fprintf(f, ":%ld", st->removals_by_rule[r]);
        first = false;
    }
    fprintf(f, "}}");
}

/* The piece solved again without the given notes; pitches only when it solved. */
static void write_counterfactual(FILE *f, const Run *run) {
    const Model *m = &run->model;
    bool solved = run->unlocked_status == SOLVE_SAT;
    fprintf(f, ",\"counterfactual\":{\"unlockedStatus\":\"%s\",\"unlockedPitch\":[",
            solve_status_name(run->unlocked_status));
    for (int i = 0; solved && i < run->config.length; i++)
        fprintf(f, "%s%d", i ? "," : "", run->unlocked_pitch[i]);
    fprintf(f, "],\"changed\":[");
    bool first = true;
    for (int i = 0; solved && run->status == SOLVE_SAT && i < run->config.length; i++) {
        if (run->values[m->pitch[i]] == run->unlocked_pitch[i]) continue;
        fprintf(f, "%s%d", first ? "" : ",", i);
        first = false;
    }
    fprintf(f, "],\"unlockedLabel\":[");
    for (int i = 0; solved && i < run->config.length; i++) {
        char name[32];
        int pitch = run->unlocked_pitch[i];
        /* spelled in the unlocked piece's own key, which may not be the main run's */
        int key = run->unlocked_key[model_section_at(m, m->vars[m->pitch[i]].index)];
        if (pitch != PITCH_REST)
            key_pitch_name(key, pitch, name, sizeof(name));
        else
            explain_label(&run->state, m->pitch[i], pitch, name, sizeof(name));
        fprintf(f, "%s", i ? "," : "");
        json_string(f, name);
    }
    fprintf(f, "]}");
}

void trace_write_json(FILE *f, const Run *run) {
    const Model *m = &run->model;
    const SolverState *s = &run->state;
    fprintf(f, "{\"title\":\"" PROJECT_TITLE "\",\"status\":\"%s\",\"failed\":%d,",
            solve_status_name(run->status), s->failed ? s->failed_variable : -1);
    write_config(f, &run->config);
    int beat = config_beat_steps(&m->config);
    int bar_steps = config_bar_steps(&m->config);
    fprintf(f, ",\"span\":%d,\"voices\":%d,\"length\":%d,\"tempo\":%d,\"stepsPerBeat\":%d,"
               "\"stepsPerBar\":%d,\"strong\":[",
            m->span, m->voices, run->config.length, run->config.tempo, beat, bar_steps);
    bool first = true;
    for (int t = 0; t < m->span; t++) {
        if (!is_strong_step(t, beat, run->config.poly_meter)) continue;
        fprintf(f, "%s%d", first ? "" : ",", t);
        first = false;
    }
    fprintf(f, "],");
    trace_write_plan(f, m);
    fprintf(f, ",");
    write_keys(f, run);
    fprintf(f, ",");
    write_score(f, run);
    if (run->status == SOLVE_SAT) write_perform(f, run);

    fprintf(f, ",\"chords\":[");
    for (int b = 0; b < m->nbars && m->chord[b] >= 0; b++) {
        char name[16] = "?";
        int chord = run->values[m->chord[b]];
        int key = run->values[m->key[model_section_at(m, b * bar_steps)]];
        if (chord >= 0 && key >= 0) degree_name(key, chord, name, sizeof(name));
        fprintf(f, "%s{\"bar\":%d,\"var\":%d,\"root\":%d,\"label\":", b ? "," : "", b + 1,
                m->chord[b], chord >= 0 && key >= 0 ? chord_root(key, chord) : -1);
        json_string(f, name);
        fprintf(f, "}");
    }
    fprintf(f, "],\"variables\":[");
    ExplainContext ctx;
    explain_begin(&ctx, s);
    for (int v = 0; v < m->nvars; v++) {
        Explanation e;
        run_explanation(run, &ctx, v, &e);
        fprintf(f, "%s", v ? "," : "");
        explain_json(f, s, &e);
    }
    explain_end(&ctx);
    fprintf(f, "],");
    write_events(f, run);

    fprintf(f, ",\"entropy\":[");
    for (int i = 0; i < s->proof.sample_count; i++) {
        fprintf(f, "%s[%d,%.4f]", i ? "," : "", s->proof.samples[i].after_event,
                s->proof.samples[i].bits);
    }
    fprintf(f, "],\"energy\":{\"total\":%d,\"parts\":{", run->energy);
    first = true;
    for (int t = 0; t < TERM_COUNT; t++) {
        if (run->breakdown[t] == 0) continue;
        fprintf(f, "%s", first ? "" : ",");
        json_string(f, term_name(t));
        fprintf(f, ":%d", run->breakdown[t]);
        first = false;
    }
    fprintf(f, "}},");
    write_stats(f, run);

    fprintf(f, ",\"core\":[");
    for (int i = 0; i < run->core_n; i++) {
        fprintf(f, "%s", i ? "," : "");
        json_string(f, rule_name(run->core[i]));
    }
    fprintf(f, "],\"coreApproximate\":%s,\"delays\":[", run->core_approximate ? "true" : "false");
    for (int i = 0; i < run->ndelays; i++) {
        const DelayTrial *d = &run->delays[i];
        fprintf(f, "%s{\"delay\":%d,\"status\":\"%s\",\"energy\":%d,\"nodes\":%ld}", i ? "," : "",
                d->delay, solve_status_name(d->status), d->energy, d->nodes);
    }
    fprintf(f, "]");
    write_violations(f, run);
    if (run->counterfactual) write_counterfactual(f, run);
    fprintf(f, "}\n");
}

bool trace_save_json(const char *path, const Run *run) {
    FILE *f = fopen(path, "w");
    if (f == NULL) return false;
    trace_write_json(f, run);
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}
