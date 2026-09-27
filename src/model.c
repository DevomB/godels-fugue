#include "model.h"

#include "canon.h"
#include "theory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *rule_name(int rule) {
    static const char *const names[CID_MAX] = {
        "",           "scale",      "range",           "melodic leap",
        "consonance", "parallel fifth", "parallel octave", "chord tone",
        "progression", "cadence",   "tie",             "max hold",
        "rest",       "lock",       "modulation",      "search",
        "learned conflict"};
    if (rule <= 0 || rule >= CID_MAX) return "unknown";
    return names[rule];
}

const char *term_name(int term) {
    static const char *const names[TERM_COUNT] = {
        "gravity",   "curve",       "corpus", "rest",  "leap",       "repeat",
        "recovery",  "motif",       "dissonance", "direct perfect", "hold",
        "syncopation", "rhythm",    "final",  "chord", "chord motion", "non-chord tone", "key",
        "key distance"};
    if (term < 0 || term >= TERM_COUNT) return "unknown";
    return names[term];
}

const char *var_kind_name(int kind) {
    static const char *const names[VAR_KIND_COUNT] = {"pitch", "tie", "chord", "key"};
    if (kind < 0 || kind >= VAR_KIND_COUNT) return "?";
    return names[kind];
}

int model_section_at(const Model *m, int time) {
    if (m->nsections > 1 && time >= m->config.modulate_at) return 1;
    return 0;
}

bool model_rule_used(const Model *m, int rule) {
    for (int i = 0; i < m->ncons; i++) {
        if (m->cons[i].rule == rule) return true;
    }
    return false;
}

static int add_var(Model *m, int kind, int index, const MidiDomain *dom) {
    int id = m->nvars++;
    m->vars[id].kind = kind;
    m->vars[id].index = index;
    m->initial[id] = *dom;
    return id;
}

static Constraint *push(Constraint **arr, int *n, int *cap) {
    if (*n == *cap) {
        int grown_cap = *cap == 0 ? 64 : *cap * 2;
        Constraint *grown = realloc(*arr, (size_t)grown_cap * sizeof(*grown));
        if (grown == NULL) return NULL;
        *arr = grown;
        *cap = grown_cap;
    }
    Constraint *c = &(*arr)[(*n)++];
    memset(c, 0, sizeof(*c));
    c->time = -1;
    return c;
}

typedef struct Builder {
    Model *m;
    bool oom;
} Builder;

static Constraint *add_con(Builder *b, int rule, int type) {
    Constraint *c = push(&b->m->cons, &b->m->ncons, &b->m->cons_cap);
    if (c == NULL) {
        b->oom = true;
        return NULL;
    }
    c->rule = rule;
    c->type = type;
    return c;
}

static Constraint *add_term(Builder *b, int term, int weight) {
    if (weight <= 0) return NULL;
    Constraint *t = push(&b->m->terms, &b->m->nterms, &b->m->terms_cap);
    if (t == NULL) {
        b->oom = true;
        return NULL;
    }
    t->rule = term;
    t->weight = weight;
    return t;
}

/* Voice -1 marks a slot that is not a sounding pitch. */
static void add_slot(Constraint *c, int var, int voice) {
    if (c == NULL || var < 0 || c->nslots >= SLOT_MAX) return;
    int pos = -1;
    for (int i = 0; i < c->n; i++) {
        if (c->vars[i] == var) pos = i;
    }
    if (pos < 0) {
        pos = c->n;
        c->vars[c->n++] = var;
    }
    c->slot[c->nslots] = pos;
    c->voice[c->nslots] = voice;
    c->nslots++;
}

static void key_modes(int mode, int modes[2], int *n) {
    if (mode < 0) {
        modes[0] = MODE_MAJOR;
        modes[1] = MODE_MINOR;
        *n = 2;
    } else {
        modes[0] = mode;
        *n = 1;
    }
}

static MidiDomain key_domain(int tonic, int mode) {
    MidiDomain d;
    int modes[2];
    int nm;
    domain_clear(&d);
    key_modes(mode, modes, &nm);
    for (int i = 0; i < nm; i++) {
        for (int t = 0; t < 12; t++) {
            if (tonic >= 0 && t != tonic) continue;
            domain_add(&d, key_id(t, modes[i]));
        }
    }
    return d;
}

static int sounding_count(const Model *m, int t) {
    int n = 0;
    for (int v = 0; v < m->voices; v++) {
        if (m->source[v][t] >= 0) n++;
    }
    return n;
}

