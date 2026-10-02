#include "explain.h"

#include "canon.h"
#include "theory.h"

#include <stdlib.h>
#include <string.h>

const char *explain_status_name(int status) {
    static const char *const names[] = {"config", "chosen", "forced", "locked", "open"};
    if (status < 0 || status > WHY_OPEN) return "?";
    return names[status];
}

void json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; s != NULL && *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            fputc('\\', f);
            fputc(c, f);
        } else if (c < 0x20 || c == '<') {
            fprintf(f, "\\u%04x", c);
        } else {
            fputc(c, f);
        }
    }
    fputc('"', f);
}

static int final_key(const SolverState *s, int section) {
    const Model *m = s->model;
    if (section >= m->nsections) section = 0;
    int k = solver_value(s, m->key[section]);
    return k >= 0 ? k : -1;
}

/* Like value_label, but spells pitches and names chords in the key the
 * solver settled on. */
void explain_label(const SolverState *s, int var, int value, char *buf, size_t cap) {
    const Model *m = s->model;
    const ModelVar *v = &m->vars[var];
    if (v->kind == VAR_PITCH && value != PITCH_REST) {
        int key = final_key(s, model_section_at(m, v->index));
        if (key >= 0) {
            key_pitch_name(key, value, buf, cap);
            return;
        }
    }
    if (v->kind == VAR_CHORD) {
        int key = final_key(s, model_section_at(m, v->index * 4));
        if (key >= 0) {
            degree_name(key, value, buf, cap);
            return;
        }
    }
    value_label(m, var, value, buf, cap);
}

static bool locked(const Model *m, int var) {
    for (int a = m->cons_adj_start[var]; a < m->cons_adj_start[var + 1]; a++) {
        if (m->cons[m->cons_adj[a]].type == C_LOCK) return true;
    }
    return false;
}

void explain_var(const SolverState *s, int var, Explanation *e) {
    const Model *m = s->model;
    const ProofLog *log = &s->proof;
    memset(e, 0, sizeof(*e));
    e->var = var;
    e->value = solver_value(s, var);
    e->event = -1;
    for (int i = log->event_count - 1; i >= 0; i--) {
        const ProofEvent *ev = &log->events[i];
        if (ev->variable_id != var) continue;
        if (ev->type == PROOF_DECIDE || ev->type == PROOF_FORCED) {
            e->event = i;
            break;
        }
    }
    if (e->value < 0) {
        e->status = WHY_OPEN;
        e->event = -1; /* earlier events describe values it no longer has */
    } else if (e->event < 0) {
        e->status = WHY_CONFIG;
    } else {
        const ProofEvent *ev = &log->events[e->event];
        e->level = ev->level;
        e->reason = ev->reason;
        if (ev->type == PROOF_DECIDE) {
            e->status = WHY_DECIDED;
            e->decision = ev->level;
        } else {
            e->status = locked(m, var) && !s->skip[CID_LOCK] ? WHY_LOCKED : WHY_FORCED;
        }
    }

    int values[128];
    int n = domain_collect(&m->initial[var], values);
    for (int k = 0; k < n; k++) {
        if (values[k] == e->value) continue;
        for (int i = log->event_count - 1; i >= 0; i--) {
            const ProofEvent *ev = &log->events[i];
            if (ev->type == PROOF_REMOVE && ev->variable_id == var && ev->value == values[k]) {
                e->rejected[e->nrejected].value = values[k];
                e->rejected[e->nrejected].event = i;
                e->nrejected++;
                break;
            }
        }
    }
}

static size_t append(char *buf, size_t cap, size_t used, const char *text) {
    if (used >= cap) return used;
    int w = snprintf(buf + used, cap - used, "%s", text);
    if (w < 0) return used;
    return used + (size_t)w >= cap ? cap - 1 : used + (size_t)w;
}

static size_t append_decisions(const SolverState *s, const LevelSet *reason, char *buf,
                               size_t cap, size_t used) {
    bool first = true;
    for (int l = 1; l <= s->level; l++) {
        if (!levelset_has(reason, l)) continue;
        char name[16];
        char value[32];
        char part[64];
        var_label(s->model, s->decisions[l].var, name, sizeof(name));
        explain_label(s, s->decisions[l].var, s->decisions[l].value, value, sizeof(value));
        snprintf(part, sizeof(part), "%s%s = %s", first ? "" : ", ", name, value);
        used = append(buf, cap, used, part);
        first = false;
    }
    if (first) used = append(buf, cap, used, "the rules alone");
    return used;
}

