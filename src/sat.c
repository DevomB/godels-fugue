#include "sat.h"

#include "canon.h"
#include "theory.h"

#include <string.h>

enum { SAT_VAR_MAX = 256, SAT_CLAUSE_MAX = 8192, SAT_LIT_MAX = 16 };

typedef struct {
    int n;
    int lit[SAT_LIT_MAX];
} SatClause;

typedef struct {
    int nvars;
    int nclauses;
    SatClause clauses[SAT_CLAUSE_MAX];
    int var_of[MELODY_MAX][128];
    int note_of[SAT_VAR_MAX + 1];
    int pitch_of[SAT_VAR_MAX + 1];
    int n_pitches[MELODY_MAX];
    int pitches[MELODY_MAX][16];
    int val[SAT_VAR_MAX + 1];
    int trail[SAT_VAR_MAX];
    int trail_n;
} SatState;

static int add_var(SatState *s, int note, int pitch) {
    if (s->nvars >= SAT_VAR_MAX) return 0;
    s->nvars += 1;
    s->var_of[note][pitch] = s->nvars;
    s->note_of[s->nvars] = note;
    s->pitch_of[s->nvars] = pitch;
    return 1;
}

static int add_clause(SatState *s, const int *lits, int n) {
    if (n < 1 || n > SAT_LIT_MAX || s->nclauses >= SAT_CLAUSE_MAX) return 0;
    SatClause *c = &s->clauses[s->nclauses++];
    c->n = n;
    memcpy(c->lit, lits, (size_t)n * sizeof(int));
    return 1;
}

static int add_bin(SatState *s, int a, int b) {
    int lits[2] = {a, b};
    return add_clause(s, lits, 2);
}

static int lit_value(const SatState *s, int lit) {
    int v = lit < 0 ? -lit : lit;
    int val = s->val[v];
    if (val == 0) return 0;
    return lit < 0 ? -val : val;
}

static void assign_lit(SatState *s, int lit) {
    int v = lit < 0 ? -lit : lit;
    s->val[v] = lit < 0 ? -1 : 1;
    s->trail[s->trail_n++] = v;
}

static int propagate(SatState *s) {
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < s->nclauses; i++) {
            const SatClause *c = &s->clauses[i];
            int open = 0;
            int last = 0;
            int sat = 0;
            for (int k = 0; k < c->n; k++) {
                int lv = lit_value(s, c->lit[k]);
                if (lv > 0) {
                    sat = 1;
                    break;
                }
                if (lv == 0) {
                    open += 1;
                    last = c->lit[k];
                }
            }
            if (sat) continue;
            if (open == 0) return 0;
            if (open == 1) {
                assign_lit(s, last);
                changed = 1;
            }
        }
    }
    return 1;
}

static int dpll(SatState *s) {
    int mark = s->trail_n;
    if (!propagate(s)) {
        while (s->trail_n > mark) {
            s->trail_n -= 1;
            s->val[s->trail[s->trail_n]] = 0;
        }
        return 0;
    }
    int pick = 0;
    for (int v = 1; v <= s->nvars; v++) {
        if (s->val[v] == 0) {
            pick = v;
            break;
        }
    }
    if (pick == 0) return 1;
    assign_lit(s, pick);
    if (dpll(s)) return 1;
    while (s->trail_n > mark) {
        s->trail_n -= 1;
        s->val[s->trail[s->trail_n]] = 0;
    }
    assign_lit(s, -pick);
    if (dpll(s)) return 1;
    while (s->trail_n > mark) {
        s->trail_n -= 1;
        s->val[s->trail[s->trail_n]] = 0;
    }
    return 0;
}

static int fill_domains(SatState *s, const PieceConfig *c) {
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < c->length; i++) {
        for (int p = c->range_low; p <= c->range_high; p++) {
            if (!pitch_in_scale(p, i, c->modulate_at)) continue;
            int sound = canon_sounding(c, 1, p);
            if (sound < c->range_low || sound > c->range_high) continue;
            if (!pitch_in_scale(sound, i, c->modulate_at)) continue;
            if (s->n_pitches[i] >= 16) return -1;
            s->pitches[i][s->n_pitches[i]++] = p;
            if (!add_var(s, i, p)) return -1;
        }
        if (s->n_pitches[i] == 0) return 0;
    }
    return 1;
}

static int encode_exact_one(SatState *s, int length) {
    for (int i = 0; i < length; i++) {
        int n = s->n_pitches[i];
        int lits[SAT_LIT_MAX];
        if (n > SAT_LIT_MAX) return 0;
        for (int k = 0; k < n; k++) {
            lits[k] = s->var_of[i][s->pitches[i][k]];
        }
        if (!add_clause(s, lits, n)) return 0;
        for (int a = 0; a < n; a++) {
            for (int b = a + 1; b < n; b++) {
                if (!add_bin(s, -lits[a], -lits[b])) return 0;
            }
        }
    }
    return 1;
}

static int encode_leap(SatState *s, const PieceConfig *c) {
    for (int i = 0; i + 1 < c->length; i++) {
        for (int a = 0; a < s->n_pitches[i]; a++) {
            int p = s->pitches[i][a];
            for (int b = 0; b < s->n_pitches[i + 1]; b++) {
                int q = s->pitches[i + 1][b];
                if (!leap_exceeds(p, q, c->max_leap)) continue;
                if (!add_bin(s, -s->var_of[i][p], -s->var_of[i + 1][q]))
                    return 0;
            }
        }
    }
    return 1;
}

