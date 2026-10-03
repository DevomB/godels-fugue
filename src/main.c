#include "analyze.h"
#include "canon.h"
#include "check.h"
#include "config.h"
#include "corpus.h"
#include "explain.h"
#include "midi_read.h"
#include "output.h"
#include "run.h"
#include "sat.h"
#include "theory.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CANON_COLLAPSE_VERSION "0.2.0"

enum { EXIT_UNSAT = 1, EXIT_TOO_LARGE = 2, EXIT_LIMIT = 3, SETS_MAX = 64 };

static void usage(FILE *f) {
    fprintf(f,
            "usage: canon-collapse [options]\n"
            "\n"
            "  --config FILE        load keys from FILE (\"key value\" lines, or JSON if\n"
            "                       the name ends in .json)\n"
            "  --preset NAME        apply a style preset before the config file\n"
            "  --set KEY=VALUE      override one key after the config file (repeatable)\n"
            "  --lock INDEX PITCH   fix melody note INDEX to MIDI PITCH\n"
            "  --melody-midi FILE   take the melody and its length from the first track\n"
            "                       of a MIDI file with notes, a step per quarter note\n"
            "  --explain VAR        print why a variable has its value: a melody index\n"
            "                       such as 5, or x5, tie5, chord2, key, key2\n"
            "  --check              only check the given notes against the rules: print\n"
            "                       each broken rule, or \"violations: none\"\n"
            "  --out FILE           MIDI path; the other score files go beside it\n"
            "                       (default output/canon.mid)\n"
            "  --proof FILE         proof trace path (default output/proof.txt)\n"
            "  --entropy FILE       entropy log path (default output/entropy.txt)\n"
            "  --corpus DIR         suggest pitch-class weights from files of MIDI pitches\n"
            "                       and MIDI files (.mid, .midi)\n"
            "  --apply-weights      use the suggested corpus weights\n"
            "  --sat                check the rules with the SAT backend instead\n"
            "  --count N            count the pieces the rules allow, up to N\n"
            "  --sensitivity        for each melody note, the values a piece can give it\n"
            "  --max-nodes N        give up after N search nodes (0 = no limit)\n"
            "  --time-limit MS      give up after MS milliseconds (0 = no limit)\n"
            "  --list-config        print every config key (--markdown for a table)\n"
            "  --list-presets       print the style presets\n"
            "  --version            print the version\n"
            "\n"
            "Exit status: 0 solved, 1 unsatisfiable or bad input, 2 too large for\n"
            "--sat, 3 search limit reached. --count and --sensitivity exit 0 with\n"
            "an answer, even none, and 3 when the search limit cut them short.\n");
}

static int find_var(const Model *m, const char *name) {
    char *end = NULL;
    long index = strtol(name, &end, 10);
    if (end != name && *end == '\0') {
        return index >= 0 && index < m->config.length ? m->pitch[index] : -1;
    }
    for (int v = 0; v < m->nvars; v++) {
        char label[16];
        var_label(m, v, label, sizeof(label));
        if (strcmp(label, name) == 0) return v;
    }
    return -1;
}

static int run_sat(const PieceConfig *config) {
    Model m;
    char err[200];
    if (!model_build(&m, config, err, sizeof(err))) {
        fprintf(stderr, "%s\n", err);
        return EXIT_UNSAT;
    }
    int values[VAR_MAX];
    int rc = sat_solve(&m, values, config->max_nodes);
    int exit_code = 0;
    if (rc == SAT_TOO_LARGE) {
        fprintf(stderr, "sat: too large\n");
        exit_code = EXIT_TOO_LARGE;
    } else if (rc == SAT_LIMIT) {
        fprintf(stderr, "sat: decision limit reached\n");
        exit_code = EXIT_LIMIT;
    } else if (rc == SAT_UNSAT) {
        printf("unsat\n");
        exit_code = EXIT_UNSAT;
    } else {
        printf("melody:");
        for (int i = 0; i < config->length; i++) {
            int p = values[m.pitch[i]];
            if (p == PITCH_REST) {
                printf(" rest");
            } else {
                printf(" %d", p);
            }
        }
        printf("\n");
    }
    model_free(&m);
    return exit_code;
}

