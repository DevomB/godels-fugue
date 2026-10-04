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

/* The first term of a rule at a step with nslots slots, or NULL. */
static const Constraint *find_term(const Model *m, int rule, int time, int nslots) {
    for (int i = 0; i < m->nterms; i++) {
        const Constraint *t = &m->terms[i];
        if (t->rule == rule && t->time == time && t->nslots == nslots) return t;
    }
    return NULL;
}

/* Cost of a term over ties alone, given in slot order as N (note) and H (hold). */
static int tie_cost(const Model *m, const Constraint *t, const char *ties) {
    int v[SCOPE_MAX];
    CHECK(t != NULL && (int)strlen(ties) == t->nslots);
    for (int k = 0; k < t->nslots; k++) v[t->slot[k]] = ties[k] == 'H' ? TIE_HOLD : TIE_NOTE;
    return term_cost(m, t, v);
}

/* Cost of a run term for two pitches and its ties, N or H in slot order. */
static int run_cost(const Model *m, const Constraint *t, int a, int b, const char *ties) {
    int v[SCOPE_MAX];
    CHECK(t != NULL && (int)strlen(ties) == t->nslots - 2);
    v[t->slot[0]] = a;
    v[t->slot[1]] = b;
    for (int k = 2; k < t->nslots; k++) v[t->slot[k]] = ties[k - 2] == 'H' ? TIE_HOLD : TIE_NOTE;
    return term_cost(m, t, v);
}

/* On the eighth grid a bar is eight steps: one chord, one strong step on
 * its first beat, and rhythm terms that count a beat as two steps. */
static void test_eighth_grid(void) {
    PieceConfig c = test_config();
    test_set(&c, "grid=eighth");
    test_set(&c, "length=24");
    test_set(&c, "delay=8");
    test_set(&c, "rhythm=1");
    test_set(&c, "harmony=1");
    TestRun *r = test_open(&c);
    const Model *m = &r->model;
    CHECK(m->span == 32 && m->nbars == 4);
    /* both voices sound on the first beats of bars 2 and 3, steps 8 and 16 */
    CHECK(count_rule(m, CID_CONSONANCE) == 2);
    CHECK(count_rule(m, CID_CHORD) == 6); /* each voice on three downbeats */
    CHECK(count_rule(m, CID_PROGRESSION) == 3);
    for (int i = 0; i < m->ncons; i++) {
        const Constraint *k = &m->cons[i];
        if (k->type == C_CONSONANCE || k->type == C_CHORD_TONE) CHECK(k->time % 8 == 0);
        if (k->type == C_CHORD_TONE) CHECK(k->vars[k->slot[1]] == m->chord[k->time / 8]);
    }
    CHECK(count_term(m, TERM_NONCHORD) == 2 * (24 - 3)); /* every other sounding step */
    CHECK(count_term(m, TERM_CHORD) == 4);
    CHECK(count_term(m, TERM_RHYTHM) == 3);
    CHECK(count_term(m, TERM_SYNCOPATION) == 5 + 11);
    char where[128];
    constraint_describe(m, find(m, C_CONSONANCE, 16), where, sizeof(where));
    CHECK(strstr(where, "at step 16 (bar 3)") != NULL);

    /* a note attacked on beat 2 (step 2) and held over beat 3 (step 4) */
    const Constraint *over = find_term(m, TERM_SYNCOPATION, 4, 3);
    CHECK(over != NULL && over->vars[over->slot[0]] == m->tie[2]);
    CHECK(tie_cost(m, over, "NHH") == c.w_syncopation);
    CHECK(tie_cost(m, over, "NHN") == 0 && tie_cost(m, over, "HHH") == 0);
    /* an off-beat eighth (step 1) held across beat 2 (step 2) */
    const Constraint *off = find_term(m, TERM_SYNCOPATION, 2, 2);
    CHECK(off != NULL && off->vars[off->slot[0]] == m->tie[1]);
    CHECK(tie_cost(m, off, "NH") == c.w_syncopation);
    CHECK(tie_cost(m, off, "NN") == 0 && tie_cost(m, off, "HH") == 0);
    CHECK(find_term(m, TERM_SYNCOPATION, 3, 2) == NULL); /* step 3 starts no beat */
    /* a bar of eight eighths or of four quarters is plain; any mix is not */
    const Constraint *bar = find_term(m, TERM_RHYTHM, 8, 7);
    CHECK(bar != NULL && bar->vars[bar->slot[0]] == m->tie[9]);
    CHECK(tie_cost(m, bar, "NNNNNNN") == c.w_rhythm);
    CHECK(tie_cost(m, bar, "HNHNHNH") == c.w_rhythm);
    CHECK(tie_cost(m, bar, "HNHNHNN") == 0);
    CHECK(tie_cost(m, bar, "NNHNHNH") == 0);
    CHECK(tie_cost(m, bar, "HHHNHHH") == 0); /* two half notes */

    /* two eighths in a row move by step: notes 5 and 6 are eighths when
     * the ties at 5, 6 and 7 are all attacks */
    CHECK(count_term(m, TERM_RUN) == 23);
    const Constraint *run = find_term(m, TERM_RUN, 5, 5);
    CHECK(run != NULL && run->vars[run->slot[2]] == m->tie[5]);
    CHECK(run_cost(m, run, 60, 64, "NNN") == c.w_run);
    CHECK(run_cost(m, run, 72, 60, "NNN") == c.w_run);
    CHECK(run_cost(m, run, 60, 62, "NNN") == 0);         /* a step */
    CHECK(run_cost(m, run, 60, 64, "HNN") == 0);         /* the first note is longer */
    CHECK(run_cost(m, run, 60, 64, "NNH") == 0);         /* so is the second */
    CHECK(run_cost(m, run, PITCH_REST, 64, "NNN") == 0); /* a rest is no note */
    /* the first note has no tie, and the last has no next step */
    CHECK(run_cost(m, find_term(m, TERM_RUN, 0, 4), 60, 67, "NN") == c.w_run);
    CHECK(run_cost(m, find_term(m, TERM_RUN, 22, 4), 67, 60, "NN") == c.w_run);
    CHECK(run_cost(m, find_term(m, TERM_RUN, 22, 4), 67, 60, "HN") == 0);
    test_close(r);

    /* the same piece on the quarter grid keeps its terms */
    c.grid = GRID_QUARTER;
    r = test_open(&c);
    m = &r->model;
    CHECK(m->span == 32 && m->nbars == 8);
    CHECK(count_rule(m, CID_CONSONANCE) == 4); /* steps 8, 12, 16, 20 */
    CHECK(count_term(m, TERM_SYNCOPATION) == 11);
    CHECK(count_term(m, TERM_RHYTHM) == 6);
    CHECK(count_term(m, TERM_RUN) == 0);
    over = find_term(m, TERM_SYNCOPATION, 4, 2);
    CHECK(tie_cost(m, over, "NH") == c.w_syncopation && tie_cost(m, over, "HH") == 0);
    bar = find_term(m, TERM_RHYTHM, 4, 3);
    CHECK(tie_cost(m, bar, "NNN") == c.w_rhythm && tie_cost(m, bar, "HNH") == 0);
    test_close(r);
}

