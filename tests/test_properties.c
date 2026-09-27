/* Random configs, solved and then checked by a rule checker written
 * straight from the rule definitions. It reads only the rendered voice
 * lines, the melody and the config, so a mistake in how the model
 * builds its constraints (wrong step, voice, key or transform) shows
 * up here rather than being checked against itself. */
#include "canon.h"
#include "sat.h"
#include "score.h"
#include "theory.h"
#include "test_util.h"

static unsigned rng_state = 12345u;

static int pick(int lo, int hi) {
    rng_state = rng_state * 1103515245u + 12345u;
    return lo + (int)((rng_state >> 8) % (unsigned)(hi - lo + 1));
}

static PieceConfig random_config(void) {
    PieceConfig c = test_config();
    c.max_nodes = 3000;
    c.length = pick(6, 16);
    c.voices = pick(2, 4);
    c.delay = pick(1, 6);
    c.range_low = pick(48, 62);
    c.range_high = c.range_low + pick(12, 24);
    c.max_leap = pick(4, 12);
    c.key = pick(0, 11);
    c.mode = pick(0, 3) == 0 ? MODE_MINOR : MODE_MAJOR;
    switch (pick(0, 7)) {
    case 0:
        c.retrograde = 1;
        break;
    case 1:
        c.transpose = pick(0, 1) ? 7 : 12;
        break;
    case 2:
        c.invert = 1;
        c.axis = c.range_low + (c.range_high - c.range_low) / 2;
        break;
    case 3:
        c.augment = 2;
        break;
    case 4:
        c.diminish = 2;
        break;
    case 5:
        c.cyclic = 1;
        break;
    case 6:
        c.phase = 1;
        break;
    default:
        break;
    }
    if (c.voices > 2 && pick(0, 1)) c.voice_delay[2] = pick(1, 9);
    c.consonance = pick(0, 4) == 0 ? CONSONANCE_ALL : CONSONANCE_STRONG;
    c.allow_fourth = c.voices > 2 && pick(0, 1);
    c.allow_unison = pick(0, 3) == 0;
    c.cadence = pick(0, 2) != 0;
    c.harmony = pick(0, 2) == 0;
    c.rhythm = pick(0, 1);
    c.max_hold = pick(1, 2);
    c.poly_meter = pick(0, 4) == 0;
    c.var_order = pick(0, 3);
    c.learn = pick(0, 1);
    c.backjump = pick(0, 3) != 0;
    if (pick(0, 3) == 0) {
        c.modulate_at = pick(1, c.length);
        c.key_second = pick(0, 1) ? KEY_SEARCH : (c.key + 7) % 12;
    }
    if (pick(0, 5) == 0) c.key = KEY_SEARCH;
    return c;
}

static int key_for(const Model *m, const int *values, int t) {
    return values[m->key[model_section_at(m, t)]];
}

static void fail(const PieceConfig *c, const char *rule, int t) {
    fprintf(stderr, "rule %s broken at step %d\n", rule, t);
    config_write(stderr, c);
    exit(1);
}

