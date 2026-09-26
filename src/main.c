#include "canon.h"
#include "constraint.h"
#include "export.h"
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

static int sibling_path(char *out, size_t cap, const char *base, const char *name)
{
    const char *slash = strrchr(base, '/');
    const char *bslash = strrchr(base, '\\');
    const char *sep = slash;
    if (bslash != NULL && (sep == NULL || bslash > sep)) {
        sep = bslash;
    }
    if (sep == NULL) {
        if (strlen(name) + 1 > cap) {
            return 0;
        }
        memcpy(out, name, strlen(name) + 1);
        return 1;
    }
    size_t n = (size_t)(sep - base + 1);
    size_t m = strlen(name);
    if (n + m + 1 > cap) {
        return 0;
    }
    memcpy(out, base, n);
    memcpy(out + n, name, m + 1);
    return 1;
}

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
        } else if (strcmp(key, "transpose") == 0) {
            config->transpose = value;
        } else if (strcmp(key, "augment") == 0) {
            config->augment = value;
        } else if (strcmp(key, "diminish") == 0) {
            config->diminish = value;
        } else if (strcmp(key, "phase") == 0) {
            config->phase = value;
        } else if (strcmp(key, "delay_1") == 0) {
            config->voice_delay[1] = value;
        } else if (strcmp(key, "delay_2") == 0) {
            config->voice_delay[2] = value;
        } else if (strcmp(key, "delay_3") == 0) {
            config->voice_delay[3] = value;
        } else if (strcmp(key, "lock") == 0) {
            config->lock = value;
        } else if (strcmp(key, "lock_index") == 0) {
            config->lock_index = value;
        } else if (strcmp(key, "lock_pitch") == 0) {
            config->lock_pitch = value;
        } else if (strcmp(key, "anneal_start") == 0) {
            config->anneal_start = value;
        } else if (strcmp(key, "anneal_end") == 0) {
            config->anneal_end = value;
        } else if (strcmp(key, "anneal_steps") == 0) {
            config->anneal_steps = value;
        } else if (strcmp(key, "w_dissonance") == 0) {
            config->w_dissonance = value;
        } else if (strcmp(key, "w_parallel") == 0) {
            config->w_parallel = value;
        } else if (strcmp(key, "strong_chord") == 0) {
            config->strong_chord = value;
        } else if (strcmp(key, "cadence") == 0) {
            config->cadence = value;
        } else if (strcmp(key, "rhythm") == 0) {
            config->rhythm = value;
        } else if (strcmp(key, "rest_at") == 0) {
            config->rest_at = value;
        } else if (strcmp(key, "cyclic") == 0) {
            config->cyclic = value;
        } else if (strcmp(key, "w_motif") == 0) {
            config->w_motif = value;
        } else if (strcmp(key, "motif_a") == 0) {
            config->motif_a = value;
        } else if (strcmp(key, "motif_b") == 0) {
            config->motif_b = value;
        }
    }

    fclose(f);
    return 1;
}

