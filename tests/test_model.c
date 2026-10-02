#include "canon.h"
#include "theory.h"
#include "test_util.h"

static int count_rule(const Model *m, int rule) {
    int n = 0;
    for (int i = 0; i < m->ncons; i++) {
        if (m->cons[i].rule == rule) n++;
    }
    return n;
}

static int count_term(const Model *m, int term) {
    int n = 0;
    for (int i = 0; i < m->nterms; i++) {
        if (m->terms[i].rule == term) n++;
    }
    return n;
}

static void test_variables(void) {
    PieceConfig c = test_config();
    TestRun *r = test_open(&c);
    const Model *m = &r->model;
    CHECK(m->nvars == 13); /* key + 12 pitches */
    CHECK(m->span == 16);
    CHECK(m->nsections == 1);
    CHECK(domain_count(&m->initial[m->key[0]]) == 1);
    CHECK(domain_count(&m->initial[m->pitch[0]]) == 13);
    CHECK(!domain_contains(&m->initial[m->pitch[0]], PITCH_REST));
    CHECK(m->tie[1] == -1);
    CHECK(count_rule(m, CID_CONSONANCE) == 2); /* steps 4 and 8 */
    CHECK(count_rule(m, CID_LOCK) == 0);
    CHECK(count_rule(m, CID_SPACING) == 0 && count_rule(m, CID_CROSSING) == 0);
    CHECK(model_rule_used(m, CID_LEAP));
    CHECK(!model_rule_used(m, CID_CHORD));
    test_close(r);

    /* given notes lock, and the spacing and crossing rules cover every
     * step where two voices sound (steps 4 to 11) */
    test_set(&c, "melody=60,?,?,64");
    test_set(&c, "max_spacing=12");
    test_set(&c, "crossing=0");
    r = test_open(&c);
    m = &r->model;
    CHECK(count_rule(m, CID_LOCK) == 2);
    CHECK(count_rule(m, CID_SPACING) == 8);
    CHECK(count_rule(m, CID_CROSSING) == 8);
    CHECK(count_rule(m, CID_CONSONANCE) == 2);
    test_close(r);
    c = test_config();

    test_set(&c, "rhythm=1");
    test_set(&c, "harmony=1");
    test_set(&c, "key=search");
    test_set(&c, "mode=search");
    test_set(&c, "modulate_at=8");
    r = test_open(&c);
    m = &r->model;
    CHECK(m->nsections == 2);
    CHECK(m->nbars == 4);
    CHECK(m->nvars == 2 + 4 + 12 + 11);
    CHECK(domain_count(&m->initial[m->key[0]]) == 24);
    CHECK(domain_contains(&m->initial[m->pitch[0]], PITCH_REST));
    CHECK(m->tie[0] == -1 && m->tie[1] >= 0);
    CHECK(count_rule(m, CID_TIE) == 11);
    CHECK(count_rule(m, CID_MODULATION) == 1);
    CHECK(count_rule(m, CID_PROGRESSION) == 2); /* bars 1-2 and 3-4; bar 3 changes key */
    CHECK(count_term(m, TERM_KEY) == 1);
    CHECK(count_term(m, TERM_KEY_DISTANCE) == 1);
    CHECK(m->max_rests_con >= 0);
    char buf[32];
    var_label(m, m->key[1], buf, sizeof(buf));
    CHECK(strcmp(buf, "key2") == 0);
    var_label(m, m->tie[3], buf, sizeof(buf));
    CHECK(strcmp(buf, "tie3") == 0);
    var_label(m, m->chord[2], buf, sizeof(buf));
    CHECK(strcmp(buf, "chord2") == 0);
    value_label(m, m->pitch[0], PITCH_REST, buf, sizeof(buf));
    CHECK(strcmp(buf, "rest") == 0);
    value_label(m, m->tie[3], TIE_HOLD, buf, sizeof(buf));
    CHECK(strcmp(buf, "hold") == 0);
    test_close(r);

    /* one leading-tone rule per melody step; double leaps once per
     * triple, shared by a plain or retrograde follower but not by a
     * pitch-class inversion; contrary motion wherever both voices move */
    c = test_config();
    CHECK(c.double_leaps == 1 && c.leading_tone == 0);
    test_set(&c, "leading_tone=1");
    test_set(&c, "double_leaps=0");
    test_set(&c, "w_contrary=3");
    r = test_open(&c);
    m = &r->model;
    CHECK(count_rule(m, CID_LEADING_TONE) == 11);
    CHECK(count_rule(m, CID_DOUBLE_LEAP) == 10);
    CHECK(count_term(m, TERM_CONTRARY) == 7); /* steps 4-5 to 10-11 */
    test_close(r);
    test_set(&c, "retrograde=1");
    r = test_open(&c);
    CHECK(count_rule(&r->model, CID_DOUBLE_LEAP) == 10);
    test_close(r);
    c.retrograde = 0;
    test_set(&c, "invert=1");
    test_set(&c, "invert_mod12=1");
    r = test_open(&c);
    CHECK(count_rule(&r->model, CID_DOUBLE_LEAP) == 20);
    test_close(r);
    c = test_config();
    r = test_open(&c);
    CHECK(count_rule(&r->model, CID_LEADING_TONE) == 0);
    CHECK(count_rule(&r->model, CID_DOUBLE_LEAP) == 0);
    CHECK(count_term(&r->model, TERM_CONTRARY) == 0);
    test_close(r);
}

static const Constraint *find(const Model *m, int type, int time) {
    for (int i = 0; i < m->ncons; i++) {
        if (m->cons[i].type == type && (time < 0 || m->cons[i].time == time)) return &m->cons[i];
    }
    return NULL;
}