/* On the sixteenth grid a beat is four steps and a bar sixteen: one chord
 * and one strong step a bar; syncopation covers a note attacked on any of a
 * beat's later sixteenths and held across the next beat; a bar of plain
 * sixteenths, eighths or quarters lacks variety; and two notes in a row
 * that are both shorter than a beat move by step. */
static void test_sixteenth_grid(void) {
    PieceConfig c = test_config();
    test_set(&c, "grid=sixteenth");
    test_set(&c, "length=40");
    test_set(&c, "delay=16");
    test_set(&c, "rhythm=1");
    test_set(&c, "harmony=1");
    test_set(&c, "max_hold=7");
    TestRun *r = test_open(&c);
    const Model *m = &r->model;
    CHECK(m->span == 56 && m->nbars == 4);
    /* both voices sound on the first beats of bars 2 and 3, steps 16 and 32 */
    CHECK(count_rule(m, CID_CONSONANCE) == 2);
    CHECK(count_rule(m, CID_CHORD) == 6); /* each voice on three downbeats */
    for (int i = 0; i < m->ncons; i++) {
        const Constraint *k = &m->cons[i];
        if (k->type == C_CONSONANCE || k->type == C_CHORD_TONE) CHECK(k->time % 16 == 0);
        if (k->type == C_CHORD_TONE) CHECK(k->vars[k->slot[1]] == m->chord[k->time / 16]);
    }
    CHECK(count_term(m, TERM_CHORD) == 4);

    /* beats 2 and 4 held over 3 and 1 at steps 8, 16, 24 and 32; an attack
     * one, two or three sixteenths before each of the nine later beats */
    CHECK(count_term(m, TERM_SYNCOPATION) == 4 + 3 * 9);
    const Constraint *over = find_term(m, TERM_SYNCOPATION, 8, 5);
    CHECK(over != NULL && over->vars[over->slot[0]] == m->tie[4]);
    CHECK(tie_cost(m, over, "NHHHH") == c.w_syncopation);
    CHECK(tie_cost(m, over, "NHHHN") == 0 && tie_cost(m, over, "HHHHH") == 0);
    CHECK(find_term(m, TERM_SYNCOPATION, 4, 5) == NULL); /* beat 2 is not held over */
    const Constraint *a = find_term(m, TERM_SYNCOPATION, 8, 2); /* the "a" of beat 2 */
    CHECK(a != NULL && a->vars[a->slot[0]] == m->tie[7]);
    CHECK(tie_cost(m, a, "NH") == c.w_syncopation && tie_cost(m, a, "NN") == 0);
    const Constraint *half = find_term(m, TERM_SYNCOPATION, 8, 3); /* its "and" */
    CHECK(half != NULL && half->vars[half->slot[0]] == m->tie[6]);
    CHECK(tie_cost(m, half, "NHH") == c.w_syncopation);
    CHECK(tie_cost(m, half, "NHN") == 0 && tie_cost(m, half, "HHH") == 0);
    const Constraint *e = find_term(m, TERM_SYNCOPATION, 8, 4); /* its "e" */
    CHECK(e != NULL && e->vars[e->slot[0]] == m->tie[5]);
    CHECK(tie_cost(m, e, "NHHH") == c.w_syncopation && tie_cost(m, e, "NNHH") == 0);
    CHECK(find_term(m, TERM_SYNCOPATION, 6, 3) == NULL); /* step 6 starts no beat */

    /* a bar of sixteenths, eighths or quarters is plain; any mix is not */
    CHECK(count_term(m, TERM_RHYTHM) == 2);
    const Constraint *bar = find_term(m, TERM_RHYTHM, 16, 15);
    CHECK(bar != NULL && bar->vars[bar->slot[0]] == m->tie[17]);
    CHECK(tie_cost(m, bar, "NNNNNNNNNNNNNNN") == c.w_rhythm);
    CHECK(tie_cost(m, bar, "HNHNHNHNHNHNHNH") == c.w_rhythm);
    CHECK(tie_cost(m, bar, "HHHNHHHNHHHNHHH") == c.w_rhythm);
    CHECK(tie_cost(m, bar, "HNHNHNHNHNHNHNN") == 0);
    CHECK(tie_cost(m, bar, "NNNHHHHNHHHNHHH") == 0);
    CHECK(tie_cost(m, bar, "HHHHHHHNHHHHHHH") == 0); /* two half notes */

    /* notes shorter than a beat that leap: the notes at steps 9 and 10,
     * with the ties of steps 7 to 13 */
    CHECK(count_term(m, TERM_RUN) == 39);
    const Constraint *run = find_term(m, TERM_RUN, 9, 9);
    CHECK(run != NULL && run->vars[run->slot[2]] == m->tie[7]);
    CHECK(run->vars[run->slot[8]] == m->tie[13]);
    CHECK(run_cost(m, run, 60, 64, "NNNNNNN") == c.w_run);  /* two sixteenths */
    CHECK(run_cost(m, run, 60, 64, "NNHNHNN") == c.w_run);  /* two eighths */
    CHECK(run_cost(m, run, 60, 64, "NHHNNNN") == c.w_run);  /* dotted eighth, sixteenth */
    CHECK(run_cost(m, run, 60, 64, "NNNNHHN") == c.w_run);  /* sixteenth, dotted eighth */
    CHECK(run_cost(m, run, 60, 62, "NNNNNNN") == 0);        /* a step */
    CHECK(run_cost(m, run, 60, 64, "HHHNNNN") == 0);        /* a quarter, then a sixteenth */
    CHECK(run_cost(m, run, 60, 64, "NNNNHHH") == 0);        /* a sixteenth, then a quarter */
    CHECK(run_cost(m, run, 60, 64, "NNNHNNN") == 0);        /* one note held on */
    CHECK(run_cost(m, run, PITCH_REST, 64, "NNNNNNN") == 0); /* a rest is no note */
    /* the first note starts the melody, and the last ends it */
    const Constraint *first = find_term(m, TERM_RUN, 0, 6);
    CHECK(first != NULL && first->vars[first->slot[2]] == m->tie[1]);
    CHECK(run_cost(m, first, 60, 67, "NNHH") == c.w_run);
    CHECK(run_cost(m, first, 60, 67, "NHHH") == 0); /* the second note is a quarter */
    const Constraint *last = find_term(m, TERM_RUN, 38, 6);
    CHECK(last != NULL && last->vars[last->slot[5]] == m->tie[39]);
    CHECK(run_cost(m, last, 67, 60, "HHNN") == c.w_run);
    CHECK(run_cost(m, last, 67, 60, "HHHN") == 0); /* the first note is a quarter */
    test_close(r);
}

