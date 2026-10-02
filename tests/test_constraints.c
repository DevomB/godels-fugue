#include "theory.h"
#include "test_util.h"

static void set_domain(TestRun *r, int var, const int *values, int n) {
    domain_clear(&r->state.domains[var]);
    for (int i = 0; i < n; i++) domain_add(&r->state.domains[var], values[i]);
    solver_touch(&r->state, var);
}

static bool has(const TestRun *r, int var, int value) {
    return domain_contains(&r->state.domains[var], value);
}

static int removed_by(const TestRun *r, int var, int value) {
    const ProofEvent *e = test_removal(r, var, value);
    return e == NULL ? CID_NONE : e->rule;
}

static void test_scale_and_range(void) {
    PieceConfig c = test_config();
    TestRun *r = test_open(&c);
    CHECK(solver_propagate(&r->state));
    for (int i = 0; i < c.length; i++) {
        int x = r->model.pitch[i];
        CHECK(!has(r, x, 61));
        CHECK(removed_by(r, x, 61) == CID_SCALE);
    }
    const ProofEvent *e = test_removal(r, r->model.pitch[0], 61);
    CHECK(e->parent_count == 1);
    CHECK(e->parent_vars[0] == r->model.key[0]);
    CHECK(e->parent_values[0] == key_id(0, MODE_MAJOR));
    CHECK(e->level == 0);
    CHECK(r->state.proof.sample_count >= 1);
    test_close(r);

    /* the follower sounds a fifth higher: B would sound F#, out of key */
    c.transpose = 7;
    c.range_low = 55;
    c.range_high = 84;
    r = test_open(&c);
    CHECK(solver_propagate(&r->state));
    int x0 = r->model.pitch[0];
    CHECK(!has(r, x0, 71));
    CHECK(removed_by(r, x0, 71) == CID_SCALE);
    CHECK(has(r, x0, 67));
    CHECK(!has(r, x0, 79)); /* G5 would sound D6, above the range */
    CHECK(removed_by(r, x0, 79) == CID_RANGE);
    test_close(r);
}

static void test_leap(void) {
    PieceConfig c = test_config();
    TestRun *r = test_open(&c);
    int x0 = r->model.pitch[0];
    int x1 = r->model.pitch[1];
    test_only(r, x0, 60);
    int pair[2] = {60, 72};
    set_domain(r, x1, pair, 2);
    CHECK(solver_propagate(&r->state));
    CHECK(!has(r, x1, 72));
    const ProofEvent *e = test_removal(r, x1, 72);
    CHECK(e != NULL && e->rule == CID_LEAP);
    CHECK(e->parent_vars[0] == x0 && e->parent_values[0] == 60);
    test_close(r);

    r = test_open(&c);
    test_only(r, r->model.pitch[0], 60);
    test_only(r, r->model.pitch[1], 72);
    CHECK(!solver_propagate(&r->state));
    CHECK(r->state.failed);
    CHECK(r->state.failed_variable >= 0);
    test_close(r);
}

