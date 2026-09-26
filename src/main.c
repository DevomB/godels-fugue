#include "canon.h"
#include "midi.h"
#include "solver.h"
#include "theory.h"
#include "types.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

static void ensure_parent_dir(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *sep = slash;
    if (bslash != NULL && (sep == NULL || bslash > sep)) {
        sep = bslash;
    }
    if (sep == NULL || sep == path) {
        return;
    }

    size_t n = (size_t)(sep - path);
    char buf[512];
    if (n >= sizeof(buf)) {
        return;
    }
    memcpy(buf, path, n);
    buf[n] = '\0';

#ifdef _WIN32
    if (_mkdir(buf) != 0 && errno != EEXIST) {
        /* ignore other errors; writers report failure */
    }
#else
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
        /* ignore other errors; writers report failure */
    }
#endif
}

static int load_config(const char *path, PieceConfig *config)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "cannot read config\n");
        return 0;
    }

    char key[64];
    int value;
    while (fscanf(f, "%63s %d", key, &value) == 2) {
        if (strcmp(key, "length") == 0) {
            config->length = value;
        } else if (strcmp(key, "voices") == 0) {
            config->voices = value;
        } else if (strcmp(key, "delay") == 0) {
            config->delay = value;
        } else if (strcmp(key, "range_low") == 0) {
            config->range_low = value;
        } else if (strcmp(key, "range_high") == 0) {
            config->range_high = value;
        } else if (strcmp(key, "max_leap") == 0) {
            config->max_leap = value;
        } else if (strcmp(key, "invert") == 0) {
            config->invert = value;
        } else if (strcmp(key, "axis") == 0) {
            config->axis = value;
        } else if (strcmp(key, "retrograde") == 0) {
            config->retrograde = value;
        } else if (strcmp(key, "energy") == 0) {
            config->energy = value;
        } else if (strcmp(key, "temperature") == 0) {
            config->temperature = value;
        } else if (strcmp(key, "seed") == 0) {
            config->seed = value;
        } else if (strcmp(key, "w_gravity") == 0) {
            config->w_gravity = value;
        } else if (strcmp(key, "w_leap") == 0) {
            config->w_leap = value;
        } else if (strcmp(key, "w_curve") == 0) {
            config->w_curve = value;
        }
    }

    fclose(f);
    return 1;
}

static int validate_config(const PieceConfig *config)
{
    if (config->voices != 2) {
        fprintf(stderr, "invalid voices\n");
        return 0;
    }
    if (config->length < 1 || config->length > MELODY_MAX) {
        fprintf(stderr, "invalid length\n");
        return 0;
    }
    if (config->delay < 0) {
        fprintf(stderr, "invalid delay\n");
        return 0;
    }
    if (config->range_low < 0 || config->range_low > 127 ||
        config->range_high < 0 || config->range_high > 127 ||
        config->range_low > config->range_high) {
        fprintf(stderr, "invalid range\n");
        return 0;
    }
    if (config->max_leap < 0) {
        fprintf(stderr, "invalid max_leap\n");
        return 0;
    }
    if (config->invert != 0 && config->invert != 1) {
        fprintf(stderr, "invalid invert\n");
        return 0;
    }
    if (config->invert == 1 && (config->axis < 0 || config->axis > 127)) {
        fprintf(stderr, "invalid axis\n");
        return 0;
    }
    if (config->retrograde != 0 && config->retrograde != 1) {
        fprintf(stderr, "invalid retrograde\n");
        return 0;
    }
    if (config->energy != 0 && config->energy != 1) {
        fprintf(stderr, "invalid energy\n");
        return 0;
    }
    if (config->temperature < 0) {
        fprintf(stderr, "invalid temperature\n");
        return 0;
    }
    if (config->w_gravity < 0 || config->w_leap < 0 || config->w_curve < 0) {
        fprintf(stderr, "invalid weight\n");
        return 0;
    }
    return 1;
}