static int validate_config(const PieceConfig *config)
{
    if (config->voices < 2 || config->voices > VOICE_MAX) {
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
    if (config->w_gravity < 0 || config->w_leap < 0 || config->w_curve < 0 ||
        config->w_dissonance < 0 || config->w_parallel < 0) {
        fprintf(stderr, "invalid weight\n");
        return 0;
    }
    if (config->augment < 0 || (config->augment == 1)) {
        fprintf(stderr, "invalid augment\n");
        return 0;
    }
    if (config->diminish < 0 || config->diminish == 1) {
        fprintf(stderr, "invalid diminish\n");
        return 0;
    }
    if (config->augment >= 2 && config->diminish >= 2) {
        fprintf(stderr, "invalid rhythm transform\n");
        return 0;
    }
    if (config->phase < 0) {
        fprintf(stderr, "invalid phase\n");
        return 0;
    }
    for (int v = 0; v < VOICE_MAX; v++) {
        if (config->voice_delay[v] < 0) {
            fprintf(stderr, "invalid delay\n");
            return 0;
        }
    }
    if (config->lock != 0 && config->lock != 1) {
        fprintf(stderr, "invalid lock\n");
        return 0;
    }
    if (config->lock == 1 &&
        (config->lock_index < 0 || config->lock_index >= config->length ||
         config->lock_pitch < 0 || config->lock_pitch > 127)) {
        fprintf(stderr, "invalid lock\n");
        return 0;
    }
    if (config->strong_chord != 0 && config->strong_chord != 1) {
        fprintf(stderr, "invalid strong_chord\n");
        return 0;
    }
    if (config->cadence != 0 && config->cadence != 1) {
        fprintf(stderr, "invalid cadence\n");
        return 0;
    }
    if (config->rhythm != 0 && config->rhythm != 1) {
        fprintf(stderr, "invalid rhythm\n");
        return 0;
    }
    if (config->rest_at < 0 || config->rest_at >= config->length) {
        fprintf(stderr, "invalid rest_at\n");
        return 0;
    }
    if (config->rest_at != 0 && config->rhythm == 0) {
        fprintf(stderr, "rest_at requires rhythm\n");
        return 0;
    }
    if (config->cyclic != 0 && config->cyclic != 1) {
        fprintf(stderr, "invalid cyclic\n");
        return 0;
    }
    if (config->w_motif < 0) {
        fprintf(stderr, "invalid w_motif\n");
        return 0;
    }
    if (config->anneal_steps < 0 || config->anneal_start < 0 ||
        config->anneal_end < 0) {
        fprintf(stderr, "invalid anneal\n");
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

static void fill_voice_line(int *line, const int *melody, int length, int voice,
                            const PieceConfig *config)
{
    for (int i = 0; i < length; i++) {
        int source = (voice > 0 && config->retrograde) ? length - 1 - i : i;
        line[i] = canon_sounding(config, voice, melody[source]);
    }
}

static void print_success(const int *melody, const PieceConfig *config, int backtracks,
                          const SolverState *state)
{
    int length = config->length;
    printf("melody:");
    for (int i = 0; i < length; i++) {
        printf(" %d", melody[i]);
    }
    printf("\n");

    int span = canon_span_config(config);
    for (int v = 1; v < config->voices; v++) {
        printf("voice %d:", v + 1);
        for (int t = 0; t < span; t++) {
            int idx = canon_map_source(config, v, t);
            if (idx < 0) {
                printf(" rest");
            } else {
                printf(" %d", canon_sounding(config, v, melody[idx]));
            }
        }
        printf("\n");
    }

    if (config->rhythm) {
        printf("rhythm:");
        for (int i = 0; i < length; i++) {
            if (state->duration[i] == 0) {
                printf(" rest");
            } else if (state->duration[i] == 2) {
                printf(" half");
            } else {
                printf(" 1");
            }
        }
        printf("\n");
    }

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
        .transpose = 0,
        .augment = 0,
        .diminish = 0,
        .phase = 0,
        .voice_delay = {0},
        .lock = 0,
        .lock_index = 0,
        .lock_pitch = 0,
        .anneal_start = 0,
        .anneal_end = 0,
        .anneal_steps = 0,
        .w_dissonance = 0,
        .w_parallel = 0,
        .strong_chord = 0,
        .cadence = 0,
        .rhythm = 0,
        .rest_at = 0,
        .cyclic = 0,
        .w_motif = 0,
        .motif_a = 0,
        .motif_b = 0,
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
        } else if (strcmp(argv[i], "--lock") == 0) {
            if (i + 2 >= argc) {
                fprintf(stderr, "missing value\n");
                return 1;
            }
            config.lock = 1;
            config.lock_index = atoi(argv[++i]);
            config.lock_pitch = atoi(argv[++i]);
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
    {
        char dag_path[512];
        size_t n = strlen(proof_path);
        if (n + 5 < sizeof(dag_path)) {
            memcpy(dag_path, proof_path, n + 1);
            char *dot = strrchr(dag_path, '.');
            if (dot != NULL && strcmp(dot, ".txt") == 0) {
                strcpy(dot, ".dag");
            } else {
                memcpy(dag_path + n, ".dag", 5);
            }
            ensure_parent_dir(dag_path);
            if (!proof_write_dag(&state.proof, dag_path)) {
                writes_ok = 0;
            }
        }
    }
    if (!write_entropy(&state.proof, entropy_path)) {
        writes_ok = 0;
    }

    if (ok) {
        int lines[VOICE_MAX][MELODY_MAX];
        const int *line_ptrs[VOICE_MAX];
        int starts[VOICE_MAX];
        for (int v = 0; v < config.voices; v++) {
            fill_voice_line(lines[v], melody, config.length, v, &config);
            line_ptrs[v] = lines[v];
            starts[v] = canon_voice_delay(&config, v) * 480;
        }
        if (!midi_write_voices(out_path, line_ptrs, starts, config.voices,
                               config.length, state.duration)) {
            writes_ok = 0;
        }
        {
            char xml_path[512];
            char svg_path[512];
            char wav_path[512];
            char json_path[512];
            if (sibling_path(xml_path, sizeof(xml_path), out_path,
                             "score.musicxml")) {
                ensure_parent_dir(xml_path);
                if (!export_musicxml(xml_path, line_ptrs, config.voices,
                                     config.length, state.duration)) {
                    writes_ok = 0;
                }
            }
            if (sibling_path(svg_path, sizeof(svg_path), out_path,
                             "contour.svg")) {
                ensure_parent_dir(svg_path);
                if (!export_contour(svg_path, melody, config.length)) {
                    writes_ok = 0;
                }
            }
            if (sibling_path(wav_path, sizeof(wav_path), out_path,
                             "voices.wav")) {
                ensure_parent_dir(wav_path);
                if (!export_wav(wav_path, line_ptrs, config.voices,
                                config.length, state.duration)) {
                    writes_ok = 0;
                }
            }
            if (sibling_path(json_path, sizeof(json_path), proof_path,
                             "proof.json")) {
                ensure_parent_dir(json_path);
                if (!export_trace(json_path, &state.proof)) {
                    writes_ok = 0;
                }
            }
            char html_path[512];
            if (sibling_path(html_path, sizeof(html_path), out_path,
                             "score.html")) {
                int energy = 0;
                if (config.energy == 1) {
                    energy = melody_energy_full(
                        melody, config.length, config.delay, config.w_gravity,
                        config.w_leap, config.w_curve, config.w_dissonance,
                        config.w_parallel, config.w_motif, config.motif_a,
                        config.motif_b);
                }
                ensure_parent_dir(html_path);
                if (!export_score_page(html_path, melody, &config, &state,
                                       backtracks, energy)) {
                    writes_ok = 0;
                }
            }
        }
        print_success(melody, &config, backtracks, &state);
        if (config.energy == 1) {
            printf("energy: %d\n",
                   melody_energy_full(melody, config.length, config.delay,
                                      config.w_gravity, config.w_leap,
                                      config.w_curve, config.w_dissonance,
                                      config.w_parallel, config.w_motif,
                                      config.motif_a, config.motif_b));
        }
        if (config.lock == 1) {
            SolverState alt = {0};
            int alt_melody[MELODY_MAX];
            int alt_bt = 0;
            solver_init(&alt, &config);
            solver_lock(&alt, config.lock_index, config.lock_pitch);
            bool alt_ok = solve(&alt, alt_melody, &alt_bt);
            printf("counterfactual: index %d pitch %d\n", config.lock_index,
                   config.lock_pitch);
            if (!alt_ok) {
                const char *msg = "unsat";
                if (alt.proof.event_count > 0) {
                    msg = alt.proof.events[alt.proof.event_count - 1].message;
                }
                printf("killed: %s\n", msg);
            } else if (melody[config.lock_index] == config.lock_pitch) {
                printf("forced\n");
            } else {
                printf("legal\n");
            }
            solver_free(&alt);
        }
        {
            char report_path[512];
            int energy = 0;
            if (config.energy == 1) {
                energy = melody_energy_full(
                    melody, config.length, config.delay, config.w_gravity,
                    config.w_leap, config.w_curve, config.w_dissonance,
                    config.w_parallel, config.w_motif, config.motif_a,
                    config.motif_b);
            }
            if (sibling_path(report_path, sizeof(report_path), out_path,
                             "report.txt")) {
                ensure_parent_dir(report_path);
                if (!export_report(report_path, &config, backtracks,
                                   entropy_bits(state.domains, config.length),
                                   energy, NULL, 0)) {
                    writes_ok = 0;
                }
            }
        }
        solver_free(&state);
        return writes_ok ? 0 : 1;
    }

    {
        char json_path[512];
        if (sibling_path(json_path, sizeof(json_path), proof_path,
                         "proof.json")) {
            ensure_parent_dir(json_path);
            if (!export_trace(json_path, &state.proof)) {
                writes_ok = 0;
            }
        }
    }

    {
        int core[16];
        int core_n = 0;
        if (solver_unsat_core(&config, core, 16, &core_n)) {
            fprintf(stderr, "core:");
            for (int i = 0; i < core_n; i++) {
                fprintf(stderr, " %s", constraint_name(core[i]));
            }
            fprintf(stderr, "\n");
        }
        char report_path[512];
        if (sibling_path(report_path, sizeof(report_path), out_path,
                         "report.txt")) {
            ensure_parent_dir(report_path);
            export_report(report_path, &config, backtracks,
                          entropy_bits(state.domains, config.length), 0, core,
                          core_n);
        }
    }
    print_unsat(&state);
    if (config.lock == 1) {
        SolverState alt = {0};
        int alt_melody[MELODY_MAX];
        int alt_bt = 0;
        solver_init(&alt, &config);
        solver_lock(&alt, config.lock_index, config.lock_pitch);
        bool alt_ok = solve(&alt, alt_melody, &alt_bt);
        printf("counterfactual: index %d pitch %d\n", config.lock_index,
               config.lock_pitch);
        if (!alt_ok) {
            const char *msg = "unsat";
            if (alt.proof.event_count > 0) {
                msg = alt.proof.events[alt.proof.event_count - 1].message;
            }
            printf("killed: %s\n", msg);
        } else {
            printf("legal\n");
        }
        solver_free(&alt);
    }
    solver_free(&state);
    return 1;
}