/* The figure parts of the beat at step start, for its ties in step order (N
 * an attack, H a held step), and their sum. The melody's first step has no
 * tie, so the first beat's ties are three. */
static int figure_cost(const Model *m, int start, const char *ties, int *parts) {
    int missing = start == 0 ? 1 : 0;
    int total = 0;
    for (int steps = 2; steps <= 4; steps++) {
        char prefix[5];
        int n = steps - missing;
        memcpy(prefix, ties, (size_t)n);
        prefix[n] = '\0';
        const Constraint *t = find_term(m, TERM_FIGURE, start, n);
        CHECK(t != NULL && t->param == steps);
        parts[steps - 2] = tie_cost(m, t, prefix);
        CHECK(parts[steps - 2] >= 0);
        total += parts[steps - 2];
    }
    return total;
}

/* On the sixteenth grid each whole beat of the melody costs its figure,
 * graded from 0 (a quarter, two eighths, a held beat) to 4 (a sixteenth on
 * the "e" after a held one); no other grid has one. The cost comes in three
 * parts, over the ties of the beat's first two, three and four steps: the
 * least grade any figure starting so can reach, then how much it rises. */
static void test_figures(void) {
    PieceConfig c = test_config();
    test_set(&c, "grid=sixteenth");
    test_set(&c, "length=22");
    test_set(&c, "rhythm=1");
    test_set(&c, "max_hold=7");
    test_set(&c, "w_figure=2");
    TestRun *r = test_open(&c);
    const Model *m = &r->model;
    CHECK(strcmp(term_name(TERM_FIGURE), "figure") == 0);
    /* beats at steps 0, 4, 8, 12 and 16; steps 20 and 21 make no whole beat */
    CHECK(count_term(m, TERM_FIGURE) == 3 * 5);
    CHECK(find_term(m, TERM_FIGURE, 20, 2) == NULL);
    const Constraint *whole = find_term(m, TERM_FIGURE, 4, 4);
    CHECK(whole != NULL && whole->vars[whole->slot[0]] == m->tie[4]);
    CHECK(whole->vars[whole->slot[3]] == m->tie[7]);
    static const struct {
        const char *ties;
        int grade;
    } figures[] = {
        {"NHHH", 0}, {"NHNH", 0}, {"HHHH", 0}, /* x... x.x. .... */
        {"NHNN", 1}, {"NNNH", 1}, {"NNNN", 1}, /* x.xx xxx. xxxx */
        {"NHHN", 1}, {"HHNH", 1},              /* x..x ..x. */
        {"NNHH", 2},                           /* xx.. */
        {"NNHN", 3}, {"HHHN", 3}, {"HHNN", 3}, {"HNNN", 3}, /* xx.x ...x ..xx .xxx */
        {"HNHH", 4}, {"HNHN", 4}, {"HNNH", 4},              /* .x.. .x.x .xx. */
    };
    int parts[3];
    for (size_t k = 0; k < sizeof(figures) / sizeof(figures[0]); k++)
        CHECK(figure_cost(m, 4, figures[k].ties, parts) == 2 * figures[k].grade);
    /* after two steps a beat is charged the least it can still cost: x. can
     * become x... for nothing, xx at best xxx. or xxxx, .x at best .xxx */
    CHECK(figure_cost(m, 4, "NHHH", parts) == 0 && parts[0] == 0);
    CHECK(figure_cost(m, 4, "NNNN", parts) == 2 && parts[0] == 2 && parts[1] == 0);
    CHECK(figure_cost(m, 4, "NNHH", parts) == 4 && parts[0] == 2 && parts[1] == 2);
    CHECK(figure_cost(m, 4, "HNHH", parts) == 8 && parts[0] == 6 && parts[1] == 2);
    CHECK(figure_cost(m, 4, "HHHN", parts) == 6 && parts[0] == 0 && parts[2] == 6);
    /* the melody's first step has no tie and always starts a note */
    const Constraint *first = find_term(m, TERM_FIGURE, 0, 3);
    CHECK(first != NULL && first->vars[first->slot[0]] == m->tie[1] && first->param == 4);
    CHECK(figure_cost(m, 0, "HHH", parts) == 0);     /* x... */
    CHECK(figure_cost(m, 0, "NNN", parts) == 2);     /* xxxx */
    CHECK(figure_cost(m, 0, "NHH", parts) == 2 * 2); /* xx.. */
    CHECK(figure_cost(m, 0, "HNN", parts) == 2);     /* x.xx */
    test_close(r);

    /* none without the weight, without rhythm, or on the eighth grid */
    test_set(&c, "w_figure=0");
    r = test_open(&c);
    CHECK(count_term(&r->model, TERM_FIGURE) == 0);
    test_close(r);
    test_set(&c, "w_figure=3");
    test_set(&c, "rhythm=0");
    r = test_open(&c);
    CHECK(count_term(&r->model, TERM_FIGURE) == 0);
    test_close(r);
    test_set(&c, "rhythm=1");
    test_set(&c, "grid=eighth");
    r = test_open(&c);
    CHECK(count_term(&r->model, TERM_FIGURE) == 0);
    test_close(r);
}

