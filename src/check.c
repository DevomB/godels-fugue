#include "check.h"

#include "theory.h"

#include <string.h>

/* The value a variable has before any search, or -1 if it is free. A
 * lock gives the value even when the domain still holds others. */
static int fixed_value(const Model *m, int var) {
    for (int a = m->cons_adj_start[var]; a < m->cons_adj_start[var + 1]; a++) {
        const Constraint *c = &m->cons[m->cons_adj[a]];
        if (c->type == C_LOCK) return c->param;
    }
    if (domain_singleton(&m->initial[var])) return domain_value(&m->initial[var]);
    return -1;
}

static bool judge(const Model *m, const Constraint *c, const int *fixed, Violation *v) {
    int vals[SCOPE_MAX];
    for (int k = 0; k < c->n; k++) {
        vals[k] = fixed[c->vars[k]];
        if (vals[k] < 0) return false;
    }
    if (constraint_holds(m, c, vals)) return false;
    v->nvars = c->n;
    for (int k = 0; k < c->n; k++) {
        v->vars[k] = c->vars[k];
        v->vals[k] = vals[k];
    }
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
    for (int var = 0; var < m->nvars; var++) fixed[var] = fixed_value(m, var);
    int n = 0;
    for (int i = 0; i < m->ncons && n < cap; i++) {
        const Constraint *c = &m->cons[i];
        Violation *v = &out[n];
        bool broken = c->type == C_MAX_RESTS ? judge_rests(m, c, fixed, v)
                                             : judge(m, c, fixed, v);
        if (!broken) continue;
        v->con = i;
        n++;
    }
    return n;
}

/* Spells a pitch in the key the violation names, else in its section's
 * key when that is fixed. */
static void label(const Model *m, const Violation *v, int k, char *buf, size_t cap) {
    int var = v->vars[k];
    const ModelVar *mv = &m->vars[var];
    if (mv->kind == VAR_PITCH && v->vals[k] != PITCH_REST) {
        int key = -1;
        for (int j = 0; j < v->nvars && key < 0; j++) {
            if (m->vars[v->vars[j]].kind == VAR_KEY) key = v->vals[j];
        }
        int key_var = m->key[model_section_at(m, mv->index)];
        if (key < 0 && domain_singleton(&m->initial[key_var])) {
            key = domain_value(&m->initial[key_var]);
        }
        if (key >= 0) {
            key_pitch_name(key, v->vals[k], buf, cap);
            return;
        }
    }
    value_label(m, var, v->vals[k], buf, cap);
}

void check_text(const Model *m, const Violation *v, char *buf, size_t cap) {
    const Constraint *c = &m->cons[v->con];
    char why[256];
    constraint_describe(m, c, why, sizeof(why));
    int w = snprintf(buf, cap, "%s: %s (", rule_name(c->rule), why);
    size_t used = w < 0 ? 0 : (size_t)w;
    for (int k = 0; k < v->nvars && used < cap; k++) {
        char name[16];
        char value[32];
        var_label(m, v->vars[k], name, sizeof(name));
        label(m, v, k, value, sizeof(value));
        w = snprintf(buf + used, cap - used, "%s%s = %s", k ? ", " : "", name, value);
        if (w < 0) break;
        used += (size_t)w;
    }
    if (used < cap) snprintf(buf + used, cap - used, ")");
}

void check_print(FILE *f, const Model *m, const Violation *v, int n) {
    for (int i = 0; i < n; i++) {
        char text[CHECK_TEXT_MAX];
        check_text(m, &v[i], text, sizeof(text));
        fprintf(f, "violation: %s\n", text);
    }
}
