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
        /* no draws here, so the configs after this one stay the same */
        if (c.length % 3 == 1) {
            c.voice_transpose[1] = c.delay % 2 ? 7 : 0;
            c.voice_transpose[2] = 12;
        } else if (c.length % 3 == 2) {
            c.diatonic = 1;
            c.transpose = c.delay % 2 ? 2 : -3;
            c.voice_transpose[2] = c.max_leap % 2 ? 4 : TRANSPOSE_SAME;
            if (c.range_low % 3 == 0) {
                c.invert = 1;
                c.axis = c.range_low + (c.range_high - c.range_low) / 2;
            }
        }
        break;
    }
    if (c.voices > 2 && pick(0, 1)) c.voice_delay[2] = pick(1, 9);
    c.consonance = pick(0, 4) == 0 ? CONSONANCE_ALL : CONSONANCE_STRONG;
    c.allow_fourth = c.voices > 2 && pick(0, 1);
    c.allow_unison = pick(0, 3) == 0;
    c.cadence = pick(0, 2) != 0;
    if (pick(0, 3) == 0) c.max_spacing = pick(7, 19);
    c.crossing = pick(0, 3) != 0;
    if (pick(0, 4) == 0) {
        for (int k = 0; k < 3; k++) {
            int i = pick(0, c.length - 1);
            c.melody[i] = pick(c.range_low, c.range_high);
        }
    }
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
    c.leading_tone = pick(0, 2) == 0;
    c.double_leaps = pick(0, 2) != 0;
    if (c.diatonic) {
        /* diatonic needs one fixed key */
        c.modulate_at = -1;
        c.key_second = -1;
        if (c.key == KEY_SEARCH) c.key = 0;
    }
    if (pick(0, 5) == 0) {
        /* half the time an axis that maps the key onto itself, if one is in range */
        c.mirror = 1;
        c.mirror_axis = pick(c.range_low, c.range_high);
        if (c.key != KEY_SEARCH && pick(0, 1)) {
            int keep = inversion_nearest_axis(key_id(c.key, c.mode), c.mirror_axis, 0);
            if (keep >= c.range_low && keep <= c.range_high) c.mirror_axis = keep;
        }
    }
    if (pick(0, 3) == 0) {
        for (int k = pick(1, 6) - 1; k >= 0; k--) c.tension[k] = pick(-1, 4);
    }
    return c;
}

static int follower_shift(const PieceConfig *c, int v) {
    int t = c->voice_transpose[v];
    return t == TRANSPOSE_SAME ? c->transpose : t;
}

/* Walks `steps` notes along the key's seven-note scale from the scale
 * note at or below p, then restores p's distance above that note. */