static int run_analysis(const PieceConfig *config, long count_max, bool sensitivity) {
    Model m;
    char err[200];
    if (!model_build(&m, config, err, sizeof(err))) {
        fprintf(stderr, "%s\n", err);
        return EXIT_UNSAT;
    }
    int exit_code = 0;
    if (count_max > 0) {
        Count c;
        if (!analyze_count(&m, count_max, &c)) {
            fprintf(stderr, "out of memory\n");
            exit_code = EXIT_UNSAT;
        } else {
            analyze_print_count(stdout, &c);
            if (c.limit_hit) exit_code = EXIT_LIMIT;
        }
    }
    if (sensitivity && exit_code != EXIT_UNSAT) {
        NoteSensitivity *notes = malloc(sizeof(NoteSensitivity) * MELODY_MAX);
        if (notes == NULL || !analyze_sensitivity(&m, notes)) {
            fprintf(stderr, "out of memory\n");
            exit_code = EXIT_UNSAT;
        } else {
            analyze_print_sensitivity(stdout, &m, notes);
            for (int i = 0; i < config->length; i++) {
                if (notes[i].unknown > 0) exit_code = EXIT_LIMIT;
            }
        }
        free(notes);
    }
    model_free(&m);
    return exit_code;
}

/* An inversion or mirror axis that maps the key onto other notes leaves
 * the scale rule few pitches to choose from, and an odd-length mirror's
 * middle note is the axis itself; say so, and suggest the axis better
 * (-1 for none) that keeps them all. */
static void warn_axis(int key, const char *verb, int axis, int kept, bool middle_off,
                      int better) {
    int size = key_scale_size(key);
    if (kept == size && !middle_off) return;
    char name[32];
    key_name(key, name, sizeof(name));
    fprintf(stderr, "note: %s around axis %d", verb, axis);
    if (kept < size) fprintf(stderr, " keeps %d of %d notes of %s in the key", kept, size, name);
    if (middle_off) {
        fprintf(stderr, "%s the middle note must be the axis, which is not in %s",
                kept < size ? ";" : ":", name);
    }
    if (better >= 0) {
        fprintf(stderr, "; axis %d keeps them all\n", better);
    } else {
        fprintf(stderr, "; no axis keeps them all\n");
    }
}

/* Judges the given notes without a search; exit 1 if any rule breaks. */
static int run_check(const PieceConfig *config) {
    Model m;
    char err[200];
    if (!model_build(&m, config, err, sizeof(err))) {
        fprintf(stderr, "%s\n", err);
        return EXIT_UNSAT;
    }
    if (config->delay_search) {
        fprintf(stderr, "note: --check judges the notes at delay %d only\n", config->delay);
    }
    Violation v[CHECK_MAX];
    int n = check_given(&m, v, CHECK_MAX);
    if (n == 0) printf("violations: none\n");
    check_print(stdout, &m, v, n);
    model_free(&m);
    return n > 0 ? EXIT_UNSAT : 0;
}

/* The melody of a MIDI file's first track with notes, one step per quarter
 * note, gives every note of the melody and its length. */