static void test_consonance(void) {
    PieceConfig c = test_config();
    TestRun *r = test_open(&c);
    int both[2] = {62, 64};
    test_only(r, r->model.pitch[0], 60);
    set_domain(r, r->model.pitch[4], both, 2);
    CHECK(solver_propagate(&r->state));
    CHECK(!has(r, r->model.pitch[4], 62));
    CHECK(removed_by(r, r->model.pitch[4], 62) == CID_CONSONANCE);
    CHECK(has(r, r->model.pitch[4], 64));
    test_close(r);

    /* weak beats are free */
    r = test_open(&c);
    test_only(r, r->model.pitch[1], 60);
    test_only(r, r->model.pitch[5], 62);
    CHECK(solver_propagate(&r->state));
    test_close(r);

    /* unisons need allow_unison; octaves are fine */
    r = test_open(&c);
    int unison[2] = {60, 72};
    test_only(r, r->model.pitch[0], 60);
    set_domain(r, r->model.pitch[4], unison, 2);
    CHECK(solver_propagate(&r->state));
    CHECK(!has(r, r->model.pitch[4], 60));
    CHECK(has(r, r->model.pitch[4], 72));
    test_close(r);

    /* consonance=all reaches weak beats */
    test_set(&c, "consonance=all");
    r = test_open(&c);
    int weak[2] = {62, 64};
    test_only(r, r->model.pitch[1], 60);
    set_domain(r, r->model.pitch[5], weak, 2);
    CHECK(solver_propagate(&r->state));
    CHECK(!has(r, r->model.pitch[5], 62));
    test_close(r);

    /* three voices: a fourth between upper voices is fine, not over the
     * bass. With 16 notes every voice sounds at step 12 too, where x8
     * over x4 needs the lead's x12 below them. */
    c = test_config();
    test_set(&c, "voices=3");
    test_set(&c, "length=16");
    test_set(&c, "range_low=48");
    test_set(&c, "range_high=84");
    r = test_open(&c);
    test_only(r, r->model.pitch[0], 48);  /* voice 3 at step 8 */
    test_only(r, r->model.pitch[4], 67);  /* voice 2 at step 8 */
    int tops[2] = {72, 74};
    set_domain(r, r->model.pitch[8], tops, 2); /* voice 1 at step 8 */
    CHECK(solver_propagate(&r->state));
    CHECK(has(r, r->model.pitch[8], 72));  /* G-C above C: consonant */
    CHECK(!has(r, r->model.pitch[8], 74)); /* D is a second from C */
    test_close(r);

    r = test_open(&c);
    test_only(r, r->model.pitch[0], 60);
    test_only(r, r->model.pitch[4], 72);
    int over_bass[2] = {65, 64};
    set_domain(r, r->model.pitch[8], over_bass, 2);
    CHECK(solver_propagate(&r->state));
    CHECK(!has(r, r->model.pitch[8], 65)); /* F is a fourth above the bass C */
    CHECK(has(r, r->model.pitch[8], 64));
    test_close(r);
}

static void test_parallels(void) {
    PieceConfig c = test_config();
    TestRun *r = test_open(&c);
    int options[2] = {64, 60};
    test_only(r, r->model.pitch[4], 60);
    test_only(r, r->model.pitch[0], 67);
    test_only(r, r->model.pitch[1], 71);
    set_domain(r, r->model.pitch[5], options, 2);
    CHECK(solver_propagate(&r->state));
    CHECK(!has(r, r->model.pitch[5], 64));
    CHECK(removed_by(r, r->model.pitch[5], 64) == CID_PARALLEL_FIFTH);
    CHECK(has(r, r->model.pitch[5], 60));
    test_close(r);

    /* contrary motion between fifths is allowed */
    r = test_open(&c);
    test_only(r, r->model.pitch[4], 60);
    test_only(r, r->model.pitch[5], 67);
    test_only(r, r->model.pitch[0], 67);
    test_only(r, r->model.pitch[1], 60);
    CHECK(solver_propagate(&r->state));
    test_close(r);

    /* C5 over C4 moving to D5 over D4 */
    test_set(&c, "range_high=76");
    r = test_open(&c);
    int octave_options[2] = {74, 71};
    test_only(r, r->model.pitch[4], 72);
    test_only(r, r->model.pitch[0], 60);
    test_only(r, r->model.pitch[1], 62);
    set_domain(r, r->model.pitch[5], octave_options, 2);
    CHECK(solver_propagate(&r->state));
    CHECK(removed_by(r, r->model.pitch[5], 74) == CID_PARALLEL_OCTAVE);
    CHECK(has(r, r->model.pitch[5], 71));
    test_close(r);

    /* retrograde: the follower at steps 4-5 plays x11 then x10, so the
     * last note is G here and the cadence rule has to be off */
    c = test_config();
    test_set(&c, "cadence=0");
    test_set(&c, "retrograde=1");
    r = test_open(&c);
    test_only(r, r->model.pitch[4], 60);
    test_only(r, r->model.pitch[11], 67);
    test_only(r, r->model.pitch[10], 71);
    set_domain(r, r->model.pitch[5], options, 2);
    CHECK(solver_propagate(&r->state));
    CHECK(removed_by(r, r->model.pitch[5], 64) == CID_PARALLEL_FIFTH);
    test_close(r);

    /* parallels=0 drops the rule */
    c = test_config();
    test_set(&c, "parallels=0");
    r = test_open(&c);
    test_only(r, r->model.pitch[4], 60);
    test_only(r, r->model.pitch[0], 67);
    test_only(r, r->model.pitch[1], 71);
    set_domain(r, r->model.pitch[5], options, 2);
    CHECK(solver_propagate(&r->state));
    CHECK(has(r, r->model.pitch[5], 64));
    test_close(r);
}