static void build_vars(Builder *b) {
    Model *m = b->m;
    const PieceConfig *c = &m->config;
    MidiDomain d = key_domain(c->key, c->mode);

    m->key[0] = add_var(m, VAR_KEY, 0, &d);
    m->key[1] = -1;
    if (m->nsections > 1) {
        if (c->key_second == KEY_SEARCH) {
            d = key_domain(-1, c->mode_second);
        } else {
            int mode = c->mode_second >= 0 ? c->mode_second : c->mode;
            d = key_domain(c->key_second, mode);
        }
        m->key[1] = add_var(m, VAR_KEY, 1, &d);
    }

    for (int bar = 0; bar < BAR_MAX; bar++) m->chord[bar] = -1;
    if (c->harmony) {
        domain_clear(&d);
        for (int deg = 0; deg < DEGREE_COUNT; deg++) domain_add(&d, deg);
        for (int bar = 0; bar < m->nbars; bar++) m->chord[bar] = add_var(m, VAR_CHORD, bar, &d);
    }

    for (int i = 0; i < c->length; i++) {
        domain_fill_range(&d, c->range_low, c->range_high);
        if (c->rhythm) domain_add(&d, PITCH_REST);
        m->pitch[i] = add_var(m, VAR_PITCH, i, &d);
    }

    domain_clear(&d);
    domain_add(&d, TIE_NOTE);
    domain_add(&d, TIE_HOLD);
    for (int i = 0; i < c->length; i++) {
        m->tie[i] = -1;
        if (c->rhythm && i > 0) m->tie[i] = add_var(m, VAR_TIE, i, &d);
    }
}

static void build_pitch_rules(Builder *b) {
    Model *m = b->m;
    const PieceConfig *c = &m->config;
    int length = c->length;
    unsigned char sounds[MELODY_MAX][2][SECTION_MAX];
    memset(sounds, 0, sizeof(sounds));
    for (int v = 0; v < m->voices; v++) {
        for (int t = 0; t < m->span; t++) {
            int i = m->source[v][t];
            if (i >= 0) sounds[i][v > 0][model_section_at(m, t)] = 1;
        }
    }

    for (int i = 0; i < length; i++) {
        Constraint *r = add_con(b, CID_RANGE, C_RANGE);
        add_slot(r, m->pitch[i], 0);
        if (sounds[i][1][0] || sounds[i][1][1]) add_slot(r, m->pitch[i], 1);
        for (int sec = 0; sec < m->nsections; sec++) {
            if (!sounds[i][0][sec] && !sounds[i][1][sec]) continue;
            Constraint *s = add_con(b, CID_SCALE, C_SCALE);
            if (sounds[i][0][sec]) add_slot(s, m->pitch[i], 0);
            if (sounds[i][1][sec]) add_slot(s, m->pitch[i], 1);
            add_slot(s, m->key[sec], -1);
        }
    }

    if (c->lock) {
        Constraint *k = add_con(b, CID_LOCK, C_LOCK);
        add_slot(k, m->pitch[c->lock_index], -1);
        if (k != NULL) k->param = c->lock_pitch;
    }
    if (c->rest_at >= 0) {
        Constraint *r = add_con(b, CID_REST, C_REST_AT);
        add_slot(r, m->pitch[c->rest_at], -1);
    }

    /* Leaps along every voice's line. Transposition and plain inversion
     * keep interval sizes, so only a pitch-class inversion needs its own
     * copy of a pair already checked for the leader. */
    unsigned char seen[2][MELODY_MAX][MELODY_MAX];
    memset(seen, 0, sizeof(seen));
    for (int v = 0; v < m->voices; v++) {
        int cls = (v > 0 && c->invert && c->invert_mod12) ? 1 : 0;
        for (int t = 0; t + 1 < m->span; t++) {
            int i = m->source[v][t];
            int j = m->source[v][t + 1];
            if (i < 0 || j < 0 || i == j) continue;
            int lo = i < j ? i : j;
            int hi = i < j ? j : i;
            if (seen[cls][lo][hi]) continue;
            seen[cls][lo][hi] = 1;
            Constraint *l = add_con(b, CID_LEAP, C_LEAP);
            add_slot(l, m->pitch[i], cls);
            add_slot(l, m->pitch[j], cls);
            if (l != NULL) l->time = t;
        }
    }
}

static void build_vertical_rules(Builder *b) {
    Model *m = b->m;
    const PieceConfig *c = &m->config;

    if (c->consonance != CONSONANCE_OFF) {
        for (int t = 0; t < m->span; t++) {
            if (c->consonance == CONSONANCE_STRONG && !is_strong_time(t, c->poly_meter))
                continue;
            if (sounding_count(m, t) < 2) continue;
            Constraint *k = add_con(b, CID_CONSONANCE, C_CONSONANCE);
            for (int v = 0; v < m->voices; v++) {
                if (m->source[v][t] >= 0) add_slot(k, m->pitch[m->source[v][t]], v);
            }
            if (k != NULL) k->time = t;
        }
    }

    if (!c->parallels) return;
    for (int t = 0; t + 1 < m->span; t++) {
        for (int va = 0; va < m->voices; va++) {
            for (int vb = va + 1; vb < m->voices; vb++) {
                int i0 = m->source[va][t];
                int i1 = m->source[va][t + 1];
                int i2 = m->source[vb][t];
                int i3 = m->source[vb][t + 1];
                if (i0 < 0 || i1 < 0 || i2 < 0 || i3 < 0) continue;
                if (i0 == i1 || i2 == i3) continue; /* a voice that holds cannot move in parallel */
                for (int fifth = 1; fifth >= 0; fifth--) {
                    Constraint *p = add_con(b, fifth ? CID_PARALLEL_FIFTH : CID_PARALLEL_OCTAVE,
                                            C_PARALLEL);
                    add_slot(p, m->pitch[i0], va);
                    add_slot(p, m->pitch[i1], va);
                    add_slot(p, m->pitch[i2], vb);
                    add_slot(p, m->pitch[i3], vb);
                    if (p != NULL) {
                        p->param = fifth;
                        p->time = t;
                    }
                }
            }
        }
    }
}