/* w_step charges each melodic interval by its size beyond a whole step:
 * (semitones - 1) / 2, so a third 1, a fourth 2, a fifth 3, an octave 5. */
static void test_step(void) {
    PieceConfig c = test_config();
    TestRun *r = test_open(&c);
    CHECK(c.w_step == 0 && count_term(&r->model, TERM_STEP) == 0);
    test_close(r);
    test_set(&c, "w_step=2");
    test_set(&c, "rhythm=1");
    r = test_open(&c);
    const Model *m = &r->model;
    CHECK(count_term(m, TERM_STEP) == c.length - 1);
    const Constraint *t = find_term(m, TERM_STEP, 3, 2);
    CHECK(t != NULL && t->vars[t->slot[0]] == m->pitch[3] && t->vars[t->slot[1]] == m->pitch[4]);
    static const int units[13] = {0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5};
    int v[SCOPE_MAX];
    for (int d = 0; d <= 12; d++) {
        v[t->slot[0]] = 66;
        v[t->slot[1]] = 66 + d;
        CHECK(term_cost(m, t, v) == 2 * units[d]);
        v[t->slot[1]] = 66 - d;
        CHECK(term_cost(m, t, v) == 2 * units[d]);
    }
    v[t->slot[1]] = PITCH_REST;
    CHECK(term_cost(m, t, v) == 0);
    test_close(r);

    /* the same on the eighth grid */
    test_set(&c, "grid=eighth");
    r = test_open(&c);
    CHECK(count_term(&r->model, TERM_STEP) == c.length - 1);
    test_close(r);
}

/* w_arc weighs every note against the one at the climax: a note above it
 * costs 1 + semitones / 3, and so does a note of the opening quarter less
 * than a major third below it. */