static int encode_second(SatState *s, const PieceConfig *c) {
    int voices = c->voices;
    if (voices < 2) voices = 2;
    if (voices > VOICE_MAX) voices = VOICE_MAX;
    int span = canon_span_config(c);
    for (int t = 0; t < span; t++) {
        if (!is_strong_time(t, c->poly_meter)) continue;
        for (int va = 0; va < voices; va++) {
            for (int vb = va + 1; vb < voices; vb++) {
                int i0 = canon_map_source(c, va, t);
                int i1 = canon_map_source(c, vb, t);
                if (i0 < 0 || i1 < 0) continue;
                for (int a = 0; a < s->n_pitches[i0]; a++) {
                    int p = s->pitches[i0][a];
                    int sp = canon_sounding(c, va, p);
                    for (int b = 0; b < s->n_pitches[i1]; b++) {
                        int q = s->pitches[i1][b];
                        if (!is_second(sp, canon_sounding(c, vb, q))) continue;
                        if (!add_bin(s, -s->var_of[i0][p], -s->var_of[i1][q]))
                            return 0;
                    }
                }
            }
        }
    }
    return 1;
}

static int encode_parallel(SatState *s, const PieceConfig *c) {
    int voices = c->voices;
    if (voices < 2) voices = 2;
    if (voices > VOICE_MAX) voices = VOICE_MAX;
    int span = canon_span_config(c);
    for (int t = 0; t + 1 < span; t++) {
        for (int va = 0; va < voices; va++) {
            for (int vb = va + 1; vb < voices; vb++) {
                int i0 = canon_map_source(c, va, t);
                int i1 = canon_map_source(c, va, t + 1);
                int i2 = canon_map_source(c, vb, t);
                int i3 = canon_map_source(c, vb, t + 1);
                if (i0 < 0 || i1 < 0 || i2 < 0 || i3 < 0) continue;
                if (i0 == i1 || i0 == i2 || i0 == i3 || i1 == i2 || i1 == i3 ||
                    i2 == i3)
                    continue;
                for (int a = 0; a < s->n_pitches[i0]; a++) {
                    int p0 = s->pitches[i0][a];
                    int s0 = canon_sounding(c, va, p0);
                    for (int b = 0; b < s->n_pitches[i1]; b++) {
                        int p1 = s->pitches[i1][b];
                        int s1 = canon_sounding(c, va, p1);
                        for (int d = 0; d < s->n_pitches[i2]; d++) {
                            int p2 = s->pitches[i2][d];
                            int s2 = canon_sounding(c, vb, p2);
                            for (int e = 0; e < s->n_pitches[i3]; e++) {
                                int p3 = s->pitches[i3][e];
                                int s3 = canon_sounding(c, vb, p3);
                                if (!is_parallel_fifth(s0, s2, s1, s3) &&
                                    !is_parallel_octave(s0, s2, s1, s3))
                                    continue;
                                int lits[4] = {-s->var_of[i0][p0],
                                               -s->var_of[i1][p1],
                                               -s->var_of[i2][p2],
                                               -s->var_of[i3][p3]};
                                if (!add_clause(s, lits, 4)) return 0;
                            }
                        }
                    }
                }
            }
        }
    }
    return 1;
}

static int encode_unary_ok(SatState *s, const PieceConfig *c, int idx, int voice,
                           int (*ok)(int)) {
    for (int k = 0; k < s->n_pitches[idx]; k++) {
        int p = s->pitches[idx][k];
        if (ok(canon_sounding(c, voice, p))) continue;
        int lit = -s->var_of[idx][p];
        if (!add_clause(s, &lit, 1)) return 0;
    }
    return 1;
}

static int encode_harmony(SatState *s, const PieceConfig *c) {
    int voices = c->voices;
    if (voices < 1) voices = 1;
    if (voices > VOICE_MAX) voices = VOICE_MAX;
    int span = canon_span_config(c);
    if (c->strong_chord) {
        for (int t = 0; t < span; t += 4) {
            for (int v = 0; v < voices; v++) {
                int idx = canon_map_source(c, v, t);
                if (idx < 0) continue;
                if (!encode_unary_ok(s, c, idx, v, in_c_triad)) return 0;
            }
        }
    }
    if (c->cadence) {
        int t = ((span - 1) / 4) * 4;
        if (t < 0) t = 0;
        for (int v = 0; v < voices; v++) {
            int idx = canon_map_source(c, v, t);
            if (idx < 0) continue;
            if (!encode_unary_ok(s, c, idx, v, in_c_dominant)) return 0;
        }
    }
    return 1;
}

int sat_solve(const PieceConfig *config, int *melody) {
    if (config == NULL || config->length < 1 || config->length > MELODY_MAX) {
        return -1;
    }
    static SatState s;
    int built = fill_domains(&s, config);
    if (built < 0) return -1;
    if (built == 0) return 0;
    if (!encode_exact_one(&s, config->length) || !encode_leap(&s, config) ||
        !encode_second(&s, config) || !encode_parallel(&s, config) ||
        !encode_harmony(&s, config)) {
        return -1;
    }
    if (!dpll(&s)) return 0;
    if (melody != NULL) {
        for (int i = 0; i < config->length; i++) {
            melody[i] = -1;
            for (int k = 0; k < s.n_pitches[i]; k++) {
                int p = s.pitches[i][k];
                int v = s.var_of[i][p];
                if (s.val[v] > 0) {
                    melody[i] = p;
                    break;
                }
            }
        }
    }
    return 1;
}