static void build_harmony_rules(Builder *b) {
    Model *m = b->m;
    const PieceConfig *c = &m->config;
    if (!c->harmony) return;
    for (int t = 0; t < m->span; t++) {
        if (!is_strong_time(t, c->poly_meter)) continue;
        for (int v = 0; v < m->voices; v++) {
            int i = m->source[v][t];
            if (i < 0) continue;
            Constraint *k = add_con(b, CID_CHORD, C_CHORD_TONE);
            add_slot(k, m->pitch[i], v);
            add_slot(k, m->chord[t / 4], -1);
            add_slot(k, m->key[model_section_at(m, t)], -1);
            if (k != NULL) k->time = t;
        }
    }
    if (!c->progression) return;
    for (int bar = 0; bar + 1 < m->nbars; bar++) {
        /* a key change between the bars acts as a pivot: any chord may follow */
        if (model_section_at(m, bar * 4) != model_section_at(m, (bar + 1) * 4)) continue;
        Constraint *p = add_con(b, CID_PROGRESSION, C_PROGRESSION);
        add_slot(p, m->chord[bar], -1);
        add_slot(p, m->chord[bar + 1], -1);
        if (p != NULL) p->time = bar * 4;
    }
}

/* The melody ends on the tonic and the note before the final one comes
 * from the dominant triad. With ties the final note may start earlier:
 * approach rule k applies when the final note is held k extra steps. */
static void build_cadence_rules(Builder *b) {
    Model *m = b->m;
    const PieceConfig *c = &m->config;
    int length = c->length;
    if (!c->cadence) return;

    for (int v = 0; v < m->voices; v++) {
        int end = -1;
        for (int t = 0; t < m->span; t++) {
            if (m->source[v][t] >= 0) end = t;
        }
        if (end < 0) continue;
        Constraint *f = add_con(b, CID_CADENCE, C_CADENCE_FINAL);
        add_slot(f, m->pitch[m->source[v][end]], v);
        add_slot(f, m->key[model_section_at(m, end)], -1);
        if (f != NULL) {
            f->param = v == 0;
            f->time = end;
        }
    }

    int end_key = m->key[model_section_at(m, length - 1)];
    int kmax = c->rhythm ? c->max_hold : 0;
    for (int k = 0; k <= kmax; k++) {
        int approach = length - 2 - k;
        if (approach < 0) break;
        Constraint *a = add_con(b, CID_CADENCE, C_CADENCE_APPROACH);
        add_slot(a, m->pitch[approach], 0);
        add_slot(a, end_key, -1);
        for (int j = length - 1 - k; j < length; j++) add_slot(a, m->tie[j], -1);
        if (a != NULL) {
            a->param = k;
            a->time = approach;
        }
    }

    if (c->harmony) {
        Constraint *last = add_con(b, CID_CADENCE, C_CHORD_IS);
        add_slot(last, m->chord[m->nbars - 1], -1);
        if (last != NULL) last->param = 1 << DEGREE_I;
        if (m->nbars >= 2) {
            Constraint *pen = add_con(b, CID_CADENCE, C_CHORD_IS);
            add_slot(pen, m->chord[m->nbars - 2], -1);
            if (pen != NULL) pen->param = (1 << DEGREE_V) | (1 << DEGREE_VII);
        }
    }
}

static void build_rhythm_rules(Builder *b) {
    Model *m = b->m;
    const PieceConfig *c = &m->config;
    if (!c->rhythm) return;
    for (int i = 1; i < c->length; i++) {
        Constraint *t = add_con(b, CID_TIE, C_TIE);
        add_slot(t, m->tie[i], -1);
        add_slot(t, m->pitch[i - 1], -1);
        add_slot(t, m->pitch[i], -1);
        if (t != NULL) t->time = i;
    }
    for (int i = c->max_hold + 1; i < c->length; i++) {
        Constraint *h = add_con(b, CID_HOLD, C_MAX_HOLD);
        for (int j = i - c->max_hold; j <= i; j++) add_slot(h, m->tie[j], -1);
        if (h != NULL) h->time = i;
    }
    if (c->max_rests < c->length) {
        Constraint *r = add_con(b, CID_REST, C_MAX_RESTS);
        if (r != NULL) {
            r->param = c->max_rests;
            m->max_rests_con = m->ncons - 1;
        }
    }
}

static void build_key_rules(Builder *b) {
    Model *m = b->m;
    const PieceConfig *c = &m->config;
    if (m->nsections < 2) return;
    if (c->key_second == KEY_SEARCH) {
        Constraint *r = add_con(b, CID_MODULATION, C_KEY_RELATION);
        add_slot(r, m->key[0], -1);
        add_slot(r, m->key[1], -1);
    } else if (c->mode_second < 0 && c->mode < 0) {
        Constraint *r = add_con(b, CID_MODULATION, C_SAME_MODE);
        add_slot(r, m->key[0], -1);
        add_slot(r, m->key[1], -1);
    }
}

