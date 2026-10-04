#include "export.h"
#include "midi.h"
#include "perform.h"
#include "run.h"
#include "test_util.h"

#include <math.h>

/* Solves a preset with extra settings, as the program does. */
static Run *solve(const char *preset, const char *const *sets) {
    PieceConfig c = test_config();
    char err[300];
    if (preset != NULL) CHECK(config_apply_preset(&c, preset, err, sizeof(err)));
    c.time_limit = 0;
    for (int i = 0; sets != NULL && sets[i] != NULL; i++) test_set(&c, sets[i]);
    CHECK(config_validate(&c, err, sizeof(err)));
    Run *run = malloc(sizeof(Run));
    CHECK(run != NULL);
    CHECK(run_piece(run, &c, err, sizeof(err)));
    CHECK(run->status == SOLVE_SAT);
    return run;
}

static void done(Run *run) {
    run_free(run);
    free(run);
}

/* The note voice v attacks at melody index i, or -1. */
static int note_of(const Score *s, int v, int i) {
    for (int k = 0; k < s->voice[v].count; k++) {
        if (s->voice[v].notes[k].source == i && s->voice[v].notes[k].pitch != SOUND_REST) return k;
    }
    return -1;
}

/* plain is no shaping at all: one tempo, one loudness, written lengths */
static void test_plain(void) {
    const char *const sets[] = {"mood=plain", "optimize=0", NULL};
    Run *run = solve("longing", sets);
    const Performance *p = &run->perf;
    const Score *s = &run->score;
    CHECK(p->mood == MOOD_PLAIN && p->hold == 0.0);
    double step = p->times[1] - p->times[0];
    CHECK(step > 0.0);
    for (int t = 0; t < s->span; t++) {
        double d = p->times[t + 1] - p->times[t];
        CHECK(d > step - 1e-9 && d < step + 1e-9);
        CHECK(p->loop_times[t + 1] == p->times[t + 1]);
    }
    for (int v = 0; v < s->voices; v++) {
        for (int k = 0; k < s->voice[v].count; k++) {
            if (s->voice[v].notes[k].pitch == SOUND_REST) continue;
            CHECK(p->note[v][k].velocity == p->note[0][0].velocity);
            CHECK(p->note[v][k].sounding == 1.0);
        }
    }
    done(run);
}

/* a mood shapes the piece around the climax the form planned, in every
 * voice as it reaches it, and broadens and holds at the end */
static void test_mood(void) {
    const char *const sets[] = {"optimize=0", NULL};
    Run *run = solve("lament", sets);
    const Performance *p = &run->perf;
    const Score *s = &run->score;
    CHECK(p->mood == MOOD_LAMENT);
    CHECK(p->climax == run->model.form.climax);
    for (int v = 0; v < s->voices; v++) {
        int first = note_of(s, v, 0);
        int peak = note_of(s, v, p->climax);
        if (first < 0 || peak < 0) continue;
        CHECK(p->note[v][peak].velocity > p->note[v][first].velocity);
    }
    for (int t = 0; t < s->span; t++) CHECK(p->times[t + 1] > p->times[t]);
    int span = s->span;
    CHECK(p->times[span] - p->times[span - 1] > 1.2 * (p->times[1] - p->times[0])); /* broadens */
    CHECK(p->loop_times[span] < p->times[span]);                                   /* not in a loop */
    CHECK(p->hold > 0.0);
    done(run);

    /* auto guesses from tempo and key: slow and minor laments */
    const char *const slow[] = {"mood=auto", "tempo=60", "mode=minor", "optimize=0", NULL};
    run = solve(NULL, slow);
    CHECK(run->perf.mood == MOOD_LAMENT);
    done(run);
}

/* each player has its mix, intensity and articulation */
static void test_players(void) {
    const char *const sets[] = {"optimize=0", "volume=110,60", "pan=auto,-80",
                                "intensity=0", "articulation=auto,staccato", NULL};
    Run *run = solve("longing", sets);
    const Performance *p = &run->perf;
    const Score *s = &run->score;
    CHECK(p->volume[0] == 110 && p->volume[1] == 60);
    CHECK(p->pan[0] == -60 && p->pan[1] == -80); /* auto spreads from the left */
    CHECK(perform_gain(p, 1) < perform_gain(p, 0));
    /* intensity 0: voice 1 at one loudness */
    int vel = -1;
    for (int k = 0; k < s->voice[0].count; k++) {
        if (s->voice[0].notes[k].pitch == SOUND_REST) continue;
        if (vel < 0) vel = p->note[0][k].velocity;
        CHECK(p->note[0][k].velocity == vel);
    }
    /* staccato voice 2, but its last note sounds to the end */
    for (int k = 0; k < s->voice[1].count; k++) {
        const ScoreNote *n = &s->voice[1].notes[k];
        if (n->pitch == SOUND_REST) continue;
        CHECK(p->note[1][k].sounding == (n->start + n->length >= s->span ? 1.0 : 0.45));
    }
    done(run);
}

static long file_size(const char *path, unsigned char **data) {
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = malloc((size_t)n);
    CHECK(*data != NULL && fread(*data, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    return n;
}

/* canon.mid and voices.wav follow the same plan as the page */
static void test_exports(void) {
    const char *const sets[] = {"optimize=0", "volume=90", NULL};
    Run *run = solve("lament", sets);
    test_mkdir("output");
    test_mkdir("output/tests");
    CHECK(midi_write_performed("output/tests/performed.mid", &run->score, &run->perf));
    unsigned char *mid;
    long n = file_size("output/tests/performed.mid", &mid);
    int tempos = 0;
    int volumes = 0;
    for (long i = 0; i + 3 < n; i++) {
        if (mid[i] == 0xFF && mid[i + 1] == 0x51 && mid[i + 2] == 3) tempos++;
        if (mid[i] == 0xB0 && mid[i + 1] == 7 && mid[i + 2] == 90) volumes++;
    }
    CHECK(tempos > 4);   /* the tempo bends */
    CHECK(volumes == 1); /* voice 1's volume */
    free(mid);

    CHECK(export_wav_performed("output/tests/performed.wav", &run->score, INSTRUMENT_AUTO,
                               &run->perf));
    unsigned char *wav;
    n = file_size("output/tests/performed.wav", &wav);
    long frames = (n - 44) / 4;
    long expect = lround((run->perf.times[run->score.span] + run->perf.hold) * 44100) + 44100 * 3 / 2;
    CHECK(frames == expect);
    free(wav);
    done(run);
}

int main(void) {
    test_plain();
    test_mood();
    test_players();
    test_exports();
    printf("ok\n");
    return 0;
}