static void test_arc(void) {
    PieceConfig c = test_config();
    TestRun *r = test_open(&c);
    CHECK(c.w_arc == 0 && c.climax == 66 && count_term(&r->model, TERM_ARC) == 0);
    test_close(r);
    test_set(&c, "w_arc=2");
    r = test_open(&c);
    const Model *m = &r->model;
    /* twelve notes: 66% of the way is note 7, moved to the strong beat at 8 */
    CHECK(count_term(m, TERM_ARC) == c.length - 1);
    const Constraint *t = find_term(m, TERM_ARC, 0, 2);
    CHECK(t != NULL && t->vars[t->slot[1]] == m->pitch[8] && t->param == 4);
    int v[SCOPE_MAX];
    v[t->slot[1]] = 72;
    v[t->slot[0]] = 68;
    CHECK(term_cost(m, t, v) == 0); /* the opening a major third below the peak */
    v[t->slot[0]] = 69;
    CHECK(term_cost(m, t, v) == 2); /* a minor third below: too close */
    v[t->slot[0]] = 75;
    CHECK(term_cost(m, t, v) == 6); /* above the peak */
    v[t->slot[0]] = PITCH_REST;
    CHECK(term_cost(m, t, v) == 0);
    const Constraint *late = find_term(m, TERM_ARC, 10, 2);
    CHECK(late != NULL && late->param == 0);
    v[late->slot[1]] = 72;
    v[late->slot[0]] = 72;
    CHECK(term_cost(m, late, v) == 0); /* level with the peak */
    v[late->slot[0]] = 74;
    CHECK(term_cost(m, late, v) == 2);
    test_close(r);
}

/* w_sequence compares each interval with the one at the same place a bar
 * before: the same, or a semitone off the same way, is free; the same
 * direction costs half, the other way all of it. */
static void test_sequence(void) {
    PieceConfig c = test_config();
    test_set(&c, "w_sequence=4");
    TestRun *r = test_open(&c);
    const Model *m = &r->model;
    /* a bar is four quarter notes: the intervals into notes 5 to 11 */
    CHECK(count_term(m, TERM_SEQUENCE) == c.length - 5);
    const Constraint *t = find_term(m, TERM_SEQUENCE, 6, 4);
    CHECK(t != NULL && t->vars[t->slot[0]] == m->pitch[1] && t->vars[t->slot[3]] == m->pitch[6]);
    int v[SCOPE_MAX];
    v[t->slot[0]] = 60;
    v[t->slot[1]] = 64; /* a major third up a bar before */
    v[t->slot[2]] = 62;
    v[t->slot[3]] = 66;
    CHECK(term_cost(m, t, v) == 0); /* the same */
    v[t->slot[3]] = 65;
    CHECK(term_cost(m, t, v) == 0); /* a minor third: the diatonic echo */
    v[t->slot[3]] = 69;
    CHECK(term_cost(m, t, v) == 2); /* a larger leap up */
    v[t->slot[3]] = 60;
    CHECK(term_cost(m, t, v) == 4); /* down instead */
    v[t->slot[3]] = 62;
    CHECK(term_cost(m, t, v) == 4); /* a repeat against a leap */
    v[t->slot[2]] = PITCH_REST;
    CHECK(term_cost(m, t, v) == 0);
    test_close(r);
}

/* The curve term of each note carries its target: the arch until a
 * curve is drawn, then the drawn curve stretched over the melody. */
static void check_targets(const PieceConfig *c, const int *want) {
    TestRun *r = test_open(c);
    int seen = 0;
    for (int k = 0; k < r->model.nterms; k++) {
        const Constraint *t = &r->model.terms[k];
        if (t->rule != TERM_CURVE) continue;
        CHECK(t->param == want[t->time]);
        seen++;
    }
    CHECK(seen == c->length);
    test_close(r);
}

static void test_tension(void) {
    PieceConfig c = test_config();
    int arch[12];
    for (int i = 0; i < 12; i++) arch[i] = tension_target(i, 12);
    check_targets(&c, arch);
    test_set(&c, "tension=0 4 0");
    const int peak[12] = {0, 1, 1, 2, 3, 4, 4, 3, 2, 1, 1, 0};
    check_targets(&c, peak);
    test_set(&c, "tension=3");
    const int flat[12] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
    check_targets(&c, flat);
    test_set(&c, "tension=0,1,2,3,4,4,3,2,1,0,1,2");
    const int full[12] = {0, 1, 2, 3, 4, 4, 3, 2, 1, 0, 1, 2};
    check_targets(&c, full);

    /* the cost is the distance from the target: E (gravity 0) under a
     * target of 3 costs three times w_curve */
    test_set(&c, "w_curve=2");
    TestRun *r = test_open(&c);
    const Constraint *t = NULL;
    for (int k = 0; k < r->model.nterms; k++) {
        if (r->model.terms[k].rule == TERM_CURVE && r->model.terms[k].time == 3)
            t = &r->model.terms[k];
    }
    CHECK(t != NULL);
    int v[SCOPE_MAX];
    v[t->slot[0]] = 64;
    v[t->slot[1]] = key_id(0, MODE_MAJOR);
    CHECK(term_cost(&r->model, t, v) == 6);
    v[t->slot[0]] = 71; /* B, gravity 3 */
    CHECK(term_cost(&r->model, t, v) == 0);
    test_close(r);
}

/* One mirror constraint per pair of notes, and the middle note of an
 * odd length alone. */