static bool uses_motif(const PieceConfig *c) {
    return c->w_motif > 0 && c->motif_a != -128;
}

static void build_terms(Builder *b) {
    Model *m = b->m;
    const PieceConfig *c = &m->config;
    int length = c->length;
    bool corpus = false;
    for (int pc = 0; pc < 12; pc++) {
        if (c->pc_weight[pc] > 0) corpus = true;
    }

    for (int i = 0; i < length; i++) {
        int key = m->key[model_section_at(m, i)];
        Constraint *t = add_term(b, TERM_GRAVITY, c->w_gravity);
        add_slot(t, m->pitch[i], 0);
        add_slot(t, key, -1);
        if (t != NULL) t->time = i;
        t = add_term(b, TERM_CURVE, c->w_curve);
        add_slot(t, m->pitch[i], 0);
        add_slot(t, key, -1);
        if (t != NULL) t->time = i;
        if (corpus) {
            t = add_term(b, TERM_CORPUS, 1);
            add_slot(t, m->pitch[i], 0);
            if (t != NULL) t->time = i;
        }
        if (c->rhythm) {
            t = add_term(b, TERM_REST, c->w_rest);
            add_slot(t, m->pitch[i], 0);
            if (t != NULL) t->time = i;
        }
    }
    for (int i = 0; i + 1 < length; i++) {
        Constraint *t = add_term(b, TERM_LEAP, c->w_leap);
        add_slot(t, m->pitch[i], 0);
        add_slot(t, m->pitch[i + 1], 0);
        if (t != NULL) t->time = i;
        t = add_term(b, TERM_REPEAT, c->w_repeat);
        add_slot(t, m->pitch[i], 0);
        add_slot(t, m->pitch[i + 1], 0);
        add_slot(t, m->tie[i + 1], -1);
        if (t != NULL) t->time = i + 1;
    }
    for (int i = 1; i + 1 < length; i++) {
        Constraint *t = add_term(b, TERM_RECOVER, c->w_recover);
        add_slot(t, m->pitch[i - 1], 0);
        add_slot(t, m->pitch[i], 0);
        add_slot(t, m->pitch[i + 1], 0);
        if (t != NULL) t->time = i;
    }
    if (uses_motif(c)) {
        for (int i = 1; i < length; i++) {
            Constraint *t = add_term(b, TERM_MOTIF, c->w_motif);
            add_slot(t, m->pitch[i - 1], 0);
            add_slot(t, m->pitch[i], 0);
            if (t != NULL) t->time = i;
        }
    }

    for (int t = 0; t < m->span; t++) {
        if (sounding_count(m, t) < 2) continue;
        Constraint *d = add_term(b, TERM_DISSONANCE, c->w_dissonance);
        for (int v = 0; v < m->voices; v++) {
            if (m->source[v][t] >= 0) add_slot(d, m->pitch[m->source[v][t]], v);
        }
        if (d != NULL) d->time = t;
    }
    for (int t = 0; t + 1 < m->span; t++) {
        for (int va = 0; va < m->voices; va++) {
            for (int vb = va + 1; vb < m->voices; vb++) {
                int i0 = m->source[va][t];
                int i1 = m->source[va][t + 1];
                int i2 = m->source[vb][t];
                int i3 = m->source[vb][t + 1];
                if (i0 < 0 || i1 < 0 || i2 < 0 || i3 < 0) continue;
                if (i0 == i1 || i2 == i3) continue;
                Constraint *d = add_term(b, TERM_DIRECT, c->w_parallel);
                add_slot(d, m->pitch[i0], va);
                add_slot(d, m->pitch[i1], va);
                add_slot(d, m->pitch[i2], vb);
                add_slot(d, m->pitch[i3], vb);
                if (d != NULL) d->time = t;
            }
        }
    }

    if (c->rhythm) {
        for (int i = 1; i < length; i++) {
            Constraint *t = add_term(b, TERM_HOLD, c->w_hold);
            add_slot(t, m->tie[i], -1);
            if (t != NULL) t->time = i;
            if (i >= 2 && i % 2 == 0) {
                t = add_term(b, TERM_SYNCOPATION, c->w_syncopation);
                add_slot(t, m->tie[i - 1], -1);
                add_slot(t, m->tie[i], -1);
                if (t != NULL) t->time = i;
            }
        }
        for (int bar = 0; bar * 4 + 3 < length; bar++) {
            Constraint *t = add_term(b, TERM_RHYTHM, c->w_rhythm);
            for (int j = 1; j <= 3; j++) add_slot(t, m->tie[bar * 4 + j], -1);
            if (t != NULL) t->time = bar * 4;
        }
        if (length >= 2) {
            Constraint *t = add_term(b, TERM_FINAL, c->w_final);
            add_slot(t, m->tie[length - 1], -1);
            if (t != NULL) t->time = length - 1;
        }
    }

    if (c->harmony) {
        for (int bar = 0; bar < m->nbars; bar++) {
            Constraint *t = add_term(b, TERM_CHORD, c->w_harmony);
            add_slot(t, m->chord[bar], -1);
            if (t != NULL) t->time = bar * 4;
            if (bar == 0) continue;
            t = add_term(b, TERM_CHORD_MOTION, c->w_harmony);
            add_slot(t, m->chord[bar - 1], -1);
            add_slot(t, m->chord[bar], -1);
            if (t != NULL) t->time = bar * 4;
        }
        for (int t = 0; t < m->span; t++) {
            if (is_strong_time(t, c->poly_meter)) continue;
            for (int v = 0; v < m->voices; v++) {
                int i = m->source[v][t];
                if (i < 0) continue;
                Constraint *n = add_term(b, TERM_NONCHORD, c->w_harmony);
                add_slot(n, m->pitch[i], v);
                add_slot(n, m->chord[t / 4], -1);
                add_slot(n, m->key[model_section_at(m, t)], -1);
                if (n != NULL) n->time = t;
            }
        }
    }

    if (domain_count(&m->initial[m->key[0]]) > 1) {
        Constraint *t = add_term(b, TERM_KEY, c->w_modulate);
        add_slot(t, m->key[0], -1);
    }
    if (m->nsections > 1) {
        Constraint *t = add_term(b, TERM_KEY_DISTANCE, c->w_modulate);
        add_slot(t, m->key[0], -1);
        add_slot(t, m->key[1], -1);
        if (t != NULL) t->time = c->modulate_at;
    }
}