static void test_cadence(void) {
    PieceConfig c = test_config();
    TestRun *r = test_open(&c);
    CHECK(solver_propagate(&r->state));
    int last = r->model.pitch[11];
    int before = r->model.pitch[10];
    CHECK(domain_count(&r->state.domains[last]) == 2); /* C4 and C5 */
    CHECK(has(r, last, 60) && has(r, last, 72));
    CHECK(removed_by(r, last, 64) == CID_CADENCE);
    CHECK(has(r, before, 62) && has(r, before, 67) && has(r, before, 71));
    CHECK(!has(r, before, 69));
    test_close(r);

    /* a tied final note moves the approach back one step */
    test_set(&c, "rhythm=1");
    r = test_open(&c);
    test_only(r, r->model.tie[11], TIE_HOLD);
    CHECK(solver_propagate(&r->state));
    CHECK(!has(r, r->model.pitch[10], 67)); /* now part of the final tonic */
    CHECK(!has(r, r->model.pitch[9], 69));  /* the new approach note */
    CHECK(has(r, r->model.pitch[9], 67));
    test_close(r);

    c = test_config();
    test_set(&c, "range_high=60");
    test_set(&c, "range_low=60");
    r = test_open(&c);
    CHECK(!solver_propagate(&r->state));
    test_close(r);
}

static void test_harmony(void) {
    /* strong-beat notes are tones of their bar's chord */
    PieceConfig c = test_config();
    test_set(&c, "harmony=1");
    test_set(&c, "cadence=0");
    test_set(&c, "length=20");
    TestRun *r = test_open(&c);
    test_only(r, r->model.chord[0], DEGREE_I);
    CHECK(solver_propagate(&r->state));
    int x0 = r->model.pitch[0];
    int key = key_id(0, MODE_MAJOR);
    CHECK(domain_count(&r->state.domains[x0]) >= 2);
    for (int p = 0; p < 128; p++) {
        if (has(r, x0, p)) CHECK(key_triad_has(key, DEGREE_I, p));
    }
    CHECK(removed_by(r, x0, 62) == CID_CHORD);
    test_close(r);

    /* V may go to I, V or vi. (The canon couples bars too: x4 is a tone
     * of V in bar 2 and sounds again in bar 3, which rules out vi.) */
    r = test_open(&c);
    test_only(r, r->model.chord[1], DEGREE_V);
    CHECK(solver_propagate(&r->state));
    int c2 = r->model.chord[2];
    CHECK(domain_count(&r->state.domains[c2]) >= 1);
    for (int d = 0; d < DEGREE_COUNT; d++) {
        if (has(r, c2, d)) CHECK(progression_allowed(DEGREE_V, d));
    }
    CHECK(removed_by(r, c2, DEGREE_II) == CID_PROGRESSION);
    CHECK(removed_by(r, c2, DEGREE_VI) == CID_CHORD);
    test_close(r);

    /* the cadence puts I in the last bar and V or vii before it */
    test_set(&c, "cadence=1");
    r = test_open(&c);
    CHECK(solver_propagate(&r->state));
    int nb = r->model.nbars;
    CHECK(domain_value(&r->state.domains[r->model.chord[nb - 1]]) == DEGREE_I);
    const MidiDomain *pen = &r->state.domains[r->model.chord[nb - 2]];
    for (int d = 0; d < DEGREE_COUNT; d++) {
        if (domain_contains(pen, d)) CHECK(d == DEGREE_V || d == DEGREE_VII);
    }
    test_close(r);
}