static void test_mirror(void) {
    PieceConfig c = test_config();
    TestRun *r = test_open(&c);
    CHECK(count_rule(&r->model, CID_MIRROR) == 0);
    test_close(r);
    CHECK(strcmp(rule_name(CID_MIRROR), "mirror") == 0);
    CHECK(strcmp(rule_name(CID_MODULATION), "modulation") == 0);
    CHECK(strcmp(rule_name(CID_REFUTED), "search") == 0);
    CHECK(strcmp(rule_name(CID_LEARNED), "learned conflict") == 0);

    test_set(&c, "mirror=1");
    r = test_open(&c);
    const Model *m = &r->model;
    CHECK(count_rule(m, CID_MIRROR) == 6);
    CHECK(model_rule_used(m, CID_MIRROR));
    const Constraint *pair = find(m, C_MIRROR, 2);
    CHECK(pair != NULL && pair->n == 2 && pair->nslots == 2 && pair->param == 66);
    CHECK(pair->vars[0] == m->pitch[2] && pair->vars[1] == m->pitch[9]);
    int v[SCOPE_MAX];
    v[0] = 60;
    v[1] = 72;
    CHECK(constraint_holds(m, pair, v));
    v[1] = 71;
    CHECK(!constraint_holds(m, pair, v));
    v[0] = PITCH_REST;
    CHECK(!constraint_holds(m, pair, v)); /* a rest pairs only with a rest */
    v[1] = PITCH_REST;
    CHECK(constraint_holds(m, pair, v));
    v[0] = 66;
    v[1] = 66;
    CHECK(constraint_holds(m, pair, v));
    char why[128];
    constraint_describe(m, pair, why, sizeof(why));
    CHECK(strstr(why, "notes 2 and 9 mirror each other around F#4") != NULL);
    test_close(r);

    test_set(&c, "length=13");
    test_set(&c, "rhythm=1");
    r = test_open(&c);
    m = &r->model;
    CHECK(count_rule(m, CID_MIRROR) == 7);
    const Constraint *middle = find(m, C_MIRROR, 6);
    CHECK(middle != NULL && middle->n == 1 && middle->nslots == 1);
    CHECK(middle->vars[0] == m->pitch[6]);
    v[0] = 66;
    CHECK(constraint_holds(m, middle, v));
    v[0] = 67;
    CHECK(!constraint_holds(m, middle, v));
    v[0] = PITCH_REST;
    CHECK(!constraint_holds(m, middle, v)); /* the middle note is the axis itself */
    constraint_describe(m, middle, why, sizeof(why));
    CHECK(strstr(why, "middle note 6") != NULL);
    test_close(r);
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
    static const char *const shapes[][14] = {
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
        {"tension=0,?,4,1", "w_curve=3", "mirror=1", "rhythm=1", NULL},
        {"grid=eighth", "rhythm=1", "harmony=1", "length=20", "delay=6", NULL},
        {"grid=eighth", "voices=3", "poly_meter=1", "rhythm=1", "max_hold=5", NULL},
        {"w_step=2", "rhythm=1", "voices=3", NULL},
        {"grid=eighth", "w_step=3", "rhythm=1", "max_hold=3", NULL},
        {"grid=sixteenth", "rhythm=1", "harmony=1", "length=24", "delay=6", NULL},
        {"grid=sixteenth", "voices=3", "poly_meter=1", "rhythm=1", "max_hold=7", NULL},
        {"grid=sixteenth", "w_step=3", "rhythm=1", "length=36", "max_hold=3", NULL},
        {"grid=sixteenth", "voices=3", "delay=5", "length=20", NULL},
        {"w_arc=3", "w_sequence=2", "rhythm=1", "voices=3", NULL},
        {"grid=eighth", "w_arc=2", "climax=40", "w_sequence=3", "rhythm=1", NULL},
        {"grid=eighth", "length=40", "phrase=1", "w_sequence=3", "rhythm=1", "harmony=1",
         "ensemble=strings", "voices=3", "transpose_1=-12", "transpose_2=-24", "range_low=40",
         "range_high=88", NULL},
        {"grid=sixteenth", "length=48", "phrase=1", "w_sequence=2", "rhythm=1", "max_hold=3", NULL},
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

/* Leaps and double leaps are checked once per line shape (diatonic
 * transposition changes interval sizes, chromatic transposition does not),
 * and range for every voice that plays the note, since each voice may have
 * an instrument with a range of its own. */
static void test_voice_transpose(void) {
    PieceConfig c = test_config();
    test_set(&c, "voices=3");
    test_set(&c, "double_leaps=0");
    TestRun *r = test_open(&c);
    int leaps = count_rule(&r->model, CID_LEAP);
    int doubles = count_rule(&r->model, CID_DOUBLE_LEAP);
    test_close(r);
    CHECK(leaps == 11);
    CHECK(doubles == 10);

    test_set(&c, "transpose_1=7");
    test_set(&c, "transpose_2=12");
    r = test_open(&c);
    CHECK(count_rule(&r->model, CID_LEAP) == leaps);
    CHECK(count_rule(&r->model, CID_DOUBLE_LEAP) == doubles);
    const Constraint *range = NULL;
    for (int i = 0; i < r->model.ncons && range == NULL; i++) {
        if (r->model.cons[i].rule == CID_RANGE) range = &r->model.cons[i];
    }
    CHECK(range != NULL && range->nslots == 3);
    test_close(r);

    test_set(&c, "diatonic=1");
    test_set(&c, "transpose_1=2");
    test_set(&c, "transpose_2=4");
    r = test_open(&c);
    CHECK(count_rule(&r->model, CID_LEAP) == 3 * leaps);
    CHECK(count_rule(&r->model, CID_DOUBLE_LEAP) == 3 * doubles);
    test_close(r);

    test_set(&c, "transpose_2=9"); /* an octave above voice 2: same shape */
    r = test_open(&c);
    CHECK(count_rule(&r->model, CID_LEAP) == 2 * leaps);
    CHECK(count_rule(&r->model, CID_DOUBLE_LEAP) == 2 * doubles);
    test_close(r);
}

/* A form of two-bar phrases over eight bars of eighths: the subject, a
 * development that echoes its head, the climax's phrase that contrasts with
 * it, and the return; each phrase but the last breathes, and with harmony
 * ends on a prepared dominant. */
static void test_form(void) {
    PieceConfig c = test_config();
    test_set(&c, "grid=eighth");
    test_set(&c, "length=64");
    test_set(&c, "delay=16");
    test_set(&c, "rhythm=1");
    test_set(&c, "harmony=1");
    test_set(&c, "max_hold=5");
    test_set(&c, "range_low=55");
    test_set(&c, "range_high=79");
    test_set(&c, "phrase=2");
    test_set(&c, "w_sequence=3");
    TestRun *r = test_open(&c);
    const Model *m = &r->model;
    const FormPlan *f = &m->form;
    CHECK(f->count == 4 && f->head == 8);
    CHECK(f->start[1] == 16 && f->start[2] == 32 && f->start[3] == 48 && f->start[4] == 64);
    CHECK(f->role[0] == ROLE_SUBJECT && f->role[1] == ROLE_DEVELOP);
    CHECK(f->role[2] == ROLE_CLIMAX && f->role[3] == ROLE_RETURN);
    CHECK(f->climax == 44); /* 66% of 63 steps, on the nearest half bar */
    /* the development echoes seven intervals of the head; the return the
     * same and its first pitch; the climax's phrase does not echo */
    CHECK(count_term(m, TERM_ECHO) == 7 + 7 + 1);
    CHECK(count_term(m, TERM_SEQUENCE) == 0); /* a form replaces bar-to-bar echoes */
    CHECK(count_term(m, TERM_CONTRAST) == 1);
    CHECK(count_term(m, TERM_BREATH) == 3);
    CHECK(count_term(m, TERM_ARRIVAL) == 3 * 2 + 1);
    /* the subject's rhythm and contour return, as rules: a tie and a move
     * per echoed step, the development's upside down */
    int same = 0;
    int contours = 0;
    for (int k = 0; k < m->ncons; k++) {
        const Constraint *rule = &m->cons[k];
        if (rule->rule != CID_FORM) continue;
        if (rule->type == C_SAME) {
            CHECK(rule->vars[rule->slot[1]] == m->tie[rule->time]);
            CHECK(rule->vars[rule->slot[0]] == m->tie[rule->param]);
            same++;
        } else {
            CHECK(rule->type == C_CONTOUR);
            CHECK((rule->param & 1) == (rule->time < 32)); /* the development is inverted */
            int cv[SCOPE_MAX];
            cv[rule->slot[0]] = 60;
            cv[rule->slot[1]] = 62;
            cv[rule->slot[2]] = 67;
            cv[rule->slot[3]] = (rule->param & 1) ? 64 : 71;
            CHECK(constraint_holds(m, rule, cv));
            cv[rule->slot[3]] = 67;
            CHECK(!constraint_holds(m, rule, cv)); /* level where the subject moved */
            cv[rule->slot[3]] = PITCH_REST;
            CHECK(!constraint_holds(m, rule, cv)); /* a rest where the subject sounds */
            contours++;
        }
    }
    CHECK(same == 7 + 7 && contours == 7 + 7);
    /* no rest or hold costs where a phrase breathes, its last half bar */
    CHECK(form_breathes(m, 12) && form_breathes(m, 15) && !form_breathes(m, 11));
    CHECK(!form_breathes(m, 63));
    for (int k = 0; k < m->nterms; k++) {
        const Constraint *t = &m->terms[k];
        if (t->rule == TERM_REST || t->rule == TERM_HOLD) CHECK(!form_breathes(m, t->time));
    }

    /* the return, against no other voice's subject, echoes the head as it
     * is: the same interval and rhythm costs nothing, the same direction a
     * different size half, another rhythm or direction all */
    const Constraint *t = find_term(m, TERM_ECHO, 48 + 3, 6);
    CHECK(t != NULL && t->param == 0);
    CHECK(t->vars[t->slot[0]] == m->pitch[2] && t->vars[t->slot[3]] == m->pitch[51]);
    int v[SCOPE_MAX];
    v[t->slot[0]] = 60;
    v[t->slot[1]] = 64;
    v[t->slot[2]] = 62;
    v[t->slot[3]] = 66;
    v[t->slot[4]] = TIE_NOTE;
    v[t->slot[5]] = TIE_NOTE;
    CHECK(term_cost(m, t, v) == 0);
    v[t->slot[3]] = 65;
    CHECK(term_cost(m, t, v) == 0); /* a diatonic sequence */
    v[t->slot[3]] = 69;
    CHECK(term_cost(m, t, v) == 2);
    v[t->slot[3]] = 60;
    CHECK(term_cost(m, t, v) == 3);
    v[t->slot[3]] = 66;
    v[t->slot[5]] = TIE_HOLD;
    CHECK(term_cost(m, t, v) == 3); /* held where the subject moved */
    /* the development sounds while voice 2 enters with the subject, so it
     * echoes the head upside down, in contrary motion against it */
    t = find_term(m, TERM_ECHO, 16 + 3, 6);
    CHECK(t != NULL && t->param == 1);
    v[t->slot[0]] = 60;
    v[t->slot[1]] = 64;
    v[t->slot[2]] = 62;
    v[t->slot[3]] = 58;
    v[t->slot[4]] = TIE_NOTE;
    v[t->slot[5]] = TIE_NOTE;
    CHECK(term_cost(m, t, v) == 0);
    v[t->slot[3]] = 66;
    CHECK(term_cost(m, t, v) == 3); /* the same way up is the wrong way here */
    /* the return starts on the subject's first pitch */
    t = find_term(m, TERM_ECHO, 48, 2);
    CHECK(t != NULL);
    v[t->slot[0]] = 62;
    v[t->slot[1]] = 62;
    CHECK(term_cost(m, t, v) == 0);
    v[t->slot[1]] = 74;
    CHECK(term_cost(m, t, v) == 3);

    /* contrast: the head's attacks (its first step always one) against the
     * climax bar's; a difference under two attacks costs */
    t = find_term(m, TERM_CONTRAST, 40, 15);
    CHECK(t != NULL && t->param == 7);
    CHECK(tie_cost(m, t, "HNHNHNH" "NHNHNHNH") == 3 * 2); /* four against four */
    CHECK(tie_cost(m, t, "HNHNHNH" "NNNNNNNN") == 0);     /* four against eight */
    CHECK(tie_cost(m, t, "HNHNHNN" "NHNHNHNH") == 3);     /* five against four */

    /* a breath: each attack after the first step of the last half bar */
    t = find_term(m, TERM_BREATH, 12, 6);
    CHECK(t != NULL);
    v[t->slot[0]] = TIE_HOLD;
    v[t->slot[1]] = 60;
    v[t->slot[2]] = TIE_HOLD;
    v[t->slot[3]] = 60;
    v[t->slot[4]] = TIE_HOLD;
    v[t->slot[5]] = 60;
    CHECK(term_cost(m, t, v) == 0); /* a half note */
    v[t->slot[2]] = TIE_NOTE;
    v[t->slot[3]] = PITCH_REST;
    CHECK(term_cost(m, t, v) == 0); /* a rest breathes too */
    v[t->slot[4]] = TIE_NOTE;
    v[t->slot[5]] = 62;
    CHECK(term_cost(m, t, v) == 3); /* a note attacked in it */

    /* arrivals: a dominant at the phrase's end, prepared by IV or ii */
    t = find_term(m, TERM_ARRIVAL, 8, 1);
    CHECK(t != NULL && t->weight == 2 * c.w_harmony);
    v[t->slot[0]] = DEGREE_V;
    CHECK(term_cost(m, t, v) == 0);
    v[t->slot[0]] = DEGREE_I;
    CHECK(term_cost(m, t, v) == 2);
    t = find_term(m, TERM_ARRIVAL, 0, 1);
    CHECK(t != NULL);
    v[t->slot[0]] = DEGREE_II;
    CHECK(term_cost(m, t, v) == 0);
    test_close(r);

    /* the solver keeps the form's promises it can: here the head comes back */
    test_set(&c, "optimize=4000");
    r = test_solve(&c);
    CHECK(r->status == SOLVE_SAT);
    int breakdown[TERM_COUNT];
    model_energy(&r->model, r->values, breakdown);
    CHECK(breakdown[TERM_ECHO] < 15 * 3 / 2); /* most of the echo holds */
    test_close(r);

    /* with an arc, the climax is the top: it sounds, and nothing is above it */
    test_set(&c, "w_arc=3");
    r = test_open(&c);
    CHECK(count_rule(&r->model, CID_FORM) == 28 + 1 + 63);
    const Constraint *top = NULL;
    for (int k = 0; k < r->model.ncons && top == NULL; k++) {
        if (r->model.cons[k].type == C_NOT_ABOVE) top = &r->model.cons[k];
    }
    CHECK(top != NULL && top->param == 44);
    int pv[SCOPE_MAX];
    pv[top->slot[0]] = 70;
    pv[top->slot[1]] = 72;
    CHECK(constraint_holds(&r->model, top, pv));
    pv[top->slot[0]] = 74;
    CHECK(!constraint_holds(&r->model, top, pv));
    pv[top->slot[0]] = PITCH_REST;
    CHECK(constraint_holds(&r->model, top, pv));
    test_close(r);
    r = test_solve(&c);
    CHECK(r->status == SOLVE_SAT);
    for (int i = 0; i < c.length; i++) {
        int p = test_pitch(r, i);
        /* the single top: only within a held note's length (5) may share it */
        CHECK(p == PITCH_REST || p < test_pitch(r, 44) || (i >= 39 && i <= 49 && p == test_pitch(r, 44)));
    }
    for (int j = 1; j < 8; j++) {
        CHECK(r->values[r->model.tie[16 + j]] == r->values[r->model.tie[j]]);
        CHECK(r->values[r->model.tie[48 + j]] == r->values[r->model.tie[j]]);
    }
    test_close(r);

    /* no form: a phrase as long as the melody, or phrase 0 */
    c = test_config();
    test_set(&c, "phrase=3");
    r = test_open(&c);
    CHECK(r->model.form.count == 1 && count_term(&r->model, TERM_ECHO) == 0);
    test_close(r);
}

int main(void) {
    test_variables();
    test_voice_transpose();
    test_predicates();
    test_tension();
    test_mirror();
    test_eighth_grid();
    test_sixteenth_grid();
    test_figures();
    test_step();
    test_arc();
    test_sequence();
    test_form();
    test_energy();
    printf("ok\n");
    return 0;
}
