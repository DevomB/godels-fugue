#include "config.h"
#include "corpus.h"
#include "explain.h"
#include "output.h"
#include "run.h"
#include "sat.h"
#include "theory.h"

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
            "  --explain VAR        print why a variable has its value: a melody index\n"
            "                       such as 5, or x5, tie5, chord2, key, key2\n"
            "  --out FILE           MIDI path; the other score files go beside it\n"
            "                       (default output/canon.mid)\n"
            "  --proof FILE         proof trace path (default output/proof.txt)\n"
            "  --entropy FILE       entropy log path (default output/entropy.txt)\n"
            "  --corpus DIR         suggest pitch-class weights from files of MIDI pitches\n"
            "  --apply-weights      use the suggested corpus weights\n"
            "  --sat                check the rules with the SAT backend instead\n"
            "  --max-nodes N        give up after N search nodes (0 = no limit)\n"
            "  --time-limit MS      give up after MS milliseconds (0 = no limit)\n"
            "  --list-config        print every config key (--markdown for a table)\n"
            "  --list-presets       print the style presets\n"
            "  --version            print the version\n"
            "\n"
            "Exit status: 0 solved, 1 unsatisfiable or bad input, 2 too large for\n"
            "--sat, 3 search limit reached.\n");
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

/* An inversion axis that maps the key onto other notes leaves the
 * scale rule few pitches to choose from; say so and suggest one. */
static void warn_about_axis(const PieceConfig *c) {
    if (!c->invert || c->key < 0 || c->mode < 0) return;
    int key = key_id(c->key, c->mode);
    int size = key_scale_size(key);
    int kept = inversion_kept(key, c->axis, c->transpose);
    if (kept == size) return;
    char name[32];
    key_name(key, name, sizeof(name));
    fprintf(stderr, "note: inverting around axis %d keeps %d of %d notes of %s in the key",
            c->axis, kept, size, name);
    int better = inversion_nearest_axis(key, c->axis, c->transpose);
    if (better >= 0) fprintf(stderr, "; axis %d keeps them all", better);
    fprintf(stderr, "\n");
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
    const char *max_nodes = NULL;
    const char *time_limit = NULL;
    bool apply_weights = false;
    bool sat_mode = false;
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
            if (strcmp(a, "--corpus") == 0) slot = &corpus_dir;
            if (strcmp(a, "--explain") == 0) slot = &explain;
            if (strcmp(a, "--max-nodes") == 0) slot = &max_nodes;
            if (strcmp(a, "--time-limit") == 0) slot = &time_limit;
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
    warn_about_axis(&config);

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
            exit_code = EXIT_UNSAT;
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