static void test_rhythm(void) {
    PieceConfig c = test_config();
    test_set(&c, "rhythm=1");
    TestRun *r = test_open(&c);
    int x4 = r->model.pitch[4];
    int x5 = r->model.pitch[5];
    int four[2] = {64, 67};
    set_domain(r, x4, four, 2);
    test_only(r, r->model.tie[5], TIE_HOLD);
    CHECK(solver_propagate(&r->state));
    CHECK(!has(r, x5, PITCH_REST));
    CHECK(!has(r, x5, 60));
    CHECK(removed_by(r, x5, 60) == CID_TIE);
    CHECK(!has(r, r->model.tie[6], TIE_HOLD)); /* max_hold 1 */
    CHECK(removed_by(r, r->model.tie[6], TIE_HOLD) == CID_HOLD);
    test_close(r);

    test_set(&c, "max_rests=1");
    r = test_open(&c);
    test_only(r, r->model.pitch[3], PITCH_REST);
    CHECK(solver_propagate(&r->state));
    for (int i = 0; i < c.length; i++) {
        if (i == 3) continue;
        CHECK(!has(r, r->model.pitch[i], PITCH_REST));
    }
    CHECK(removed_by(r, r->model.pitch[5], PITCH_REST) == CID_REST);
    test_close(r);

    r = test_open(&c);
    test_only(r, r->model.pitch[3], PITCH_REST);
    test_only(r, r->model.pitch[5], PITCH_REST);
    CHECK(!solver_propagate(&r->state));
    test_close(r);

    test_set(&c, "rest_at=3");
    r = test_open(&c);
    CHECK(solver_propagate(&r->state));
    CHECK(domain_value(&r->state.domains[r->model.pitch[3]]) == PITCH_REST);
    const ProofEvent *last = &r->state.proof.events[r->state.proof.event_count - 1];
    bool forced = false;
    for (int i = 0; i < r->state.proof.event_count; i++) {
        const ProofEvent *e = &r->state.proof.events[i];
        if (e->type == PROOF_FORCED && e->variable_id == r->model.pitch[3]) forced = true;
    }
    CHECK(forced);
    (void)last;
    test_close(r);
}

static void test_keys(void) {
    /* the key changes at step 4 for every voice at once */
    PieceConfig c = test_config();
    test_set(&c, "modulate_at=4");
    test_set(&c, "key_second=G");
    TestRun *r = test_open(&c);
    CHECK(solver_propagate(&r->state));
    int x0 = r->model.pitch[0]; /* lead at 0 in C, follower at 4 in G */
    int x6 = r->model.pitch[6]; /* lead at 6 and follower at 10, both in G */
    CHECK(!has(r, x0, 65) && !has(r, x0, 66));
    CHECK(has(r, x6, 66) && !has(r, x6, 65));
    CHECK(domain_value(&r->state.domains[r->model.key[1]]) == key_id(7, MODE_MAJOR));
    test_close(r);

    test_set(&c, "key_second=related");
    r = test_open(&c);
    CHECK(solver_propagate(&r->state));
    const MidiDomain *k2 = &r->state.domains[r->model.key[1]];
    CHECK(domain_count(k2) == 5);
    CHECK(domain_contains(k2, key_id(7, MODE_MAJOR)));
    CHECK(domain_contains(k2, key_id(5, MODE_MAJOR)));
    CHECK(domain_contains(k2, key_id(9, MODE_MINOR)));
    CHECK(domain_contains(k2, key_id(4, MODE_MINOR)));
    CHECK(domain_contains(k2, key_id(2, MODE_MINOR)));
    test_close(r);

    /* a searched key keeps only keys holding a locked F# */
    c = test_config();
    test_set(&c, "key=search");
    test_set(&c, "mode=search");
    test_set(&c, "lock=1");
    test_set(&c, "lock_index=0");
    test_set(&c, "lock_pitch=66");
    r = test_open(&c);
    CHECK(solver_propagate(&r->state));
    const MidiDomain *k = &r->state.domains[r->model.key[0]];
    CHECK(!domain_contains(k, key_id(0, MODE_MAJOR)));
    CHECK(domain_contains(k, key_id(7, MODE_MAJOR)));
    CHECK(domain_contains(k, key_id(4, MODE_MINOR)));
    CHECK(removed_by(r, r->model.key[0], key_id(0, MODE_MAJOR)) == CID_SCALE);
    test_close(r);
}

