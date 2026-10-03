#include "check.h"

#include "canon.h"
#include "theory.h"

#include <string.h>

/* Assignments of a constraint's free variables tried before giving up
 * on it: a rule over many open notes is the search's business. */
enum { CHECK_TRIALS = 1 << 15 };

/* The value a variable has before any search, or -1 if it is free. A
 * lock gives the value even when the domain still holds others, and
 * rest_at gives a rest. */
static int fixed_value(const Model *m, int var) {
    for (int a = m->cons_adj_start[var]; a < m->cons_adj_start[var + 1]; a++) {
        const Constraint *c = &m->cons[m->cons_adj[a]];
        if (c->type == C_LOCK) return c->param;
        if (c->type == C_REST_AT) return PITCH_REST;
    }
    if (domain_singleton(&m->initial[var])) return domain_value(&m->initial[var]);
    return -1;
}

/* A tie between two fixed notes is no choice when they differ or the
 * second is a rest: it starts a new note. */
static int implied_value(const Model *m, int var, const int *fixed) {
    const ModelVar *mv = &m->vars[var];
    if (fixed[var] >= 0 || mv->kind != VAR_TIE) return fixed[var];
    int prev = fixed[m->pitch[mv->index - 1]];
    int now = fixed[m->pitch[mv->index]];
    if (prev < 0 || now < 0) return -1;
    return prev != now || now == PITCH_REST ? TIE_NOTE : -1;
}

/* True if some values of the unknown variables, each from its initial
 * domain, satisfy c, or if there are too many to try. */
static bool satisfiable(const Model *m, const Constraint *c, const int *known) {
    int vals[SCOPE_MAX];
    int pos[SCOPE_MAX];
    int count[SCOPE_MAX];
    int at[SCOPE_MAX];
    int options[SCOPE_MAX][128];
    int nfree = 0;
    long trials = 1;
    for (int k = 0; k < c->n; k++) {
        vals[k] = known[c->vars[k]];
        if (vals[k] >= 0) continue;
        count[nfree] = domain_collect(&m->initial[c->vars[k]], options[nfree]);
        trials *= count[nfree];
        if (trials == 0 || trials > CHECK_TRIALS) return true;
        at[nfree] = 0;
        pos[nfree++] = k;
    }
    for (;;) {
        for (int f = 0; f < nfree; f++) vals[pos[f]] = options[f][at[f]];
        if (constraint_holds(m, c, vals)) return true;
        int f = 0;
        while (f < nfree && ++at[f] == count[f]) at[f++] = 0;
        if (f == nfree) return false;
    }
}

/* Broken when no values of its free variables satisfy it; a constraint
 * on no fixed variable is left to the search. Names the fixed ones. */
static bool judge(const Model *m, const Constraint *c, const int *fixed, const int *known,
                  Violation *v) {
    v->nvars = 0;
    for (int k = 0; k < c->n; k++) {
        int var = c->vars[k];
        if (fixed[var] < 0) continue;
        v->vars[v->nvars] = var;
        v->vals[v->nvars++] = fixed[var];
    }
    if (v->nvars == 0 || satisfiable(m, c, known)) return false;
    /* an earlier lock holds the note; name the value this one gives */
    if (c->type == C_LOCK) v->vals[0] = c->param;
    return true;
}

/* The rest count has no scope: the given rests alone must not exceed it. */
static bool judge_rests(const Model *m, const Constraint *c, const int *fixed, Violation *v) {
    int n = 0;
    for (int i = 0; i < m->config.length; i++) {
        int var = m->pitch[i];
        if (fixed[var] != PITCH_REST) continue;
        if (n < MELODY_MAX) {
            v->vars[n] = var;
            v->vals[n] = PITCH_REST;
        }
        n++;
    }
    v->nvars = n < MELODY_MAX ? n : MELODY_MAX;
    return n > c->param;
}