static void test_predicates(void) {
    PieceConfig c = test_config();
    test_set(&c, "rhythm=1");
    TestRun *r = test_open(&c);
    const Model *m = &r->model;

    const Constraint *tie = find(m, C_TIE, 5);
    CHECK(tie != NULL && tie->n == 3);
    int v[SCOPE_MAX];
    v[0] = TIE_HOLD;
    v[1] = 64;
    v[2] = 64;
    CHECK(constraint_holds(m, tie, v));
    v[2] = 65;
    CHECK(!constraint_holds(m, tie, v));
    v[0] = TIE_NOTE;
    CHECK(constraint_holds(m, tie, v));
    v[0] = TIE_HOLD;
    v[1] = PITCH_REST;
    v[2] = PITCH_REST;
    CHECK(!constraint_holds(m, tie, v)); /* a rest cannot be tied */

    const Constraint *cons = find(m, C_CONSONANCE, 4);
    CHECK(cons != NULL);
    int a = cons->slot[0];
    int b = cons->slot[1];
    v[a] = 60;
    v[b] = 64;
    CHECK(constraint_holds(m, cons, v));
    v[b] = 65;
    CHECK(!constraint_holds(m, cons, v));
    v[b] = 60;
    CHECK(!constraint_holds(m, cons, v));
    v[b] = PITCH_REST;
    CHECK(constraint_holds(m, cons, v)); /* a silent voice cannot clash */

    /* approach k=0 applies only when the last note is attacked */
    const Constraint *approach = NULL;
    for (int i = 0; i < m->ncons; i++) {
        if (m->cons[i].type == C_CADENCE_APPROACH && m->cons[i].param == 0) approach = &m->cons[i];
    }
    CHECK(approach != NULL && approach->nslots == 3);
    int key = key_id(0, MODE_MAJOR);
    v[approach->slot[0]] = 60;
    v[approach->slot[1]] = key;
    v[approach->slot[2]] = TIE_NOTE;
    CHECK(!constraint_holds(m, approach, v));
    v[approach->slot[0]] = 71;
    CHECK(constraint_holds(m, approach, v));
    v[approach->slot[0]] = 60;
    v[approach->slot[2]] = TIE_HOLD;
    CHECK(constraint_holds(m, approach, v));
    test_close(r);
}

/* Changing one variable changes the total energy by exactly the change
 * in its choice cost when every other variable is fixed. */
static void check_energy_consistency(const PieceConfig *c, unsigned seed) {
    TestRun *r = test_open(c);
    const Model *m = &r->model;
    int values[VAR_MAX];
    unsigned x = seed * 2654435761u + 1u;
    for (int v = 0; v < m->nvars; v++) {
        int dom[128];
        int n = domain_collect(&m->initial[v], dom);
        x = x * 1103515245u + 12345u;
        values[v] = dom[(x >> 8) % (unsigned)n];
    }
    for (int v = 0; v < m->nvars; v++) {
        domain_clear(&r->state.domains[v]);
        domain_add(&r->state.domains[v], values[v]);
    }
    for (int var = 0; var < m->nvars; var++) {
        int dom[128];
        int n = domain_collect(&m->initial[var], dom);
        int costs[128];
        MidiDomain saved = r->state.domains[var];
        domain_fill_range(&r->state.domains[var], 0, 127); /* let var look open */
        solver_choice_costs(&r->state, var, dom, n, costs, NULL);
        r->state.domains[var] = saved;
        int keep = values[var];
        values[var] = dom[0];
        int base = model_energy(m, values, NULL);
        for (int k = 1; k < n; k++) {
            values[var] = dom[k];
            CHECK(model_energy(m, values, NULL) - base == costs[k] - costs[0]);
        }
        values[var] = keep;
    }
    int breakdown[TERM_COUNT];
    int total = model_energy(m, values, breakdown);
    int sum = 0;
    for (int t = 0; t < TERM_COUNT; t++) sum += breakdown[t];
    CHECK(sum == total);
    test_close(r);
}

static void test_energy(void) {
    static const char *const shapes[][6] = {
        {"voices=2", NULL},
        {"voices=4", "delay=3", NULL},
        {"voices=3", "delay_1=2", "delay_2=7", "phase=1", NULL},
        {"invert=1", "axis=68", "range_high=76", NULL},
        {"invert=1", "invert_mod12=1", "axis=62", NULL},
        {"retrograde=1", "transpose=7", NULL},
        {"augment=2", NULL},
        {"diminish=2", "delay=3", NULL},
        {"cyclic=1", "voices=3", NULL},
        {"rhythm=1", "harmony=1", "w_motif=3", "motif_a=2", "motif_b=-1", NULL},
        {"key=search", "mode=search", "modulate_at=6", "pc_weight=0,1,2,3,4,5,6,7,8,9,10,11", NULL},
        {"poly_meter=1", "rhythm=1", "voices=3", NULL},
    };
    for (size_t s = 0; s < sizeof(shapes) / sizeof(shapes[0]); s++) {
        PieceConfig c = test_config();
        test_set(&c, "w_dissonance=3");
        test_set(&c, "w_parallel=2");
        test_set(&c, "w_contrary=1");
        for (int k = 0; shapes[s][k] != NULL; k++) test_set(&c, shapes[s][k]);
        for (unsigned seed = 1; seed <= 4; seed++) check_energy_consistency(&c, seed + 10u * (unsigned)s);
    }
}

int main(void) {
    test_variables();
    test_predicates();
    test_energy();
    printf("ok\n");
    return 0;
}