/* A follower transformed to pitch -1 is out of range, not a rest. */
static void test_negative_followers(void) {
    PieceConfig c = test_config();
    test_set(&c, "invert=1");
    test_set(&c, "axis=30");
    test_set(&c, "key=C#");
    test_set(&c, "length=4");
    test_set(&c, "cadence=0");
    TestRun *r = test_open(&c);
    CHECK(!solver_propagate(&r->state));
    test_close(r);

    c = test_config();
    test_set(&c, "transpose=-24");
    test_set(&c, "range_low=1");
    test_set(&c, "range_high=30");
    test_set(&c, "key=B");
    test_set(&c, "length=4");
    test_set(&c, "cadence=0");
    r = test_open(&c);
    CHECK(solver_propagate(&r->state));
    int x0 = r->model.pitch[0];
    CHECK(!has(r, x0, 23));
    CHECK(removed_by(r, x0, 23) == CID_RANGE);
    for (int p = 1; p < 128; p++) {
        if (has(r, x0, p)) CHECK(p - 24 >= 1);
    }
    test_close(r);
}

static void test_lock(void) {
    PieceConfig c = test_config();
    test_set(&c, "lock=1");
    test_set(&c, "lock_pitch=61");
    TestRun *r = test_open(&c);
    CHECK(!solver_propagate(&r->state));
    CHECK(r->state.failed_variable == r->model.pitch[0]);
    test_close(r);

    /* given melody notes lock the same way, and a free note stays free */
    c = test_config();
    test_set(&c, "melody=?,67,?,64");
    r = test_open(&c);
    CHECK(solver_propagate(&r->state));
    CHECK(solver_value(&r->state, r->model.pitch[1]) == 67);
    CHECK(solver_value(&r->state, r->model.pitch[3]) == 64);
    CHECK(removed_by(r, r->model.pitch[1], 65) == CID_LOCK);
    CHECK(domain_count(&r->state.domains[r->model.pitch[2]]) > 1);
    test_close(r);
}

static void test_spacing_and_crossing(void) {
    /* at step 4 the follower's x0 sounds under the lead's x4 */
    PieceConfig c = test_config();
    test_set(&c, "range_high=84");
    test_set(&c, "max_spacing=7");
    TestRun *r = test_open(&c);
    int x0 = r->model.pitch[0];
    int x4 = r->model.pitch[4];
    int wide[3] = {64, 67, 72};
    test_only(r, x0, 60);
    set_domain(r, x4, wide, 3);
    CHECK(solver_propagate(&r->state));
    CHECK(has(r, x4, 64) && has(r, x4, 67));
    CHECK(!has(r, x4, 72));
    const ProofEvent *e = test_removal(r, x4, 72);
    CHECK(e != NULL && e->rule == CID_SPACING);
    CHECK(e->parent_vars[0] == x0 && e->parent_values[0] == 60);
    char why[128];
    constraint_describe(&r->model, &r->model.cons[e->constraint], why, sizeof(why));
    CHECK(strstr(why, "within 7 semitones at step 4") != NULL);
    test_close(r);

    /* without crossing, voice 2 may not sound above voice 1; a unison is fine */
    c = test_config();
    test_set(&c, "crossing=0");
    test_set(&c, "allow_unison=1");
    r = test_open(&c);
    x0 = r->model.pitch[0];
    x4 = r->model.pitch[4];
    int around[4] = {60, 64, 67, 72};
    test_only(r, x0, 64);
    set_domain(r, x4, around, 4);
    CHECK(solver_propagate(&r->state));
    CHECK(!has(r, x4, 60));
    CHECK(removed_by(r, x4, 60) == CID_CROSSING);
    CHECK(has(r, x4, 64) && has(r, x4, 67) && has(r, x4, 72));
    test_close(r);

    /* the rules also hold on weak beats, where consonance does not look */
    c = test_config();
    test_set(&c, "max_spacing=4");
    r = test_open(&c);
    test_only(r, r->model.pitch[1], 60);
    test_only(r, r->model.pitch[5], 67);
    CHECK(!solver_propagate(&r->state));
    test_close(r);
}

static bool parent_is(const ProofEvent *e, int var, int value) {
    for (int k = 0; k < e->parent_count; k++) {
        if (e->parent_vars[k] == var && e->parent_values[k] == value) return true;
    }
    return false;
}