int check_given(const Model *m, Violation *out, int cap) {
    int fixed[VAR_MAX];
    int known[VAR_MAX];
    for (int var = 0; var < m->nvars; var++) fixed[var] = fixed_value(m, var);
    for (int var = 0; var < m->nvars; var++) known[var] = implied_value(m, var, fixed);
    int n = 0;
    Violation spare;
    for (int i = 0; i < m->ncons; i++) {
        const Constraint *c = &m->cons[i];
        Violation *v = n < cap ? &out[n] : &spare;
        bool broken = c->type == C_MAX_RESTS ? judge_rests(m, c, fixed, v)
                                             : judge(m, c, fixed, known, v);
        if (!broken) continue;
        v->con = i;
        n++;
    }
    return n;
}

/* The key the violation names, else the fixed key of the note's
 * section, or -1. */
static int violation_key(const Model *m, const Violation *v, int var) {
    for (int j = 0; j < v->nvars; j++) {
        if (m->vars[v->vars[j]].kind == VAR_KEY) return v->vals[j];
    }
    int key_var = m->key[model_section_at(m, m->vars[var].index)];
    return domain_singleton(&m->initial[key_var]) ? domain_value(&m->initial[key_var]) : -1;
}

static void spell(const Model *m, int var, int key, int value, char *buf, size_t cap) {
    if (m->vars[var].kind == VAR_PITCH && value != PITCH_REST && key >= 0) {
        key_pitch_name(key, value, buf, cap);
    } else {
        value_label(m, var, value, buf, cap);
    }
}

static bool outside(const Model *m, int type, int key, int pitch) {
    if (type == C_RANGE) return pitch < m->config.range_low || pitch > m->config.range_high;
    return key >= 0 && !key_has_pitch(key, pitch);
}

/* The slot of a follower that takes an in-range, in-key melody note out
 * of the range or the key, or -1. */
static int broken_follower(const Model *m, const Constraint *c, int key, int value) {
    if (c->type != C_RANGE && c->type != C_SCALE) return -1;
    if (value == PITCH_REST || outside(m, c->type, key, value)) return -1;
    for (int k = 0; k < c->nslots; k++) {
        if (c->voice[k] <= 0) continue;
        int s = canon_sounding(&m->config, c->voice[k], value);
        if (s != SOUND_REST && outside(m, c->type, key, s)) return k;
    }
    return -1;
}

/* "A4", or "A4, sounding E6 in voice 2" when a follower breaks the rule. */
static void label(const Model *m, const Violation *v, int k, char *buf, size_t cap) {
    const Constraint *c = &m->cons[v->con];
    int var = v->vars[k];
    int key = m->vars[var].kind == VAR_PITCH ? violation_key(m, v, var) : -1;
    spell(m, var, key, v->vals[k], buf, cap);
    if (m->vars[var].kind != VAR_PITCH) return;
    int slot = broken_follower(m, c, key, v->vals[k]);
    if (slot < 0) return;
    char sound[32];
    spell(m, var, key, canon_sounding(&m->config, c->voice[slot], v->vals[k]), sound,
          sizeof(sound));
    size_t used = strlen(buf);
    snprintf(buf + used, cap - used, ", sounding %s in voice %d", sound, c->voice[slot] + 1);
}

void check_text(const Model *m, const Violation *v, char *buf, size_t cap) {
    const Constraint *c = &m->cons[v->con];
    char why[256];
    constraint_describe(m, c, why, sizeof(why));
    int w = snprintf(buf, cap, "%s: %s (", rule_name(c->rule), why);
    size_t used = w < 0 ? 0 : (size_t)w;
    for (int k = 0; k < v->nvars && used < cap; k++) {
        char name[16];
        char value[64];
        var_label(m, v->vars[k], name, sizeof(name));
        label(m, v, k, value, sizeof(value));
        w = snprintf(buf + used, cap - used, "%s%s = %s", k ? ", " : "", name, value);
        if (w < 0) break;
        used += (size_t)w;
    }
    if (used < cap) snprintf(buf + used, cap - used, ")");
}

void check_print(FILE *f, const Model *m, const Violation *v, int n) {
    for (int i = 0; i < check_stored(n); i++) {
        char text[CHECK_TEXT_MAX];
        check_text(m, &v[i], text, sizeof(text));
        fprintf(f, "violation: %s\n", text);
    }
    if (n > CHECK_MAX) fprintf(f, "violation: ... and %d more\n", n - CHECK_MAX);
}
