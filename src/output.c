#include "output.h"

#include "canon.h"
#include "explain.h"
#include "export.h"
#include "midi.h"
#include "notation.h"
#include "page.h"
#include "theory.h"
#include "trace.h"

#include <ctype.h>
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

/* The proof DAG goes beside the proof trace: proof.txt gives proof.dag,
 * and any other name gains the extension. */
static bool dag_path(char *out, size_t cap, const char *proof) {
    size_t n = strlen(proof);
    if (n + 5 > cap) return false;
    memcpy(out, proof, n + 1);
    char *dot = strrchr(out, '.');
    if (dot != NULL && strcmp(dot, ".txt") == 0) {
        strcpy(dot, ".dag");
    } else {
        memcpy(out + n, ".dag", 5);
    }
    return true;
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

static bool write_lilypond(const char *path, const Run *run) {
    return export_lilypond(path, &run->score);
}

static bool write_abc(const char *path, const Run *run) {
    return export_abc(path, &run->score);
}

static bool write_contour(const char *path, const Run *run) {
    return export_contour(path, &run->score);
}

static bool write_wav(const char *path, const Run *run) {
    return export_wav(path, &run->score, run->config.instrument);
}

bool output_write_all(Run *run, const OutputPaths *paths) {
    bool ok = true;
    run_explain(run); /* proof.json, score.html and explain.txt share one pass */
    ensure_parent_dir(paths->proof);
    ensure_parent_dir(paths->entropy);
    ensure_parent_dir(paths->midi);
    ok &= trace_write_text(paths->proof, run);
    {
        char dag[512];
        ok &= dag_path(dag, sizeof(dag), paths->proof) && trace_write_dag(dag, run);
    }
    ok &= trace_write_entropy(paths->entropy, run);
    ok &= write_beside(paths->proof, "proof.json", trace_save_json, run);
    ok &= write_beside(paths->midi, "report.txt", output_write_report, run);
    /* the page and explanations also show why a failed run failed */
    ok &= write_beside(paths->midi, "score.html", page_write, run);
    ok &= write_beside(paths->midi, "explain.txt", output_write_explanations, run);
    if (run->status != SOLVE_SAT) {
        static const char *const stale[] = {"score.musicxml", "score.ly", "score.abc",
                                            "contour.svg", "voices.wav"};
        remove(paths->midi);
        for (size_t i = 0; i < sizeof(stale) / sizeof(stale[0]); i++) {
            char path[512];
            if (sibling_path(path, sizeof(path), paths->midi, stale[i])) remove(path);
        }
        return ok;
    }

    ok &= write_midi(paths->midi, run);
    ok &= write_beside(paths->midi, "score.musicxml", write_musicxml, run);
    ok &= write_beside(paths->midi, "score.ly", write_lilypond, run);
    ok &= write_beside(paths->midi, "score.abc", write_abc, run);
    ok &= write_beside(paths->midi, "contour.svg", write_contour, run);
    ok &= write_beside(paths->midi, "voices.wav", write_wav, run);
    return ok;
}

static void normalize_path(const char *in, char *out, size_t cap) {
    size_t i = 0;
    for (; in[i] != '\0' && i + 1 < cap; i++) {
        char ch = in[i] == '\\' ? '/' : in[i];
#ifdef _WIN32
        ch = (char)tolower((unsigned char)ch); /* Windows paths ignore case */
#endif
        out[i] = ch;
    }
    out[i] = '\0';
}

bool output_paths_distinct(const OutputPaths *paths, char *err, size_t cap) {
    static const char *const beside_midi[] = {"score.musicxml", "score.ly",    "score.abc",
                                              "contour.svg",    "voices.wav",  "score.html",
                                              "explain.txt",    "report.txt"};
    char all[13][512];
    int n = 0;
    snprintf(all[n++], sizeof(all[0]), "%s", paths->midi);
    snprintf(all[n++], sizeof(all[0]), "%s", paths->proof);
    snprintf(all[n++], sizeof(all[0]), "%s", paths->entropy);
    for (size_t i = 0; i < sizeof(beside_midi) / sizeof(beside_midi[0]); i++) {
        if (!sibling_path(all[n], sizeof(all[0]), paths->midi, beside_midi[i])) return true;
        n++;
    }
    if (sibling_path(all[n], sizeof(all[0]), paths->proof, "proof.json")) n++;
    if (dag_path(all[n], sizeof(all[0]), paths->proof)) n++;
    for (int i = 0; i < n; i++) {
        char a[512];
        normalize_path(all[i], a, sizeof(a));
        for (int j = i + 1; j < n; j++) {
            char b[512];
            normalize_path(all[j], b, sizeof(b));
            if (strcmp(a, b) == 0) {
                snprintf(err, cap, "two output files would both be written to %s", all[i]);
                return false;
            }
        }
    }
    return true;
}

const char *output_value_name(int eighths) {
    switch (eighths) {
    case 1:
        return "e";
    case 2:
        return "q";
    case 3:
        return "q.";
    case 4:
        return "h";
    case 6:
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

static void print_optimize_line(FILE *out, const Run *run) {
    const SolverStats *st = &run->state.stats;
    if (run->status != SOLVE_SAT || run->config.optimize <= 0) return;
    long improvements = st->solutions - 1;
    fprintf(out, "optimize: energy %d -> %d, %ld improvement%s in %ld window%s and %ld more "
                 "nodes, %s\n",
            st->first_energy, run->energy, improvements, improvements == 1 ? "" : "s",
            st->windows, st->windows == 1 ? "" : "s", st->nodes - st->first.nodes,
            st->converged     ? "no window improves it"
            : st->windows_cut ? "a full pass found nothing cheaper, some windows cut short"
                              : "budget spent");
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
                /* values tied with ~ when no single one fits */
                fprintf(out, " ");
                for (int left = len; left > 0;) {
                    int part = score_written_steps(&run->score, left);
                    fprintf(out, "%s%s", left < len ? "~" : "",
                            output_value_name(score_eighths(&run->score, part)));
                    left -= part;
                }
            }
        }
        fprintf(out, "\n");
    }
    if (c->harmony) {
        fprintf(out, "chords:");
        int bar_steps = config_bar_steps(&m->config);
        for (int b = 0; b < m->nbars; b++) {
            char name[16];
            int key = run->values[m->key[model_section_at(m, b * bar_steps)]];
            degree_name(key, run->values[m->chord[b]], name, sizeof(name));
            fprintf(out, " %s", name);
        }
        fprintf(out, "\n");
    }
    if (c->delay_search) fprintf(out, "delay: %d\n", c->delay);
    /* effort to the first piece; the optimizer's share is on its own line */
    fprintf(out, "backtracks: %ld\n", st->first.backtracks);
    fprintf(out, "search: %ld nodes, %ld decisions, %ld backjumps, %ld learned\n",
            st->first.nodes, st->first.decisions, st->first.backjumps, st->first.learned);
    print_optimize_line(out, run);
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
    check_print(err, &run->model, run->violations, run->nviolations);
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
    /* the last removal emptied the failed variable; after a search that
     * backed out to the root there is no such removal to show */
    for (int i = s->proof.event_count - 1; s->failed && i >= 0; i--) {
        const ProofEvent *e = &s->proof.events[i];
        if (e->type != PROOF_REMOVE) continue;
        char why[EXPLAIN_TEXT_MAX];
        char name[16];
        char value[32];
        var_label(&run->model, e->variable_id, name, sizeof(name));
        explain_label(s, e->variable_id, e->value, value, sizeof(value));
        explain_removal(s, e, why, sizeof(why));
        fprintf(err, "last removal: %s %s by %s\n", name, value, why);
        break;
    }
}