static bool melody_from_midi(PieceConfig *c, const char *path, char *err, size_t cap) {
    MidiFile file;
    if (!midi_read_file(&file, path, err, cap)) return false;
    int steps[MELODY_MAX];
    int track = midi_first_track(&file);
    int n = track < 0 ? 0 : midi_track_steps(&file, track, steps, MELODY_MAX);
    midi_file_free(&file);
    if (n == 0) {
        snprintf(err, cap, "%s: no notes", path);
        return false;
    }
    if (n > MELODY_MAX) {
        snprintf(err, cap, "%s: the melody is %d steps long; at most %d fit", path, n,
                 MELODY_MAX);
        return false;
    }
    for (int i = 0; i < n && !c->rhythm; i++) {
        if (steps[i] == PITCH_REST) {
            snprintf(err, cap, "%s: the melody rests at step %d, and rests need rhythm: "
                               "add --set rhythm=1",
                     path, i);
            return false;
        }
    }
    c->length = n;
    for (int i = 0; i < MELODY_MAX; i++) c->melody[i] = i < n ? steps[i] : -1;
    return true;
}

/* The notes of the key every follower keeps in it, at the fewest, with
 * the canon inverted around axis; transposition, diatonic or not, counts. */
static int followers_kept(const PieceConfig *c, int key, int axis) {
    PieceConfig t = *c;
    t.axis = axis;
    int kept = key_scale_size(key);
    for (int v = 1; v < config_voice_count(&t); v++) {
        int k = 0;
        for (int pc = 0; pc < 12; pc++) {
            if (key_has_pitch(key, pc) && key_has_pitch(key, canon_sounding(&t, v, 60 + pc)))
                k++;
        }
        if (k < kept) kept = k;
    }
    return kept;
}

static int axis_kept(const PieceConfig *c, int key, bool mirror, int axis) {
    return mirror ? inversion_kept(key, axis, 0) : followers_kept(c, key, axis);
}

/* The axis nearest the configured one that keeps every note of the key, or
 * -1. A mirror axis must lie in range_low..range_high, and in the key when
 * the length is odd, since the middle note is the axis. */
static int better_axis(const PieceConfig *c, int key, bool mirror) {
    int near = mirror ? c->mirror_axis : c->axis;
    int lo = mirror ? c->range_low : 0;
    int hi = mirror ? c->range_high : 127;
    bool in_key = mirror && c->length % 2 == 1;
    for (int d = 0; d < 128; d++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            int axis = near + sign * d;
            if (axis >= lo && axis <= hi && (!in_key || key_has_pitch(key, axis)) &&
                axis_kept(c, key, mirror, axis) == key_scale_size(key))
                return axis;
            if (d == 0) break;
        }
    }
    return -1;
}

static void warn_about_axis(const PieceConfig *c) {
    if (c->key < 0 || c->mode < 0) return;
    int key = key_id(c->key, c->mode);
    if (c->invert) {
        warn_axis(key, "inverting", c->axis, followers_kept(c, key, c->axis), false,
                  better_axis(c, key, false));
    }
    if (c->mirror) {
        bool middle_off = c->length % 2 == 1 && !key_has_pitch(key, c->mirror_axis);
        warn_axis(key, "mirroring the melody", c->mirror_axis,
                  inversion_kept(key, c->mirror_axis, 0), middle_off, better_axis(c, key, true));
    }
}

static bool need_value(int i, int argc, int count, const char *flag) {
    if (i + count < argc) return true;
    fprintf(stderr, "missing value for %s\n", flag);
    return false;
}

