#include "output.h"

#include "canon.h"
#include "explain.h"
#include "export.h"
#include "midi.h"
#include "page.h"
#include "theory.h"
#include "trace.h"

#include <errno.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

static int sibling_path(char *out, size_t cap, const char *base, const char *name) {
    const char *slash = strrchr(base, '/');
    const char *bslash = strrchr(base, '\\');
    const char *sep = slash;
    if (bslash != NULL && (sep == NULL || bslash > sep)) sep = bslash;
    size_t n = sep == NULL ? 0 : (size_t)(sep - base + 1);
    size_t m = strlen(name);
    if (n + m + 1 > cap) return 0;
    memcpy(out, base, n);
    memcpy(out + n, name, m + 1);
    return 1;
}

/* Creates each missing directory on the way to path. */
static void ensure_parent_dir(const char *path) {
    char buf[512];
    size_t n = strlen(path);
    if (n >= sizeof(buf)) return;
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (buf[i] != '/' && buf[i] != '\\') continue;
        char saved = buf[i];
        buf[i] = '\0';
#ifdef _WIN32
        if (_mkdir(buf) != 0 && errno != EEXIST) { /* writers report failure */
        }
#else
        if (mkdir(buf, 0755) != 0 && errno != EEXIST) { /* writers report failure */
        }
#endif
        buf[i] = saved;
    }
}

typedef bool (*Writer)(const char *path, const Run *run);

static bool write_beside(const char *base, const char *name, Writer writer, const Run *run) {
    char path[512];
    if (!sibling_path(path, sizeof(path), base, name)) return false;
    ensure_parent_dir(path);
    return writer(path, run);
}

static bool write_midi(const char *path, const Run *run) {
    return midi_write_score(path, &run->score);
}

static bool write_musicxml(const char *path, const Run *run) {
    return export_musicxml(path, &run->score);
}

static bool write_contour(const char *path, const Run *run) {
    return export_contour(path, &run->score);
}

static bool write_wav(const char *path, const Run *run) {
    return export_wav(path, &run->score, run->config.sample);
}

bool output_write_all(const Run *run, const OutputPaths *paths) {
    bool ok = true;
    ensure_parent_dir(paths->proof);
    ensure_parent_dir(paths->entropy);
    ensure_parent_dir(paths->midi);
    ok &= trace_write_text(paths->proof, run);
    {
        char dag[512];
        size_t n = strlen(paths->proof);
        if (n + 5 < sizeof(dag)) {
            memcpy(dag, paths->proof, n + 1);
            char *dot = strrchr(dag, '.');
            if (dot != NULL && strcmp(dot, ".txt") == 0) {
                strcpy(dot, ".dag");
            } else {
                memcpy(dag + n, ".dag", 5);
            }
            ok &= trace_write_dag(dag, run);
        }
    }
    ok &= trace_write_entropy(paths->entropy, run);
    ok &= write_beside(paths->proof, "proof.json", trace_save_json, run);
    ok &= write_beside(paths->midi, "report.txt", output_write_report, run);
    if (run->status != SOLVE_SAT) return ok;

    ensure_parent_dir(paths->midi);
    ok &= write_midi(paths->midi, run);
    ok &= write_beside(paths->midi, "score.musicxml", write_musicxml, run);
    ok &= write_beside(paths->midi, "contour.svg", write_contour, run);
    ok &= write_beside(paths->midi, "voices.wav", write_wav, run);
    ok &= write_beside(paths->midi, "score.html", page_write, run);
    ok &= write_beside(paths->midi, "explain.txt", output_write_explanations, run);
    return ok;
}

const char *output_length_name(int steps) {
    switch (steps) {
    case 1:
        return "q";
    case 2:
        return "h";
    case 3:
        return "h.";
    default:
        return "w";
    }
}

static void print_key_lines(FILE *out, const Run *run) {
    const Model *m = &run->model;
    for (int sec = 0; sec < m->nsections; sec++) {
        char name[32];
        int key = run->values[m->key[sec]];
        if (key < 0) continue;
        key_name(key, name, sizeof(name));
        if (sec == 0) {
            fprintf(out, "key: %s\n", name);
        } else {
            fprintf(out, "key 2: %s from step %d\n", name, run->config.modulate_at);
        }
    }
}