void output_print_counterfactual(FILE *out, const Run *run) {
    const PieceConfig *c = &run->config;
    char value[32];
    if (c->lock) {
        explain_label(&run->state, run->model.pitch[c->lock_index], c->lock_pitch, value,
                      sizeof(value));
        fprintf(out, "counterfactual: lock x%d = %s\n", c->lock_index, value);
    }
    bool first = true;
    for (int i = 0; i < c->length; i++) {
        if (c->melody[i] < 0) continue;
        explain_label(&run->state, run->model.pitch[i], c->melody[i], value, sizeof(value));
        fprintf(out, "%sx%d = %s", first ? "counterfactual: given " : ", ", i, value);
        first = false;
    }
    if (!first) fprintf(out, "\n");
    if (run->unlocked_status != SOLVE_SAT) {
        fprintf(out, "%s without the given notes\n",
                run->unlocked_status == SOLVE_UNSAT ? "unsat" : "search limit");
    } else if (run->status == SOLVE_LIMIT) {
        fprintf(out, "inconclusive: the search hit its limit with the given notes\n");
    } else if (run->status != SOLVE_SAT) {
        fprintf(out, "killed:");
        int shown = 0;
        for (int i = 0; i < run->core_n; i++) {
            if (run->core[i] == CID_LOCK) continue;
            fprintf(out, " %s", rule_name(run->core[i]));
            shown++;
        }
        fprintf(out, "%s\n", shown ? "" : " the given notes alone");
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
    fprintf(f, "to the first piece: nodes: %ld  decisions: %ld  backtracks: %ld  "
               "backjumps: %ld  learned: %ld\n",
            st->first.nodes, st->first.decisions, st->first.backtracks, st->first.backjumps,
            st->first.learned);
    fprintf(f, "whole search: nodes: %ld  decisions: %ld  backtracks: %ld  backjumps: %ld  "
               "learned: %ld\n",
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
    print_optimize_line(f, run);
    fprintf(f, "entropy: %.6f (peak after propagation %.6f)\n", solver_entropy(&run->state),
            peak);
    fprintf(f, "energy: %d\n", run->energy);
    for (int t = 0; t < TERM_COUNT; t++) {
        if (run->breakdown[t] != 0) fprintf(f, "  %-15s %d\n", term_name(t), run->breakdown[t]);
    }
    fprintf(f, "values removed during the whole search, by rule:\n");
    for (int r = 1; r < CID_MAX; r++) {
        if (st->removals_by_rule[r] != 0)
            fprintf(f, "  %-16s %ld\n", rule_name(r), st->removals_by_rule[r]);
    }
    if (run->core_n > 0) {
        fprintf(f, "core:");
        for (int i = 0; i < run->core_n; i++) fprintf(f, " %s", rule_name(run->core[i]));
        fprintf(f, "%s\n", run->core_approximate ? " (approximate)" : "");
    }
    if (run->nviolations > 0) {
        fprintf(f, "violations:\n");
        for (int i = 0; i < check_stored(run->nviolations); i++) {
            char text[CHECK_TEXT_MAX];
            check_text(&run->model, &run->violations[i], text, sizeof(text));
            fprintf(f, "  %s\n", text);
        }
        if (run->nviolations > CHECK_MAX)
            fprintf(f, "  ... and %d more\n", run->nviolations - CHECK_MAX);
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
    ExplainContext ctx;
    explain_begin(&ctx, &run->state);
    for (int v = 0; v < run->model.nvars; v++) {
        Explanation e;
        run_explanation(run, &ctx, v, &e);
        explain_print(f, &run->state, &e);
        fprintf(f, "\n");
    }
    explain_end(&ctx);
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}