static bool build_adjacency(const Model *m, const Constraint *arr, int n, int **start_out,
                            int **adj_out, int *degree) {
    int *start = calloc((size_t)m->nvars + 1, sizeof(int));
    if (start == NULL) return false;
    int total = 0;
    for (int k = 0; k < n; k++) {
        const Constraint *c = &arr[k];
        if (c->type == C_MAX_RESTS && arr == m->cons) {
            for (int i = 0; i < m->config.length; i++) start[m->pitch[i] + 1]++;
            total += m->config.length;
            continue;
        }
        for (int i = 0; i < c->n; i++) start[c->vars[i] + 1]++;
        total += c->n;
    }
    for (int v = 0; v < m->nvars; v++) start[v + 1] += start[v];
    int *adj = malloc((size_t)(total > 0 ? total : 1) * sizeof(int));
    int *fill = malloc((size_t)(m->nvars > 0 ? m->nvars : 1) * sizeof(int));
    if (adj == NULL || fill == NULL) {
        free(start);
        free(adj);
        free(fill);
        return false;
    }
    memcpy(fill, start, (size_t)m->nvars * sizeof(int));
    for (int k = 0; k < n; k++) {
        const Constraint *c = &arr[k];
        if (c->type == C_MAX_RESTS && arr == m->cons) {
            for (int i = 0; i < m->config.length; i++) adj[fill[m->pitch[i]]++] = k;
            continue;
        }
        for (int i = 0; i < c->n; i++) adj[fill[c->vars[i]]++] = k;
    }
    if (degree != NULL) {
        for (int v = 0; v < m->nvars; v++) degree[v] = start[v + 1] - start[v];
    }
    free(fill);
    *start_out = start;
    *adj_out = adj;
    return true;
}

bool model_build(Model *m, const PieceConfig *config, char *err, size_t cap) {
    memset(m, 0, sizeof(*m));
    m->config = *config;
    m->max_rests_con = -1;
    if (config->length < 1 || config->length > MELODY_MAX) {
        snprintf(err, cap, "invalid length");
        return false;
    }
    m->span = canon_span_config(config);
    if (m->span < 1 || m->span > SPAN_MAX) {
        snprintf(err, cap, "piece too long: canon spans more than %d steps", SPAN_MAX);
        return false;
    }
    m->voices = config_voice_count(config);
    m->nbars = (m->span + 3) / 4;
    m->nsections = config->modulate_at >= 0 ? 2 : 1;
    for (int v = 0; v < VOICE_MAX; v++) {
        for (int t = 0; t < SPAN_MAX; t++) {
            m->source[v][t] = (v < m->voices && t < m->span) ? canon_map_source(config, v, t) : -1;
        }
    }

    Builder b = {m, false};
    build_vars(&b);
    build_pitch_rules(&b);
    build_vertical_rules(&b);
    build_harmony_rules(&b);
    build_cadence_rules(&b);
    build_rhythm_rules(&b);
    build_key_rules(&b);
    build_terms(&b);
    if (b.oom || !model_link(m)) {
        snprintf(err, cap, "out of memory");
        model_free(m);
        return false;
    }
    return true;
}

bool model_link(Model *m) {
    for (int v = 0; v < m->nvars; v++) {
        for (int value = 0; value < 128; value++)
            m->rank[v][value] = (unsigned char)domain_rank(&m->initial[v], value);
    }
    free(m->cons_adj_start);
    free(m->cons_adj);
    free(m->terms_adj_start);
    free(m->terms_adj);
    m->cons_adj_start = m->cons_adj = m->terms_adj_start = m->terms_adj = NULL;
    return build_adjacency(m, m->cons, m->ncons, &m->cons_adj_start, &m->cons_adj,
                           m->degree) &&
           build_adjacency(m, m->terms, m->nterms, &m->terms_adj_start, &m->terms_adj, NULL);
}