static void print_line(FILE *out, const char *label, const int *line, int n) {
    fprintf(out, "%s:", label);
    for (int t = 0; t < n; t++) {
        if (line[t] == SOUND_REST) {
            fprintf(out, " rest");
        } else {
            fprintf(out, " %d", line[t]);
        }
    }
    fprintf(out, "\n");
}

void output_print_summary(FILE *out, const Run *run) {
    const Model *m = &run->model;
    const PieceConfig *c = &run->config;
    const SolverStats *st = &run->state.stats;
    print_key_lines(out, run);
    int melody[MELODY_MAX];
    for (int i = 0; i < c->length; i++) {
        int p = run->values[m->pitch[i]];
        melody[i] = p == PITCH_REST ? SOUND_REST : p;
    }
    print_line(out, "melody", melody, c->length);
    for (int v = 1; v < m->voices; v++) {
        char label[24];
        snprintf(label, sizeof(label), "voice %d", v + 1);
        print_line(out, label, run->score.line[v], m->span);
    }
    if (c->rhythm) {
        fprintf(out, "rhythm:");
        for (int i = 0; i < c->length; i++) {
            if (run->values[m->pitch[i]] == PITCH_REST) {
                fprintf(out, " r");
            } else if (m->tie[i] >= 0 && run->values[m->tie[i]] == TIE_HOLD) {
                fprintf(out, " -");
            } else {
                int len = 1;
                while (i + len < c->length && m->tie[i + len] >= 0 &&
                       run->values[m->tie[i + len]] == TIE_HOLD)
                    len++;
                fprintf(out, " %s", output_length_name(len));
            }
        }
        fprintf(out, "\n");
    }
    if (c->harmony) {
        fprintf(out, "chords:");
        for (int b = 0; b < m->nbars; b++) {
            char name[16];
            int key = run->values[m->key[model_section_at(m, b * 4)]];
            degree_name(key, run->values[m->chord[b]], name, sizeof(name));
            fprintf(out, " %s", name);
        }
        fprintf(out, "\n");
    }
    if (c->delay_search) fprintf(out, "delay: %d\n", c->delay);
    fprintf(out, "backtracks: %ld\n", st->backtracks);
    fprintf(out, "search: %ld nodes, %ld decisions, %ld backjumps, %ld learned\n", st->nodes,
            st->decisions, st->backjumps, st->learned);
    fprintf(out, "entropy: %.6f\n", solver_entropy(&run->state));
    fprintf(out, "energy: %d", run->energy);
    bool first = true;
    for (int t = 0; t < TERM_COUNT; t++) {
        if (run->breakdown[t] == 0) continue;
        fprintf(out, "%s%s %d", first ? " (" : ", ", term_name(t), run->breakdown[t]);
        first = false;
    }
    fprintf(out, "%s\n", first ? "" : ")");
}

void output_print_failure(FILE *err, const Run *run) {
    const SolverState *s = &run->state;
    if (run->status == SOLVE_LIMIT) {
        fprintf(err, "search limit reached after %ld nodes and %.2f s "
                     "(max_nodes %d, time_limit %d ms)\n",
                s->stats.nodes, s->stats.seconds, run->config.max_nodes,
                run->config.time_limit);
        return;
    }
    if (run->core_n > 0) {
        fprintf(err, "core:");
        for (int i = 0; i < run->core_n; i++) fprintf(err, " %s", rule_name(run->core[i]));
        fprintf(err, "%s\n", run->core_approximate ? " (approximate: a trial hit the limit)" : "");
    }
    fprintf(err, "unsat: no melody satisfies every rule");
    if (s->failed && s->failed_variable >= 0) {
        char name[16];
        var_label(&run->model, s->failed_variable, name, sizeof(name));
        fprintf(err, "; %s has no value left", name);
    }
    fprintf(err, "\n");
    for (int i = s->proof.event_count - 1; i >= 0; i--) {
        const ProofEvent *e = &s->proof.events[i];
        if (e->type != PROOF_REMOVE) continue;
        char why[320];
        char name[16];
        char value[32];
        var_label(&run->model, e->variable_id, name, sizeof(name));
        value_label(&run->model, e->variable_id, e->value, value, sizeof(value));
        explain_removal(s, e, why, sizeof(why));
        fprintf(err, "last removal: %s %s by %s\n", name, value, why);
        break;
    }
}