void explain_removal(const SolverState *s, const ProofEvent *ev, char *buf, size_t cap) {
    const Model *m = s->model;
    size_t used = 0;
    buf[0] = '\0';
    if (ev->rule == CID_REFUTED) {
        used = append(buf, cap, used, "search: every completion failed given ");
        append_decisions(s, &ev->reason, buf, cap, used);
        return;
    }
    char detail[160];
    if (ev->rule == CID_LEARNED) {
        snprintf(detail, sizeof(detail), "learned conflict %d", NOGOOD_INDEX(ev->constraint) + 1);
    } else if (ev->constraint >= 0 && ev->constraint < m->ncons) {
        char where[128];
        constraint_describe(m, &m->cons[ev->constraint], where, sizeof(where));
        snprintf(detail, sizeof(detail), "%s: %s", rule_name(ev->rule), where);
    } else {
        snprintf(detail, sizeof(detail), "%s", rule_name(ev->rule));
    }
    used = append(buf, cap, used, detail);
    bool first = true;
    for (int p = 0; p < ev->parent_count; p++) {
        if (ev->parent_values[p] < 0) continue;
        char name[16];
        char value[32];
        char part[64];
        var_label(m, ev->parent_vars[p], name, sizeof(name));
        explain_label(s, ev->parent_vars[p], ev->parent_values[p], value, sizeof(value));
        snprintf(part, sizeof(part), "%s%s = %s", first ? " (with " : ", ", name, value);
        used = append(buf, cap, used, part);
        first = false;
    }
    if (!first) append(buf, cap, used, ")");
}

static void print_heard(FILE *f, const SolverState *s, int index) {
    const Model *m = s->model;
    bool first = true;
    for (int v = 0; v < m->voices; v++) {
        for (int t = 0; t < m->span; t++) {
            if (m->source[v][t] != index) continue;
            fprintf(f, "%svoice %d step %d", first ? "  heard: " : ", ", v + 1, t);
            first = false;
        }
    }
    if (!first) fprintf(f, "\n");
}

static void print_candidates(FILE *f, const SolverState *s, const Explanation *e) {
    const Decision *d = &s->decisions[e->decision];
    fprintf(f, "  candidates by cost%s:\n",
            d->temperature > 0 ? " (sampled at a temperature above 0)" : "");
    int width = 6; /* key names are longer than pitches */
    for (int k = 0; k < d->ncand; k++) {
        char value[32];
        explain_label(s, e->var, d->cand_values[k], value, sizeof(value));
        if ((int)strlen(value) > width) width = (int)strlen(value);
    }
    for (int k = 0; k < d->ncand; k++) {
        char value[32];
        explain_label(s, e->var, d->cand_values[k], value, sizeof(value));
        fprintf(f, "    %-*s %3d%s", width, value, d->cand_costs[k],
                d->cand_values[k] == e->value ? "  <- chosen" : "");
        if (k < CAND_BREAKDOWN && d->cand_costs[k] > 0) {
            bool first = true;
            for (int t = 0; t < TERM_COUNT; t++) {
                if (d->cand_breakdown[k][t] == 0) continue;
                fprintf(f, "%s%s %d", first ? "  (" : ", ", term_name(t), d->cand_breakdown[k][t]);
                first = false;
            }
            if (!first) fprintf(f, ")");
        }
        fprintf(f, "\n");
    }
}

void explain_print(FILE *f, const SolverState *s, const Explanation *e) {
    const Model *m = s->model;
    const ModelVar *v = &m->vars[e->var];
    char name[16];
    char value[32];
    var_label(m, e->var, name, sizeof(name));
    if (e->value >= 0) {
        explain_label(s, e->var, e->value, value, sizeof(value));
    } else {
        snprintf(value, sizeof(value), "?");
    }
    fprintf(f, "%s = %s (%s", name, value, var_kind_name(v->kind));
    if (v->kind == VAR_PITCH || v->kind == VAR_TIE) fprintf(f, " of melody note %d", v->index);
    if (v->kind == VAR_CHORD) fprintf(f, " of bar %d", v->index + 1);
    fprintf(f, ")\n");
    if (v->kind == VAR_PITCH) print_heard(f, s, v->index);

    switch (e->status) {
    case WHY_CONFIG:
        fprintf(f, "  set by the config: no other value was ever possible\n");
        break;
    case WHY_LOCKED:
        fprintf(f, "  locked by the user\n");
        break;
    case WHY_DECIDED:
        if (s->decisions[e->decision].guided) {
            fprintf(f, "  chosen as decision %d to rebuild the lowest-energy piece the "
                       "optimizer found\n",
                    e->decision);
        } else {
            fprintf(f, "  chosen by the search as decision %d\n", e->decision);
        }
        print_candidates(f, s, e);
        break;
    case WHY_FORCED:
        fprintf(f, "  forced %s: every other value was removed\n",
                e->level == 0 ? "before any decision" : "by propagation");
        break;
    default:
        if (s->result == SOLVE_UNSAT) {
            fprintf(f, "  no value: the rules admit no piece at all\n");
        } else if (s->result == SOLVE_LIMIT) {
            fprintf(f, "  undecided: the search stopped at its limit\n");
        } else {
            fprintf(f, "  undecided\n");
        }
        break;
    }
    if (e->nrejected > 0) {
        /* values removed for the same reason share one line */
        char(*why)[EXPLAIN_TEXT_MAX] = malloc((size_t)e->nrejected * sizeof(*why));
        bool printed[128] = {false};
        if (why == NULL) return;
        fprintf(f, "  %s:\n", e->status == WHY_DECIDED ? "removed before the choice" : "removed");
        for (int k = 0; k < e->nrejected; k++)
            explain_removal(s, &s->proof.events[e->rejected[k].event], why[k], sizeof(why[k]));
        for (int k = 0; k < e->nrejected; k++) {
            if (printed[k]) continue;
            int width = 0;
            fprintf(f, "    ");
            for (int j = k; j < e->nrejected; j++) {
                if (printed[j] || strcmp(why[j], why[k]) != 0) continue;
                char rv[32];
                explain_label(s, e->var, e->rejected[j].value, rv, sizeof(rv));
                width += fprintf(f, "%s%s", width ? " " : "", rv);
                printed[j] = true;
            }
            fprintf(f, "%*s %s\n", width < 6 ? 6 - width : 0, "", why[k]);
        }
        free(why);
    }
    if (e->status == WHY_FORCED) {
        char deps[EXPLAIN_TEXT_MAX];
        append_decisions(s, &e->reason, deps, sizeof(deps), 0);
        fprintf(f, "  depends on: %s\n", deps);
    }
}