/* Checks the finished piece against the rules as written. */
static void check_piece(const Model *m, const int *values) {
    const PieceConfig *c = &m->config;
    Score score;
    score_build(&score, m, values);
    int voices = m->voices;

    for (int v = 0; v < voices; v++) {
        for (int t = 0; t < m->span; t++) {
            int i = canon_map_source(c, v, t);
            int p = score.line[v][t];
            int melody = i < 0 ? PITCH_REST : values[m->pitch[i]];
            /* the voice plays the transformed melody */
            if (i < 0 || melody == PITCH_REST) {
                if (p != SOUND_REST) fail(c, "canon silence", t);
                continue;
            }
            int expect = melody;
            if (v > 0 && c->invert)
                expect = c->invert_mod12 ? invert_pitch_mod12(c->axis, expect)
                                         : invert_pitch(c->axis, expect);
            if (v > 0) expect += c->transpose;
            if (p != expect) fail(c, "canon copy", t);
            if (p < c->range_low || p > c->range_high) fail(c, "range", t);
            if (!key_has_pitch(key_for(m, values, t), p)) fail(c, "scale", t);
        }
        /* leaps between consecutive sounding steps of one voice */
        for (int t = 0; t + 1 < m->span; t++) {
            int a = score.line[v][t];
            int b = score.line[v][t + 1];
            if (a == SOUND_REST || b == SOUND_REST) continue;
            if (abs(a - b) > c->max_leap) fail(c, "leap", t);
        }
    }

    for (int t = 0; t < m->span; t++) {
        int sounding[VOICE_MAX];
        int n = 0;
        for (int v = 0; v < voices; v++) {
            if (score.line[v][t] != SOUND_REST) sounding[n++] = score.line[v][t];
        }
        bool strong = is_strong_time(t, c->poly_meter);
        if (c->consonance == CONSONANCE_ALL || (c->consonance == CONSONANCE_STRONG && strong)) {
            int low = 999;
            for (int k = 0; k < n; k++) low = sounding[k] < low ? sounding[k] : low;
            for (int a = 0; a < n; a++) {
                for (int b = a + 1; b < n; b++) {
                    int d = abs(sounding[a] - sounding[b]);
                    int ic = d % 12;
                    if (d == 0 && !c->allow_unison) fail(c, "unison", t);
                    if (d == 0) continue;
                    bool ok = ic == 0 || ic == 3 || ic == 4 || ic == 7 || ic == 8 || ic == 9;
                    if (ic == 5)
                        ok = c->allow_fourth || (sounding[a] != low && sounding[b] != low);
                    if (!ok) fail(c, "consonance", t);
                }
            }
        }
        if (c->harmony && strong) {
            int chord = values[m->chord[t / 4]];
            int key = key_for(m, values, t);
            for (int k = 0; k < n; k++) {
                if (!key_triad_has(key, chord, sounding[k])) fail(c, "chord tone", t);
            }
        }
        if (t + 1 >= m->span || !c->parallels) continue;
        for (int a = 0; a < voices; a++) {
            for (int b = a + 1; b < voices; b++) {
                int p0 = score.line[a][t], p1 = score.line[a][t + 1];
                int q0 = score.line[b][t], q1 = score.line[b][t + 1];
                if (p0 == SOUND_REST || p1 == SOUND_REST || q0 == SOUND_REST || q1 == SOUND_REST)
                    continue;
                int d0 = abs(p0 - q0) % 12;
                int d1 = abs(p1 - q1) % 12;
                bool similar = (p1 - p0) * (q1 - q0) > 0;
                if (similar && d0 == 7 && d1 == 7) fail(c, "parallel fifth", t);
                if (similar && d0 == 0 && d1 == 0) fail(c, "parallel octave", t);
            }
        }
    }

    if (c->harmony && c->progression) {
        for (int b = 0; b + 1 < m->nbars; b++) {
            if (model_section_at(m, b * 4) != model_section_at(m, b * 4 + 4)) continue;
            if (!progression_allowed(values[m->chord[b]], values[m->chord[b + 1]]))
                fail(c, "progression", b * 4);
        }
    }

    /* rhythm: ties repeat a sounding pitch; no tie longer than max_hold */
    int rests = 0;
    int run = 0;
    for (int i = 0; i < c->length; i++) {
        int p = values[m->pitch[i]];
        rests += p == PITCH_REST;
        bool hold = m->tie[i] >= 0 && values[m->tie[i]] == TIE_HOLD;
        if (hold && (p == PITCH_REST || values[m->pitch[i - 1]] != p)) fail(c, "tie", i);
        run = hold ? run + 1 : 0;
        if (run > c->max_hold) fail(c, "max hold", i);
    }
    if (c->rhythm && rests > c->max_rests) fail(c, "max rests", 0);

    if (c->cadence) {
        int last = c->length - 1;
        int key = key_for(m, values, last);
        int final = values[m->pitch[last]];
        if (final == PITCH_REST || pitch_class(final) != key_tonic(key)) fail(c, "cadence final", last);
        int onset = last;
        while (onset > 0 && m->tie[onset] >= 0 && values[m->tie[onset]] == TIE_HOLD) onset--;
        if (onset > 0) {
            int approach = values[m->pitch[onset - 1]];
            if (approach == PITCH_REST || !key_triad_has(key, DEGREE_V, approach))
                fail(c, "cadence approach", onset - 1);
        }
        for (int v = 1; v < voices; v++) {
            int end = -1;
            for (int t = 0; t < m->span; t++) {
                if (canon_map_source(c, v, t) >= 0) end = t;
            }
            if (end < 0) continue;
            int p = score.line[v][end];
            if (p == SOUND_REST || !key_triad_has(key_for(m, values, end), DEGREE_I, p))
                fail(c, "cadence follower", end);
        }
    }
}

int main(void) {
    int solved = 0;
    int unsat = 0;
    int limited = 0;
    int sat_checked = 0;
    for (int trial = 0; trial < 300; trial++) {
        PieceConfig c = random_config();
        char err[200];
        if (!config_validate(&c, err, sizeof(err))) continue;
        TestRun *r = test_solve(&c);
        if (r->status == SOLVE_SAT) {
            solved++;
            check_piece(&r->model, r->values);
            /* the proof log's last word on every variable is its value */
            for (int v = 0; v < r->model.nvars; v++) CHECK(r->values[v] >= 0);
            CHECK(solver_entropy(&r->state) == 0.0);
        } else if (r->status == SOLVE_UNSAT) {
            unsat++;
        } else {
            limited++;
        }
        /* an independent search agrees on whether a solution exists */
        if (r->status != SOLVE_LIMIT && c.length <= 10) {
            int values[VAR_MAX];
            int rc = sat_solve(&r->model, values, 200000);
            if (rc == SAT_SAT || rc == SAT_UNSAT) {
                sat_checked++;
                if ((rc == SAT_SAT) != (r->status == SOLVE_SAT)) {
                    fprintf(stderr, "sat says %d, search says %d for:\n", rc, r->status);
                    config_write(stderr, &c);
                }
                CHECK((rc == SAT_SAT) == (r->status == SOLVE_SAT));
                if (rc == SAT_SAT) check_piece(&r->model, values);
            }
        }
        test_close(r);
    }
    printf("solved %d, unsat %d, limit %d, sat-checked %d\n", solved, unsat, limited,
           sat_checked);
    CHECK(solved >= 100);
    CHECK(sat_checked >= 50);
    printf("ok\n");
    return 0;
}