void model_free(Model *m) {
    free(m->cons);
    free(m->terms);
    free(m->cons_adj_start);
    free(m->cons_adj);
    free(m->terms_adj_start);
    free(m->terms_adj);
    memset(m, 0, sizeof(*m));
}

static int slot_value(const Constraint *c, const int *vals, int k) {
    return vals[c->slot[k]];
}

static int slot_sound(const Model *m, const Constraint *c, const int *vals, int k) {
    return canon_sounding(&m->config, c->voice[k], slot_value(c, vals, k));
}

bool constraint_holds(const Model *m, const Constraint *c, const int *vals) {
    const PieceConfig *cfg = &m->config;
    switch (c->type) {
    case C_RANGE:
        for (int k = 0; k < c->nslots; k++) {
            int s = slot_sound(m, c, vals, k);
            if (s == SOUND_REST) continue;
            if (s < cfg->range_low || s > cfg->range_high) return false;
        }
        return true;
    case C_SCALE: {
        int key = slot_value(c, vals, c->nslots - 1);
        for (int k = 0; k + 1 < c->nslots; k++) {
            int s = slot_sound(m, c, vals, k);
            if (s == SOUND_REST) continue;
            if (!key_has_pitch(key, s)) return false;
        }
        return true;
    }
    case C_LEAP: {
        int a = slot_sound(m, c, vals, 0);
        int b = slot_sound(m, c, vals, 1);
        if (a == SOUND_REST || b == SOUND_REST) return true;
        return !leap_exceeds(a, b, cfg->max_leap);
    }
    case C_CONSONANCE: {
        int pitches[SLOT_MAX];
        int n = 0;
        for (int k = 0; k < c->nslots; k++) {
            int s = slot_sound(m, c, vals, k);
            if (s != SOUND_REST) pitches[n++] = s;
        }
        return sonority_consonant(pitches, n, cfg->allow_fourth, cfg->allow_unison);
    }
    case C_PARALLEL: {
        int s[4];
        for (int k = 0; k < 4; k++) {
            s[k] = slot_sound(m, c, vals, k);
            if (s[k] == SOUND_REST) return true;
        }
        return c->param ? !is_parallel_fifth(s[0], s[2], s[1], s[3])
                        : !is_parallel_octave(s[0], s[2], s[1], s[3]);
    }
    case C_CHORD_TONE: {
        int s = slot_sound(m, c, vals, 0);
        if (s == SOUND_REST) return true;
        return key_triad_has(slot_value(c, vals, 2), slot_value(c, vals, 1), s);
    }
    case C_PROGRESSION:
        return progression_allowed(slot_value(c, vals, 0), slot_value(c, vals, 1));
    case C_CHORD_IS:
        return (c->param >> slot_value(c, vals, 0)) & 1;
    case C_CADENCE_FINAL: {
        int s = slot_sound(m, c, vals, 0);
        int key = slot_value(c, vals, 1);
        if (s == SOUND_REST) return false;
        if (c->param) return pitch_class(s) == key_tonic(key);
        return key_triad_has(key, DEGREE_I, s);
    }
    case C_CADENCE_APPROACH: {
        /* slot 2, if present, is the final note's onset tie; later
         * slots are the ties holding it to the end */
        for (int k = 2; k < c->nslots; k++) {
            int want = k == 2 ? TIE_NOTE : TIE_HOLD;
            if (slot_value(c, vals, k) != want) return true;
        }
        int s = slot_sound(m, c, vals, 0);
        if (s == SOUND_REST) return false;
        return key_triad_has(slot_value(c, vals, 1), DEGREE_V, s);
    }
    case C_TIE: {
        if (slot_value(c, vals, 0) != TIE_HOLD) return true;
        int prev = slot_value(c, vals, 1);
        int now = slot_value(c, vals, 2);
        return prev == now && now != PITCH_REST;
    }
    case C_MAX_HOLD:
        for (int k = 0; k < c->nslots; k++) {
            if (slot_value(c, vals, k) != TIE_HOLD) return true;
        }
        return false;
    case C_REST_AT:
        return slot_value(c, vals, 0) == PITCH_REST;
    case C_LOCK:
        return slot_value(c, vals, 0) == c->param;
    case C_KEY_RELATION:
        return keys_closely_related(slot_value(c, vals, 0), slot_value(c, vals, 1));
    case C_SAME_MODE:
        return key_mode(slot_value(c, vals, 0)) == key_mode(slot_value(c, vals, 1));
    case C_MAX_RESTS:
    default:
        return true;
    }
}

static int iabs(int x) {
    return x < 0 ? -x : x;
}

