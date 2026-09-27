#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include "config.h"
#include "model.h"
#include "solver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define test_mkdir(path) _mkdir(path)
#else
#include <sys/stat.h>
#define test_mkdir(path) mkdir(path, 0755)
#endif

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "fail %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/* The defaults with every search limit off, so tests are deterministic
 * whatever the machine's speed. */
static inline PieceConfig test_config(void) {
    PieceConfig c;
    config_defaults(&c);
    c.time_limit = 0;
    c.optimize = 0;
    return c;
}

static inline void test_set(PieceConfig *c, const char *assignment) {
    char err[200];
    if (!config_assign(c, assignment, err, sizeof(err))) {
        fprintf(stderr, "bad test setting %s: %s\n", assignment, err);
        exit(1);
    }
}

/* A model and solver for one config; free with test_close. */
typedef struct TestRun {
    Model model;
    SolverState state;
    SolveStatus status;
    int values[VAR_MAX];
} TestRun;

static inline TestRun *test_open(const PieceConfig *c) {
    char err[200];
    TestRun *r = (TestRun *)malloc(sizeof(TestRun));
    CHECK(r != NULL);
    if (!model_build(&r->model, c, err, sizeof(err))) {
        fprintf(stderr, "model_build: %s\n", err);
        exit(1);
    }
    CHECK(solver_init(&r->state, &r->model));
    return r;
}

static inline TestRun *test_solve(const PieceConfig *c) {
    TestRun *r = test_open(c);
    r->status = solver_solve(&r->state);
    solver_values(&r->state, r->values);
    return r;
}

static inline void test_close(TestRun *r) {
    solver_free(&r->state);
    model_free(&r->model);
    free(r);
}

static inline int test_pitch(const TestRun *r, int index) {
    return r->values[r->model.pitch[index]];
}

static inline void test_only(TestRun *r, int var, int value) {
    domain_clear(&r->state.domains[var]);
    domain_add(&r->state.domains[var], value);
    solver_touch(&r->state, var);
}

/* The latest removal of value from var, or NULL. */
static inline const ProofEvent *test_removal(const TestRun *r, int var, int value) {
    const ProofLog *log = &r->state.proof;
    for (int i = log->event_count - 1; i >= 0; i--) {
        const ProofEvent *e = &log->events[i];
        if (e->type == PROOF_REMOVE && e->variable_id == var && e->value == value) return e;
    }
    return NULL;
}

static inline char *test_slurp(const char *path, long *size) {
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    long n = ftell(f);
    CHECK(n >= 0);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    char *buf = (char *)malloc((size_t)n + 1);
    CHECK(buf != NULL);
    CHECK(fread(buf, 1, (size_t)n, f) == (size_t)n);
    buf[n] = '\0';
    fclose(f);
    if (size != NULL) *size = n;
    return buf;
}

static inline void test_output_dir(void) {
    test_mkdir("output");
    test_mkdir("output/tests");
}

#endif