static void test_counterpoint(void) {
    /* B in C major must rise to C */
    PieceConfig c = test_config();
    test_set(&c, "leading_tone=1");
    TestRun *r = test_open(&c);
    int x0 = r->model.pitch[0];
    int x1 = r->model.pitch[1];
    int next[3] = {67, 72, 69};
    test_only(r, x0, 71);
    set_domain(r, x1, next, 3);
    CHECK(solver_propagate(&r->state));
    CHECK(has(r, x1, 72) && !has(r, x1, 67) && !has(r, x1, 69));
    const ProofEvent *e = test_removal(r, x1, 67);
    CHECK(e != NULL && e->rule == CID_LEADING_TONE);
    CHECK(parent_is(e, x0, 71));
    CHECK(parent_is(e, r->model.key[0], key_id(0, MODE_MAJOR)));
    char why[128];
    constraint_describe(&r->model, &r->model.cons[e->constraint], why, sizeof(why));
    CHECK(strstr(why, "melody note 0 rises to the tonic at note 1") != NULL);
    test_close(r);

    /* the other way round: a note other than C after x0 rules out B */
    r = test_open(&c);
    test_only(r, r->model.pitch[1], 69);
    CHECK(solver_propagate(&r->state));
    CHECK(!has(r, r->model.pitch[0], 71));
    CHECK(removed_by(r, r->model.pitch[0], 71) == CID_LEADING_TONE);
    test_close(r);

    /* a held leading tone passes the duty on; a rest does not resolve it */
    test_set(&c, "rhythm=1");
    r = test_open(&c);
    int x2 = r->model.pitch[2];
    int after[3] = {PITCH_REST, 67, 72};
    test_only(r, r->model.pitch[0], 71);
    test_only(r, r->model.tie[1], TIE_HOLD);
    set_domain(r, x2, after, 3);
    CHECK(solver_propagate(&r->state));
    CHECK(has(r, r->model.pitch[1], 71));
    CHECK(has(r, x2, 72) && !has(r, x2, 67) && !has(r, x2, PITCH_REST));
    CHECK(removed_by(r, x2, PITCH_REST) == CID_LEADING_TONE);
    test_close(r);

    /* in A minor the raised seventh G# leads to A; G does not */
    c = test_config();
    test_set(&c, "leading_tone=1");
    test_set(&c, "key=A");
    test_set(&c, "mode=minor");
    test_set(&c, "range_low=57");
    test_set(&c, "range_high=76");
    r = test_open(&c);
    int free_next[2] = {64, 69};
    test_only(r, r->model.pitch[0], 68);
    set_domain(r, r->model.pitch[1], free_next, 2);
    CHECK(solver_propagate(&r->state));
    CHECK(!has(r, r->model.pitch[1], 64) && has(r, r->model.pitch[1], 69));
    test_close(r);
    r = test_open(&c);
    test_only(r, r->model.pitch[0], 67);
    set_domain(r, r->model.pitch[1], free_next, 2);
    CHECK(solver_propagate(&r->state));
    CHECK(has(r, r->model.pitch[1], 64));
    test_close(r);

    /* two leaps over a major third in one direction */
    c = test_config();
    test_set(&c, "double_leaps=0");
    test_set(&c, "max_leap=12");
    test_set(&c, "range_high=84");
    r = test_open(&c);
    x0 = r->model.pitch[0];
    x1 = r->model.pitch[1];
    x2 = r->model.pitch[2];
    int third[4] = {64, 71, 72, 74};
    test_only(r, x0, 60);
    test_only(r, x1, 67);
    set_domain(r, x2, third, 4);
    CHECK(solver_propagate(&r->state));
    CHECK(has(r, x2, 64) && has(r, x2, 71)); /* back down, or a third up */
    CHECK(!has(r, x2, 72) && !has(r, x2, 74));
    e = test_removal(r, x2, 72);
    CHECK(e != NULL && e->rule == CID_DOUBLE_LEAP);
    CHECK(parent_is(e, x0, 60) && parent_is(e, x1, 67));
    test_close(r);
}

int main(void) {
    test_scale_and_range();
    test_leap();
    test_consonance();
    test_parallels();
    test_cadence();
    test_harmony();
    test_rhythm();
    test_keys();
    test_negative_followers();
    test_lock();
    test_spacing_and_crossing();
    test_counterpoint();
    printf("ok\n");
    return 0;
}