int term_cost(const Model *m, const Constraint *t, const int *vals) {
    const PieceConfig *cfg = &m->config;
    int w = t->weight;
    switch (t->rule) {
    case TERM_GRAVITY:
    case TERM_CURVE: {
        int p = slot_value(t, vals, 0);
        if (p == PITCH_REST) return 0;
        int g = pitch_gravity(p, slot_value(t, vals, 1));
        if (t->rule == TERM_GRAVITY) return w * g;
        return w * iabs(g - tension_target(t->time, cfg->length));
    }
    case TERM_CORPUS: {
        int p = slot_value(t, vals, 0);
        return p == PITCH_REST ? 0 : w * cfg->pc_weight[pitch_class(p)];
    }
    case TERM_REST:
        return slot_value(t, vals, 0) == PITCH_REST ? w : 0;
    case TERM_LEAP: {
        int a = slot_value(t, vals, 0);
        int b = slot_value(t, vals, 1);
        if (a == PITCH_REST || b == PITCH_REST) return 0;
        return w * (iabs(a - b) / 4);
    }
    case TERM_REPEAT: {
        int a = slot_value(t, vals, 0);
        int b = slot_value(t, vals, 1);
        if (a != b || a == PITCH_REST) return 0;
        if (t->nslots > 2 && slot_value(t, vals, 2) == TIE_HOLD) return 0;
        return w;
    }
    case TERM_RECOVER: {
        int a = slot_value(t, vals, 0);
        int b = slot_value(t, vals, 1);
        int c = slot_value(t, vals, 2);
        if (a == PITCH_REST || b == PITCH_REST || c == PITCH_REST) return 0;
        int leap = b - a;
        if (iabs(leap) <= 4) return 0;
        int step = c - b;
        bool recovers = step != 0 && iabs(step) <= 2 && ((step > 0) != (leap > 0));
        return recovers ? 0 : w;
    }
    case TERM_MOTIF: {
        int a = slot_value(t, vals, 0);
        int b = slot_value(t, vals, 1);
        if (a == PITCH_REST || b == PITCH_REST) return 0;
        int pat[4];
        int n = 0;
        const int slots[4] = {cfg->motif_a, cfg->motif_b, cfg->motif_c, cfg->motif_d};
        for (int i = 0; i < 4 && slots[i] != -128; i++) pat[n++] = slots[i];
        if (n == 0) return 0;
        return (b - a) == pat[(t->time - 1) % n] ? 0 : w;
    }
    case TERM_DISSONANCE: {
        int s[SLOT_MAX];
        int n = 0;
        int cost = 0;
        for (int k = 0; k < t->nslots; k++) {
            int p = slot_sound(m, t, vals, k);
            if (p != SOUND_REST) s[n++] = p;
        }
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) cost += dissonance_grade(s[i], s[j]);
        }
        return w * cost;
    }
    case TERM_DIRECT: {
        int s[4];
        for (int k = 0; k < 4; k++) {
            s[k] = slot_sound(m, t, vals, k);
            if (s[k] == SOUND_REST) return 0;
        }
        return is_direct_perfect(s[0], s[2], s[1], s[3]) ? w : 0;
    }
    case TERM_HOLD:
        return slot_value(t, vals, 0) == TIE_HOLD ? w : 0;
    case TERM_SYNCOPATION:
        return slot_value(t, vals, 0) == TIE_NOTE && slot_value(t, vals, 1) == TIE_HOLD
                   ? w
                   : 0;
    case TERM_RHYTHM:
        for (int k = 0; k < t->nslots; k++) {
            if (slot_value(t, vals, k) != TIE_NOTE) return 0;
        }
        return w;
    case TERM_FINAL:
        return slot_value(t, vals, 0) == TIE_NOTE ? w : 0;
    case TERM_CHORD: {
        static const int cost[DEGREE_COUNT] = {0, 1, 2, 0, 0, 1, 2};
        int d = slot_value(t, vals, 0);
        return d >= 0 && d < DEGREE_COUNT ? w * cost[d] : 0;
    }
    case TERM_CHORD_MOTION: {
        /* strongest: the root falls a fifth (V-I, ii-V) or rises a step
         * (IV-V); repeating a chord is weakest */
        int from = slot_value(t, vals, 0);
        int to = slot_value(t, vals, 1);
        int step = ((to - from) % DEGREE_COUNT + DEGREE_COUNT) % DEGREE_COUNT;
        if (from == to) return 2 * w;
        return step == 3 || step == 1 ? 0 : w;
    }
    case TERM_NONCHORD: {
        int s = slot_sound(m, t, vals, 0);
        if (s == SOUND_REST) return 0;
        return key_triad_has(slot_value(t, vals, 2), slot_value(t, vals, 1), s) ? 0 : w;
    }
    case TERM_KEY:
        return w * iabs(key_fifths(slot_value(t, vals, 0)));
    case TERM_KEY_DISTANCE:
        return w * key_distance(slot_value(t, vals, 0), slot_value(t, vals, 1));
    default:
        return 0;
    }
}

int model_energy(const Model *m, const int *values, int *breakdown) {
    int total = 0;
    if (breakdown != NULL) memset(breakdown, 0, TERM_COUNT * sizeof(int));
    for (int k = 0; k < m->nterms; k++) {
        const Constraint *t = &m->terms[k];
        int vals[SCOPE_MAX];
        for (int i = 0; i < t->n; i++) vals[i] = values[t->vars[i]];
        int cost = term_cost(m, t, vals);
        total += cost;
        if (breakdown != NULL) breakdown[t->rule] += cost;
    }
    return total;
}