static int write_entropy(const ProofLog *log, const char *path)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return 0;
    }
    for (int i = 0; i < log->sample_count; i++) {
        if (fprintf(f, "%.6f\n", log->samples[i].bits) < 0) {
            fclose(f);
            return 0;
        }
    }
    if (fclose(f) != 0) {
        return 0;
    }
    return 1;
}

static void print_success(const int *melody, const int *follow, int length, int delay,
                          int backtracks, const SolverState *state)
{
    printf("melody:");
    for (int i = 0; i < length; i++) {
        printf(" %d", melody[i]);
    }
    printf("\n");

    int span = canon_span(length, delay);
    printf("voice 2:");
    for (int t = 0; t < span; t++) {
        int idx = canon_melody_index(1, t, delay, length);
        if (idx < 0) {
            printf(" rest");
        } else {
            printf(" %d", follow[idx]);
        }
    }
    printf("\n");

    printf("backtracks: %d\n", backtracks);
    printf("entropy: %.6f\n", entropy_bits(state->domains, length));
}

static void print_unsat(const SolverState *state)
{
    fprintf(stderr, "unsat: variable %d\n", state->failed_variable);
    if (state->proof.event_count > 0) {
        const ProofEvent *e = &state->proof.events[state->proof.event_count - 1];
        fprintf(stderr, "last removal: variable %d pitch %d %s\n",
                e->variable_id, e->removed_pitch, e->message);
    }
}

int main(int argc, char **argv)
{
    PieceConfig config = {
        .length = 12,
        .voices = 2,
        .delay = 4,
        .range_low = 60,
        .range_high = 72,
        .max_leap = 7,
        .invert = 0,
        .axis = 67,
        .retrograde = 0,
        .energy = 0,
        .temperature = 0,
        .seed = 1,
        .w_gravity = 0,
        .w_leap = 0,
        .w_curve = 0,
    };

    const char *config_path = NULL;
    const char *out_path = "output/canon.mid";
    const char *proof_path = "output/proof.txt";
    const char *entropy_path = "output/entropy.txt";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value\n");
                return 1;
            }
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value\n");
                return 1;
            }
            out_path = argv[++i];
        } else if (strcmp(argv[i], "--proof") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value\n");
                return 1;
            }
            proof_path = argv[++i];
        } else if (strcmp(argv[i], "--entropy") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value\n");
                return 1;
            }
            entropy_path = argv[++i];
        } else {
            fprintf(stderr, "unknown argument\n");
            return 1;
        }
    }

    if (config_path != NULL && !load_config(config_path, &config)) {
        return 1;
    }
    if (!validate_config(&config)) {
        return 1;
    }

    SolverState state = {0};
    solver_init(&state, &config);

    int melody[MELODY_MAX];
    int backtracks = 0;
    bool ok = solve(&state, melody, &backtracks);

    ensure_parent_dir(out_path);
    ensure_parent_dir(proof_path);
    ensure_parent_dir(entropy_path);

    int writes_ok = 1;
    if (!proof_write(&state.proof, proof_path)) {
        writes_ok = 0;
    }
    if (!write_entropy(&state.proof, entropy_path)) {
        writes_ok = 0;
    }

    if (ok) {
        int follow[MELODY_MAX];
        for (int i = 0; i < config.length; i++) {
            int source = config.retrograde ? config.length - 1 - i : i;
            follow[i] = melody[source];
            if (config.invert) {
                follow[i] = invert_pitch(config.axis, follow[i]);
            }
        }
        if (!midi_write_canon(out_path, melody, follow, config.length, config.delay)) {
            writes_ok = 0;
        }
        print_success(melody, follow, config.length, config.delay, backtracks, &state);
        if (config.energy == 1) {
            printf("energy: %d\n",
                   melody_energy(melody, config.length, config.w_gravity,
                                 config.w_leap, config.w_curve));
        }
        solver_free(&state);
        return writes_ok ? 0 : 1;
    }

    print_unsat(&state);
    solver_free(&state);
    return 1;
}