void explain_json(FILE *f, const SolverState *s, const Explanation *e) {
    const Model *m = s->model;
    char buf[EXPLAIN_TEXT_MAX];
    var_label(m, e->var, buf, sizeof(buf));
    fprintf(f, "{\"var\":");
    json_string(f, buf);
    fprintf(f, ",\"id\":%d,\"kind\":\"%s\",\"index\":%d,\"value\":%d,\"label\":", e->var,
            var_kind_name(m->vars[e->var].kind), m->vars[e->var].index, e->value);
    if (e->value >= 0) {
        explain_label(s, e->var, e->value, buf, sizeof(buf));
    } else {
        snprintf(buf, sizeof(buf), "?");
    }
    json_string(f, buf);
    fprintf(f, ",\"status\":\"%s\",\"event\":%d,\"depth\":%d,\"optimized\":%s",
            explain_status_name(e->status), e->event, e->level,
            e->status == WHY_DECIDED && s->decisions[e->decision].guided ? "true" : "false");

    fprintf(f, ",\"rejected\":[");
    for (int k = 0; k < e->nrejected; k++) {
        const ProofEvent *ev = &s->proof.events[e->rejected[k].event];
        explain_label(s, e->var, e->rejected[k].value, buf, sizeof(buf));
        fprintf(f, "%s{\"value\":%d,\"label\":", k ? "," : "", e->rejected[k].value);
        json_string(f, buf);
        fprintf(f, ",\"rule\":");
        json_string(f, rule_name(ev->rule));
        explain_removal(s, ev, buf, sizeof(buf));
        fprintf(f, ",\"why\":");
        json_string(f, buf);
        fprintf(f, ",\"event\":%d}", e->rejected[k].event);
    }
    fprintf(f, "]");

    fprintf(f, ",\"depends\":[");
    bool first = true;
    if (e->status == WHY_FORCED || e->status == WHY_DECIDED) {
        for (int l = 1; l <= s->level; l++) {
            if (!levelset_has(&e->reason, l)) continue;
            if (e->status == WHY_DECIDED && l == e->decision) continue; /* itself */
            char name[16];
            var_label(m, s->decisions[l].var, name, sizeof(name));
            explain_label(s, s->decisions[l].var, s->decisions[l].value, buf, sizeof(buf));
            fprintf(f, "%s{\"decision\":%d,\"var\":", first ? "" : ",", l);
            json_string(f, name);
            fprintf(f, ",\"id\":%d,\"label\":", s->decisions[l].var);
            json_string(f, buf);
            fprintf(f, "}");
            first = false;
        }
    }
    fprintf(f, "]");

    fprintf(f, ",\"candidates\":[");
    if (e->status == WHY_DECIDED) {
        const Decision *d = &s->decisions[e->decision];
        for (int k = 0; k < d->ncand; k++) {
            explain_label(s, e->var, d->cand_values[k], buf, sizeof(buf));
            fprintf(f, "%s{\"value\":%d,\"label\":", k ? "," : "", d->cand_values[k]);
            json_string(f, buf);
            fprintf(f, ",\"cost\":%d,\"parts\":{", d->cand_costs[k]);
            if (k < CAND_BREAKDOWN) {
                bool firstpart = true;
                for (int t = 0; t < TERM_COUNT; t++) {
                    if (d->cand_breakdown[k][t] == 0) continue;
                    fprintf(f, "%s", firstpart ? "" : ",");
                    json_string(f, term_name(t));
                    fprintf(f, ":%d", d->cand_breakdown[k][t]);
                    firstpart = false;
                }
            }
            fprintf(f, "}}");
        }
    }
    fprintf(f, "]}");
}