static int scale_walk(const PieceConfig *c, int p, int steps) {
    static const int major[7] = {0, 2, 4, 5, 7, 9, 11};
    static const int minor[7] = {0, 2, 3, 5, 7, 8, 10};
    const int *scale = c->mode == MODE_MINOR ? minor : major;
    int mask = 0;
    for (int d = 0; d < 7; d++) mask |= 1 << ((c->key + scale[d]) % 12);
    int base = p;
    while (!((mask >> (((base % 12) + 12) % 12)) & 1)) base--;
    int q = base;
    for (int n = steps; n != 0;) {
        q += n > 0 ? 1 : -1;
        if ((mask >> (((q % 12) + 12) % 12)) & 1) n += n > 0 ? -1 : 1;
    }
    return q + (p - base);
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
            if (v > 0 && c->diatonic) {
                expect = scale_walk(c, expect, follower_shift(c, v));
            } else if (v > 0) {
                expect += follower_shift(c, v);
            }
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
        /* two leaps over 4 semitones in one direction between three
         * consecutive notes; a step that repeats its melody note (as in
         * augmentation) is the same note, a silence breaks the line */
        int prev[2] = {SOUND_REST, SOUND_REST};
        int last = -1;
        for (int t = 0; t < m->span && !c->double_leaps; t++) {
            int i = canon_map_source(c, v, t);
            int p = score.line[v][t];
            if (i < 0 || p == SOUND_REST) {
                prev[0] = prev[1] = SOUND_REST;
                last = -1;
                continue;
            }
            if (i == last) continue;
            last = i;
            if (prev[0] != SOUND_REST && prev[1] != SOUND_REST) {
                int first = prev[1] - prev[0];
                int second = p - prev[1];
                if (abs(first) > 4 && abs(second) > 4 && (first > 0) == (second > 0))
                    fail(c, "double leap", t);
            }
            prev[0] = prev[1];
            prev[1] = p;
        }
    }

    /* a leading tone in the melody rises to the tonic at the next attack */
    for (int i = 0; c->leading_tone && i + 1 < c->length; i++) {
        int p = values[m->pitch[i]];
        if (p == PITCH_REST || pitch_class(p + 1) != key_tonic(key_for(m, values, i))) continue;
        if (m->tie[i + 1] >= 0 && values[m->tie[i + 1]] == TIE_HOLD) continue;
        if (values[m->pitch[i + 1]] != p + 1) fail(c, "leading tone", i);
    }

    /* a step is a quarter-note beat, or half of one on the eighth grid; the
     * first beat of a 4/4 bar is strong, and with poly_meter every third */
    int beat = c->grid == GRID_EIGHTH ? 2 : 1;
    int bar = 4 * beat;
    for (int t = 0; t < m->span; t++) {
        int sounding[VOICE_MAX];
        int n = 0;
        for (int v = 0; v < voices; v++) {
            if (score.line[v][t] != SOUND_REST) sounding[n++] = score.line[v][t];
        }
        int beats = t / beat;
        bool strong = t % beat == 0 && (beats % 4 == 0 || (c->poly_meter && beats % 3 == 0));
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
        if (c->max_spacing > 0) {
            for (int a = 0; a < n; a++) {
                for (int b = a + 1; b < n; b++) {
                    if (abs(sounding[a] - sounding[b]) > c->max_spacing) fail(c, "spacing", t);
                }
            }
        }
        if (!c->crossing) {
            /* sounding[] lists the voices in order, so each stays at or below the last */
            for (int k = 1; k < n; k++) {
                if (sounding[k] > sounding[k - 1]) fail(c, "crossing", t);
            }
        }
        if (c->harmony && strong) {
            int chord = values[m->chord[t / bar]];
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
        if (m->nbars != (m->span + bar - 1) / bar) fail(c, "bar count", 0);
        for (int b = 0; b + 1 < m->nbars; b++) {
            if (model_section_at(m, b * bar) != model_section_at(m, b * bar + bar)) continue;
            if (!progression_allowed(values[m->chord[b]], values[m->chord[b + 1]]))
                fail(c, "progression", b * bar);
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
    for (int i = 0; i < c->length; i++) {
        if (c->melody[i] >= 0 && values[m->pitch[i]] != c->melody[i]) fail(c, "given note", i);
    }
    /* mirror: read backwards and upside down, the melody is itself */
    for (int i = 0; c->mirror && i < c->length; i++) {
        int a = values[m->pitch[i]];
        int b = values[m->pitch[c->length - 1 - i]];
        bool rest = a == PITCH_REST || b == PITCH_REST;
        if (rest ? a != b || 2 * i + 1 == c->length : a + b != 2 * c->mirror_axis)
            fail(c, "mirror", i);
    }

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
    int mirrored = 0;
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
            mirrored += c.mirror;
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
    printf("solved %d (%d mirrored), unsat %d, limit %d, sat-checked %d\n", solved, mirrored,
           unsat, limited, sat_checked);
    CHECK(solved >= 100);
    CHECK(mirrored >= 3);
    CHECK(sat_checked >= 50);

    /* diatonic canons with each follower at its own scale step */
    int diatonic_solved = 0;
    for (int trial = 0; trial < 60; trial++) {
        PieceConfig c = random_config();
        c.diatonic = 1;
        c.modulate_at = -1;
        c.key_second = -1;
        if (c.key == KEY_SEARCH) c.key = pick(0, 11);
        c.augment = 0;
        c.diminish = 0;
        c.transpose = pick(-4, 4);
        for (int v = 1; v < VOICE_MAX; v++)
            c.voice_transpose[v] = pick(0, 2) == 0 ? TRANSPOSE_SAME : pick(-7, 9);
        char err[200];
        if (!config_validate(&c, err, sizeof(err))) continue;
        TestRun *r = test_solve(&c);
        if (r->status == SOLVE_SAT) {
            diatonic_solved++;
            check_piece(&r->model, r->values);
        }
        test_close(r);
    }
    printf("diatonic solved %d\n", diatonic_solved);
    CHECK(diatonic_solved >= 10);

    /* the eighth grid: two steps to a beat and eight to a bar, longer ties,
     * and entries a beat or more apart */
    int eighth_solved = 0;
    int eighth_checked = 0;
    for (int trial = 0; trial < 120; trial++) {
        PieceConfig c = random_config();
        c.grid = GRID_EIGHTH;
        c.length = pick(8, 24);
        c.delay = 2 * pick(1, 6) - pick(0, 1);
        c.max_hold = pick(1, 5);
        if (c.modulate_at > c.length) c.modulate_at = c.length;
        for (int i = c.length; i < MELODY_MAX; i++) c.melody[i] = -1;
        char err[200];
        if (!config_validate(&c, err, sizeof(err))) continue;
        TestRun *r = test_solve(&c);
        if (r->status == SOLVE_SAT) {
            eighth_solved++;
            check_piece(&r->model, r->values);
        }
        if (r->status != SOLVE_LIMIT && c.length <= 10) {
            int values[VAR_MAX];
            int rc = sat_solve(&r->model, values, 200000);
            if (rc == SAT_SAT || rc == SAT_UNSAT) {
                eighth_checked++;
                CHECK((rc == SAT_SAT) == (r->status == SOLVE_SAT));
                if (rc == SAT_SAT) check_piece(&r->model, values);
            }
        }
        test_close(r);
    }
    printf("eighth grid solved %d, sat-checked %d\n", eighth_solved, eighth_checked);
    CHECK(eighth_solved >= 40);
    CHECK(eighth_checked >= 5);
    printf("ok\n");
    return 0;
}
