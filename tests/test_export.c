#include "export.h"
#include "proof.h"
#include "solver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "fail %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

static void slurp(const char *path, unsigned char **buf, long *sz) {
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    *sz = ftell(f);
    CHECK(*sz > 0);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    *buf = malloc((size_t)*sz + 1);
    CHECK(*buf != NULL);
    CHECK(fread(*buf, 1, (size_t)*sz, f) == (size_t)*sz);
    (*buf)[*sz] = 0;
    fclose(f);
}

int main(void) {
#ifdef _WIN32
    _mkdir("output");
#else
    mkdir("output", 0755);
#endif

    int lead[] = {60, 64, 67, 72};
    int follow[] = {60, 64, 67, 72};
    const int *lines[2];
    int durs[] = {1, 1, 1, 1};
    lines[0] = lead;
    lines[1] = follow;

    CHECK(export_musicxml("output/score.musicxml", lines, 2, 4, durs));
    CHECK(export_contour("output/contour.svg", lead, 4));
    CHECK(export_wav("output/voices.wav", lines, 2, 4, durs));

    ProofLog log;
    proof_init(&log);
    CHECK(proof_append_removal(&log, 0, 61, 1, "scale"));
    CHECK(export_trace("output/proof.json", &log));

    unsigned char *buf = NULL;
    long sz = 0;

    slurp("output/score.musicxml", &buf, &sz);
    CHECK(sz > 20);
    CHECK(memcmp(buf, "<?xml", 5) == 0);
    CHECK(strstr((char *)buf, "score-partwise") != NULL);
    CHECK(strstr((char *)buf, "<step>C</step>") != NULL);
    free(buf);

    slurp("output/contour.svg", &buf, &sz);
    CHECK(memcmp(buf, "<svg", 4) == 0);
    CHECK(strstr((char *)buf, "polyline") != NULL);
    free(buf);

    slurp("output/voices.wav", &buf, &sz);
    CHECK(sz > 44);
    CHECK(memcmp(buf, "RIFF", 4) == 0);
    CHECK(memcmp(buf + 8, "WAVE", 4) == 0);
    CHECK(memcmp(buf + 12, "fmt ", 4) == 0);
    CHECK(memcmp(buf + 36, "data", 4) == 0);
    free(buf);

    slurp("output/proof.json", &buf, &sz);
    CHECK(buf[0] == '{');
    CHECK(strstr((char *)buf, "\"events\"") != NULL);
    CHECK(strstr((char *)buf, "\"scale\"") != NULL);
    free(buf);

    {
        PieceConfig cfg = {0};
        cfg.length = 4;
        cfg.voices = 2;
        cfg.delay = 2;
        cfg.range_low = 60;
        cfg.range_high = 72;
        cfg.max_leap = 7;
        cfg.axis = 67;
        SolverState s = {0};
        int melody[MELODY_MAX];
        int bt = 0;
        solver_init(&s, &cfg);
        CHECK(solve(&s, melody, &bt));
        CHECK(export_score_page("output/score.html", melody, &cfg, &s, bt, 0));
        slurp("output/score.html", &buf, &sz);
        CHECK(memcmp(buf, "<!DOCTYPE html>", 15) == 0);
        CHECK(strstr((char *)buf, "melody:") != NULL);
        CHECK(strstr((char *)buf, "backtracks:") != NULL);
        CHECK(strstr((char *)buf, "entropy:") != NULL);
        free(buf);
        solver_free(&s);
    }

    proof_free(&log);
    printf("ok\n");
    return 0;
}