void output_print_counterfactual(FILE *out, const Run *run) {
    const PieceConfig *c = &run->config;
    char value[32];
    value_label(&run->model, run->model.pitch[c->lock_index], c->lock_pitch, value,
                sizeof(value));
    fprintf(out, "counterfactual: lock x%d = %s\n", c->lock_index, value);
    if (run->unlocked_status != SOLVE_SAT) {
        fprintf(out, "%s without the lock\n",
                run->unlocked_status == SOLVE_UNSAT ? "unsat" : "search limit");
    } else if (run->status != SOLVE_SAT) {
        fprintf(out, "killed:");
        int shown = 0;
        for (int i = 0; i < run->core_n; i++) {
            if (run->core[i] == CID_LOCK) continue;
            fprintf(out, " %s", rule_name(run->core[i]));
            shown++;
        }
        fprintf(out, "%s\n", shown ? "" : " the lock");
    } else {
        int changed = 0;
        fprintf(out, "changed:");
        for (int i = 0; i < c->length; i++) {
            if (run->values[run->model.pitch[i]] != run->unlocked_pitch[i]) {
                fprintf(out, " %d", i);
                changed++;
            }
        }
        fprintf(out, "%s\n", changed ? "" : " none");
    }
}

bool output_write_report(const char *path, const Run *run) {
    const Model *m = &run->model;
    const SolverStats *st = &run->state.stats;
    FILE *f = fopen(path, "w");
    if (f == NULL) return false;
    fprintf(f, "Canon Collapse report\nstatus: %s\n", solve_status_name(run->status));
    print_key_lines(f, run);
    fprintf(f, "voices: %d  delay: %d  length: %d  span: %d steps\n", m->voices,
            run->config.delay, run->config.length, m->span);
    fprintf(f, "variables: %d  constraints: %d  soft terms: %d\n", m->nvars, m->ncons, m->nterms);
    fprintf(f, "nodes: %ld  decisions: %ld  backtracks: %ld  backjumps: %ld  learned: %ld\n",
            st->nodes, st->decisions, st->backtracks, st->backjumps, st->learned);
    long forced = 0;
    double peak = 0.0;
    for (int i = 0; i < run->state.proof.event_count; i++) {
        if (run->state.proof.events[i].type == PROOF_FORCED) forced++;
    }
    for (int i = 0; i < run->state.proof.sample_count; i++) {
        if (run->state.proof.samples[i].bits > peak) peak = run->state.proof.samples[i].bits;
    }
    fprintf(f, "forced collapses: %ld  propagations: %ld  removals: %ld  time: %.4f s\n",
            forced, st->propagations, st->removals, st->seconds);
    fprintf(f, "entropy: %.6f (peak after propagation %.6f)\n", solver_entropy(&run->state),
            peak);
    fprintf(f, "energy: %d\n", run->energy);
    for (int t = 0; t < TERM_COUNT; t++) {
        if (run->breakdown[t] != 0) fprintf(f, "  %-15s %d\n", term_name(t), run->breakdown[t]);
    }
    fprintf(f, "rule impact (values removed):\n");
    for (int r = 1; r < CID_MAX; r++) {
        if (st->removals_by_rule[r] != 0)
            fprintf(f, "  %-16s %ld\n", rule_name(r), st->removals_by_rule[r]);
    }
    if (run->core_n > 0) {
        fprintf(f, "core:");
        for (int i = 0; i < run->core_n; i++) fprintf(f, " %s", rule_name(run->core[i]));
        fprintf(f, "%s\n", run->core_approximate ? " (approximate)" : "");
    }
    if (run->ndelays > 0) {
        fprintf(f, "delays tried:\n");
        for (int i = 0; i < run->ndelays; i++) {
            const DelayTrial *d = &run->delays[i];
            fprintf(f, "  delay %d: %s", d->delay, solve_status_name(d->status));
            if (d->status == SOLVE_SAT) fprintf(f, ", energy %d", d->energy);
            fprintf(f, ", %ld nodes\n", d->nodes);
        }
    }
    fprintf(f, "config:\n");
    config_write(f, &run->config);
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

bool output_write_explanations(const char *path, const Run *run) {
    FILE *f = fopen(path, "w");
    if (f == NULL) return false;
    for (int v = 0; v < run->model.nvars; v++) {
        Explanation e;
        explain_var(&run->state, v, &e);
        explain_print(f, &run->state, &e);
        fprintf(f, "\n");
    }
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}