int main(int argc, char **argv) {
    PieceConfig config;
    config_defaults(&config);
    const char *config_path = NULL;
    const char *preset = NULL;
    const char *sets[SETS_MAX];
    int nsets = 0;
    OutputPaths paths = {"output/canon.mid", "output/proof.txt", "output/entropy.txt"};
    const char *corpus_dir = NULL;
    const char *explain = NULL;
    const char *lock_index = NULL;
    const char *lock_pitch = NULL;
    const char *melody_midi = NULL;
    const char *max_nodes = NULL;
    const char *time_limit = NULL;
    bool apply_weights = false;
    bool sat_mode = false;
    bool check_mode = false;
    const char *count_text = NULL;
    bool sensitivity = false;
    bool markdown = false;
    bool list_config = false;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(a, "--version") == 0) {
            printf("canon-collapse %s\n", CANON_COLLAPSE_VERSION);
            return 0;
        } else if (strcmp(a, "--list-presets") == 0) {
            config_print_presets(stdout);
            return 0;
        } else if (strcmp(a, "--list-config") == 0) {
            list_config = true;
        } else if (strcmp(a, "--markdown") == 0) {
            markdown = true;
        } else if (strcmp(a, "--apply-weights") == 0) {
            apply_weights = true;
        } else if (strcmp(a, "--sat") == 0) {
            sat_mode = true;
        } else if (strcmp(a, "--check") == 0) {
            check_mode = true;
        } else if (strcmp(a, "--sensitivity") == 0) {
            sensitivity = true;
        } else if (strcmp(a, "--lock") == 0) {
            if (!need_value(i, argc, 2, a)) return EXIT_UNSAT;
            lock_index = argv[++i];
            lock_pitch = argv[++i];
        } else {
            const char **slot = NULL;
            if (strcmp(a, "--config") == 0) slot = &config_path;
            if (strcmp(a, "--preset") == 0) slot = &preset;
            if (strcmp(a, "--out") == 0) slot = &paths.midi;
            if (strcmp(a, "--proof") == 0) slot = &paths.proof;
            if (strcmp(a, "--entropy") == 0) slot = &paths.entropy;
            if (strcmp(a, "--melody-midi") == 0) slot = &melody_midi;
            if (strcmp(a, "--corpus") == 0) slot = &corpus_dir;
            if (strcmp(a, "--explain") == 0) slot = &explain;
            if (strcmp(a, "--max-nodes") == 0) slot = &max_nodes;
            if (strcmp(a, "--time-limit") == 0) slot = &time_limit;
            if (strcmp(a, "--count") == 0) slot = &count_text;
            if (strcmp(a, "--set") == 0) {
                if (nsets >= SETS_MAX) {
                    fprintf(stderr, "too many --set options\n");
                    return EXIT_UNSAT;
                }
                slot = &sets[nsets++];
            }
            if (slot == NULL) {
                fprintf(stderr, "unknown argument: %s\n", a);
                usage(stderr);
                return EXIT_UNSAT;
            }
            if (!need_value(i, argc, 1, a)) return EXIT_UNSAT;
            *slot = argv[++i];
        }
    }
    if (list_config) {
        config_print_reference(stdout, markdown);
        return 0;
    }
    /* --check judges the given notes alone and --explain a solved piece, so
     * a mode either would ignore is refused rather than dropped */
    const char *mode = count_text != NULL ? "--count"
                       : sensitivity      ? "--sensitivity"
                       : sat_mode         ? "--sat"
                                          : NULL;
    if (check_mode && (mode != NULL || explain != NULL || corpus_dir != NULL)) {
        fprintf(stderr, "--check cannot be combined with %s\n",
                mode != NULL ? mode : explain != NULL ? "--explain" : "--corpus");
        return EXIT_UNSAT;
    }
    if (explain != NULL && mode != NULL) {
        fprintf(stderr, "--explain needs a solved piece; it cannot be combined with %s\n", mode);
        return EXIT_UNSAT;
    }

    char err[300];
    if (preset != NULL && !config_apply_preset(&config, preset, err, sizeof(err))) {
        fprintf(stderr, "%s\n", err);
        return EXIT_UNSAT;
    }
    if (config_path != NULL && !config_load_file(&config, config_path, err, sizeof(err))) {
        fprintf(stderr, "%s\n", err);
        return EXIT_UNSAT;
    }
    for (int i = 0; i < nsets; i++) {
        if (!config_assign(&config, sets[i], err, sizeof(err))) {
            fprintf(stderr, "%s\n", err);
            return EXIT_UNSAT;
        }
    }
    if (melody_midi != NULL && !melody_from_midi(&config, melody_midi, err, sizeof(err))) {
        fprintf(stderr, "%s\n", err);
        return EXIT_UNSAT;
    }
    if (lock_index != NULL) {
        if (!config_set(&config, "lock_index", lock_index, err, sizeof(err)) ||
            !config_set(&config, "lock_pitch", lock_pitch, err, sizeof(err))) {
            fprintf(stderr, "%s\n", err);
            return EXIT_UNSAT;
        }
        config.lock = 1;
    }
    if ((max_nodes != NULL && !config_set(&config, "max_nodes", max_nodes, err, sizeof(err))) ||
        (time_limit != NULL && !config_set(&config, "time_limit", time_limit, err, sizeof(err)))) {
        fprintf(stderr, "%s\n", err);
        return EXIT_UNSAT;
    }
    if (!config_validate(&config, err, sizeof(err))) {
        fprintf(stderr, "%s\n", err);
        return EXIT_UNSAT;
    }
    long count_max = 0;
    if (count_text != NULL) {
        char *end = NULL;
        errno = 0;
        count_max = strtol(count_text, &end, 10);
        if (end == count_text || *end != '\0' || errno == ERANGE || count_max < 1) {
            fprintf(stderr, "--count needs a positive number up to %ld, not %s\n", LONG_MAX,
                    count_text);
            return EXIT_UNSAT;
        }
    }
    bool analysis = count_text != NULL || sensitivity;
    if (analysis && (sat_mode || config.delay_search)) {
        fprintf(stderr, "--count and --sensitivity need the solver and a fixed delay "
                        "(no --sat, delay_search=0)\n");
        return EXIT_UNSAT;
    }
    warn_about_axis(&config);
    if (check_mode) return run_check(&config);
    if (!sat_mode && !output_paths_distinct(&paths, err, sizeof(err))) {
        fprintf(stderr, "%s\n", err);
        return EXIT_UNSAT;
    }

    if (corpus_dir != NULL) {
        int counts[12] = {0};
        int weights[12];
        if (!corpus_dir_counts(corpus_dir, counts)) {
            fprintf(stderr, "cannot read corpus: %s\n", corpus_dir);
            return EXIT_UNSAT;
        }
        corpus_weights_from_counts(counts, config.w_corpus, weights);
        printf("weights:");
        for (int p = 0; p < 12; p++) printf(" %d", weights[p]);
        printf("\n");
        if (apply_weights) memcpy(config.pc_weight, weights, sizeof(weights));
    }

    if (sat_mode) return run_sat(&config);
    if (analysis) return run_analysis(&config, count_max, sensitivity);

    Run *run = malloc(sizeof(Run));
    if (run == NULL) {
        fprintf(stderr, "out of memory\n");
        return EXIT_UNSAT;
    }
    if (!run_piece(run, &config, err, sizeof(err))) {
        fprintf(stderr, "%s\n", err);
        free(run);
        return EXIT_UNSAT;
    }

    bool writes_ok = output_write_all(run, &paths);
    int exit_code = 0;
    if (run->status == SOLVE_SAT) {
        output_print_summary(stdout, run);
    } else {
        output_print_failure(stderr, run);
        exit_code = run->status == SOLVE_LIMIT ? EXIT_LIMIT : EXIT_UNSAT;
    }
    if (run->counterfactual) output_print_counterfactual(stdout, run);
    if (explain != NULL) {
        int var = find_var(&run->model, explain);
        if (var < 0) {
            fprintf(stderr, "no variable named %s\n", explain);
            if (exit_code == 0) exit_code = EXIT_UNSAT;
        } else {
            Explanation e;
            explain_var(&run->state, var, &e);
            explain_print(stdout, &run->state, &e);
        }
    }
    if (!writes_ok) {
        fprintf(stderr, "could not write every output file\n");
        if (exit_code == 0) exit_code = EXIT_UNSAT;
    }
    run_free(run);
    free(run);
    return exit_code;
}
