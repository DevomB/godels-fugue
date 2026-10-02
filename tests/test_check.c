#include "check.h"
#include "test_util.h"

static bool locked(const Model *m, int var) {
    for (int i = 0; i < m->ncons; i++) {
        const Constraint *c = &m->cons[i];
        if (c->type == C_LOCK && c->vars[0] == var) return true;
    }
    return domain_singleton(&m->initial[var]);
}

/* Every reported violation names only fixed variables. */
static void check_only_fixed(const Model *m, const Violation *v, int n) {
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < v[i].nvars; k++) CHECK(locked(m, v[i].vars[k]));
    }
}

static int check_config(const PieceConfig *c, Violation *v, Model *m) {
    char err[200];
    CHECK(model_build(m, c, err, sizeof(err)));
    int n = check_given(m, v, CHECK_MAX);
    check_only_fixed(m, v, n);
    return n;
}

static bool reports(const Model *m, const Violation *v, int n, int rule) {
    for (int i = 0; i < n; i++) {
        if (m->cons[v[i].con].rule == rule) return true;
    }
    return false;
}

int main(void) {
    Violation v[CHECK_MAX];
    Model m;
    char text[CHECK_TEXT_MAX];

    /* C#4 is not in C major */
    PieceConfig c = test_config();
    test_set(&c, "melody=60,61");
    int n = check_config(&c, v, &m);
    CHECK(n == 1);
    CHECK(m.cons[v[0].con].rule == CID_SCALE);
    check_text(&m, &v[0], text, sizeof(text));
    CHECK(strstr(text, "scale: notes stay in the key") == text);
    CHECK(strstr(text, "x1 = C#4") != NULL);
    CHECK(strstr(text, "key = C major") != NULL);
    model_free(&m);

    /* x0 in voice 2 against x4 in voice 1 at the strong step 4: a second */
    c = test_config();
    test_set(&c, "melody=60,?,?,?,62");
    n = check_config(&c, v, &m);
    CHECK(n == 1);
    CHECK(m.cons[v[0].con].rule == CID_CONSONANCE);
    CHECK(m.cons[v[0].con].time == 4);
    check_text(&m, &v[0], text, sizeof(text));
    CHECK(strstr(text, "consonance: voices 1,2 must be consonant at step 4") == text);
    CHECK(strstr(text, "x4 = D4") != NULL && strstr(text, "x0 = C4") != NULL);
    model_free(&m);

    /* a pitch out of range and an excess of given rests */
    c = test_config();
    test_set(&c, "rhythm=1");
    test_set(&c, "max_rests=1");
    test_set(&c, "melody=rest,rest,?,?,?,?,?,?,?,?,?,100");
    n = check_config(&c, v, &m);
    CHECK(reports(&m, v, n, CID_RANGE));
    CHECK(reports(&m, v, n, CID_REST));
    model_free(&m);

    /* a solved melody, given in full, breaks nothing */
    c = test_config();
    TestRun *r = test_solve(&c);
    CHECK(r->status == SOLVE_SAT);
    char melody[256] = "melody=";
    for (int i = 0; i < c.length; i++) {
        size_t used = strlen(melody);
        snprintf(melody + used, sizeof(melody) - used, "%s%d", i ? "," : "",
                 test_pitch(r, i));
    }
    test_close(r);
    test_set(&c, melody);
    CHECK(check_config(&c, v, &m) == 0);
    model_free(&m);

    /* notes the solver still chooses are never judged */
    c = test_config();
    test_set(&c, "melody=60,?,?,?,?,?,?,?,?,?,?,?");
    CHECK(check_config(&c, v, &m) == 0);
    model_free(&m);
    c = test_config();
    test_set(&c, "melody=60,?,?,?,62,72");
    n = check_config(&c, v, &m);
    CHECK(n == 2);
    CHECK(reports(&m, v, n, CID_CONSONANCE) && reports(&m, v, n, CID_LEAP));
    model_free(&m);

    /* a lock and a given note disagree: the later one is named with its value */
    c = test_config();
    test_set(&c, "lock=1");
    test_set(&c, "lock_index=0");
    test_set(&c, "lock_pitch=60");
    test_set(&c, "melody=64");
    n = check_config(&c, v, &m);
    CHECK(n == 1);
    CHECK(m.cons[v[0].con].rule == CID_LOCK);
    check_text(&m, &v[0], text, sizeof(text));
    CHECK(strstr(text, "(x0 = E4)") != NULL);
    model_free(&m);

    printf("test_check: ok\n");
    return 0;
}