/* The configured key when it is fixed, else -1. */
static int fixed_key(const Model *m) {
    const PieceConfig *c = &m->config;
    if (c->key < 0 || c->mode < 0) return -1;
    return key_id(c->key, c->mode);
}

void var_label(const Model *m, int var, char *buf, size_t cap) {
    if (var < 0 || var >= m->nvars) {
        snprintf(buf, cap, "?");
        return;
    }
    const ModelVar *v = &m->vars[var];
    switch (v->kind) {
    case VAR_PITCH:
        snprintf(buf, cap, "x%d", v->index);
        break;
    case VAR_TIE:
        snprintf(buf, cap, "tie%d", v->index);
        break;
    case VAR_CHORD:
        snprintf(buf, cap, "chord%d", v->index);
        break;
    default:
        snprintf(buf, cap, v->index == 0 ? "key" : "key%d", v->index + 1);
        break;
    }
}

void value_label(const Model *m, int var, int value, char *buf, size_t cap) {
    if (var < 0 || var >= m->nvars) {
        snprintf(buf, cap, "%d", value);
        return;
    }
    switch (m->vars[var].kind) {
    case VAR_PITCH:
        if (value == PITCH_REST) {
            snprintf(buf, cap, "rest");
        } else {
            key_pitch_name(fixed_key(m), value, buf, cap);
        }
        break;
    case VAR_TIE:
        snprintf(buf, cap, "%s", value == TIE_HOLD ? "hold" : "note");
        break;
    case VAR_CHORD: {
        static const char *const numerals[DEGREE_COUNT] = {"I",  "II", "III", "IV",
                                                           "V",  "VI", "VII"};
        snprintf(buf, cap, "%s", value >= 0 && value < DEGREE_COUNT ? numerals[value] : "?");
        break;
    }
    default:
        key_name(value, buf, cap);
        break;
    }
}

void constraint_describe(const Model *m, const Constraint *c, char *buf, size_t cap) {
    const PieceConfig *cfg = &m->config;
    int bar = c->time >= 0 ? c->time / 4 + 1 : 0;
    char voices[32];
    size_t used = 0;
    voices[0] = '\0';
    for (int k = 0; k < c->nslots && c->voice[k] >= 0; k++) {
        bool dup = false;
        for (int j = 0; j < k; j++) {
            if (c->voice[j] == c->voice[k]) dup = true;
        }
        if (dup) continue;
        int w = snprintf(voices + used, sizeof(voices) - used, "%s%d", used ? "," : "",
                         c->voice[k] + 1);
        if (w < 0 || (size_t)w >= sizeof(voices) - used) break;
        used += (size_t)w;
    }
    switch (c->type) {
    case C_RANGE:
        snprintf(buf, cap, "every voice stays within %d..%d", cfg->range_low,
                 cfg->range_high);
        break;
    case C_SCALE:
        snprintf(buf, cap, "notes stay in the key");
        break;
    case C_LEAP:
        snprintf(buf, cap, "leap of at most %d semitones", cfg->max_leap);
        break;
    case C_CONSONANCE:
        snprintf(buf, cap, "voices %s must be consonant at step %d (bar %d)", voices,
                 c->time, bar);
        break;
    case C_PARALLEL:
        snprintf(buf, cap, "voices %s at steps %d-%d", voices, c->time, c->time + 1);
        break;
    case C_CHORD_TONE:
        snprintf(buf, cap, "voice %s at step %d plays a tone of the bar %d chord", voices,
                 c->time, bar);
        break;
    case C_PROGRESSION:
        snprintf(buf, cap, "chord of bar %d must lead to the chord of bar %d", bar, bar + 1);
        break;
    case C_CHORD_IS:
        snprintf(buf, cap, "cadence chord");
        break;
    case C_CADENCE_FINAL:
        snprintf(buf, cap, c->param ? "the melody ends on the tonic"
                                    : "voice %s ends on a tonic-triad note",
                 voices);
        break;
    case C_CADENCE_APPROACH:
        snprintf(buf, cap, "the note before the final note is in the dominant triad");
        break;
    case C_TIE:
        snprintf(buf, cap, "a tie at %d repeats the pitch of %d", c->time, c->time - 1);
        break;
    case C_MAX_HOLD:
        snprintf(buf, cap, "at most %d tied steps in a row", cfg->max_hold);
        break;
    case C_REST_AT:
        snprintf(buf, cap, "rest_at forces a rest");
        break;
    case C_MAX_RESTS:
        snprintf(buf, cap, "at most %d rests", cfg->max_rests);
        break;
    case C_LOCK:
        snprintf(buf, cap, "locked by the user");
        break;
    case C_KEY_RELATION:
        snprintf(buf, cap, "the second key is closely related to the first");
        break;
    case C_SAME_MODE:
        snprintf(buf, cap, "both keys share a mode");
        break;
    default:
        snprintf(buf, cap, "%s", rule_name(c->rule));
        break;
    }
}
