#include "constraint.h"

#include "canon.h"
#include "theory.h"

#include <stdlib.h>

const char *constraint_name(int cid) {
    static const char *names[] = {"",
                                  "scale",
                                  "range",
                                  "leap",
                                  "second",
                                  "parallel fifth",
                                  "parallel octave",
                                  "chord",
                                  "cadence"};
    if (cid <= 0 || cid >= CID_MAX) return "unknown";
    return names[cid];
}

static int skipped(const SolverState *s, int cid) {
    return cid > 0 && cid < 16 && s->skip_cid[cid];
}

static bool mark_failed(SolverState *s, int variable) {
    s->failed = true;
    s->failed_variable = variable;
    return false;
}

static bool remove_unsupported(SolverState *s, int variable, int pitch,
                               int constraint_id, const char *message,
                               const int *related, int related_count) {
    int self = variable;
    const int *deps = related;
    int dep_n = related_count;
    if (dep_n == 0 && domain_singleton(&s->domains[variable]) &&
        domain_value(&s->domains[variable]) == pitch) {
        deps = &self;
        dep_n = 1;
    }
    solver_trail_push(s, variable);
    domain_remove(&s->domains[variable], pitch);
    proof_append_removal_deps(&s->proof, variable, pitch, constraint_id, message,
                              deps, dep_n, s->domains);
    solver_enqueue(s, variable);
    for (int i = 0; i < related_count; i++) {
        solver_enqueue(s, related[i]);
    }
    if (domain_count(&s->domains[variable]) == 0)
        return mark_failed(s, variable);
    return true;
}

static bool revise_scale_one(SolverState *s, int i) {
    if (skipped(s, CID_SCALE)) return true;
    int pitches[128];
    int n = domain_collect(&s->domains[i], pitches);
    for (int k = 0; k < n; k++) {
        int pitch = pitches[k];
        if (pitch_in_scale(pitch, i, s->config.modulate_at) &&
            pitch_in_scale(canon_sounding(&s->config, 1, pitch), i,
                           s->config.modulate_at))
            continue;
        if (!remove_unsupported(s, i, pitch, CID_SCALE, "scale", NULL, 0))
            return false;
    }
    return true;
}

static bool revise_scale(SolverState *s) {
    int length = s->config.length;
    for (int i = 0; i < length; i++) {
        if (!revise_scale_one(s, i)) return false;
    }
    return true;
}

static bool revise_range_one(SolverState *s, int i) {
    if (skipped(s, CID_RANGE)) return true;
    int low = s->config.range_low;
    int high = s->config.range_high;
    int pitches[128];
    int n = domain_collect(&s->domains[i], pitches);
    for (int k = 0; k < n; k++) {
        int pitch = pitches[k];
        int sound = canon_sounding(&s->config, 1, pitch);
        if (pitch >= low && pitch <= high && sound >= low && sound <= high)
            continue;
        if (!remove_unsupported(s, i, pitch, CID_RANGE, "range", NULL, 0))
            return false;
    }
    return true;
}

static bool revise_range(SolverState *s) {
    int length = s->config.length;
    for (int i = 0; i < length; i++) {
        if (!revise_range_one(s, i)) return false;
    }
    return true;
}

static bool leap_supported(const MidiDomain *partner, int pitch, int max_leap) {
    for (int q = domain_next(partner, 0); q >= 0; q = domain_next(partner, q + 1)) {
        if (abs(pitch - q) <= max_leap) return true;
    }
    return false;
}

static bool revise_leap_edge(SolverState *s, int a, int b) {
    if (skipped(s, CID_LEAP)) return true;
    int max_leap = s->config.max_leap;
    int pitches[128];
    int n = domain_collect(&s->domains[a], pitches);
    for (int k = 0; k < n; k++) {
        int pitch = pitches[k];
        if (leap_supported(&s->domains[b], pitch, max_leap)) continue;
        if (!remove_unsupported(s, a, pitch, CID_LEAP, "melodic leap", &b, 1))
            return false;
    }
    n = domain_collect(&s->domains[b], pitches);
    for (int k = 0; k < n; k++) {
        int pitch = pitches[k];
        if (leap_supported(&s->domains[a], pitch, max_leap)) continue;
        if (!remove_unsupported(s, b, pitch, CID_LEAP, "melodic leap", &a, 1))
            return false;
    }
    return true;
}

static bool revise_leap(SolverState *s) {
    int length = s->config.length;
    for (int i = 0; i < length - 1; i++) {
        if (!revise_leap_edge(s, i, i + 1)) return false;
    }
    return true;
}

static bool second_supported_pair(const MidiDomain *partner, int pitch, int voice_self,
                                  int voice_other, const PieceConfig *config) {
    int self = canon_sounding(config, voice_self, pitch);
    for (int q = domain_next(partner, 0); q >= 0; q = domain_next(partner, q + 1)) {
        if (!is_second(self, canon_sounding(config, voice_other, q))) return true;
    }
    return false;
}

static bool revise_second_pair(SolverState *s, int a, int b, int voice_a, int voice_b) {
    if (skipped(s, CID_SECOND)) return true;
    int pitches[128];
    int n = domain_collect(&s->domains[a], pitches);
    for (int k = 0; k < n; k++) {
        int pitch = pitches[k];
        if (second_supported_pair(&s->domains[b], pitch, voice_a, voice_b, &s->config))
            continue;
        if (!remove_unsupported(s, a, pitch, CID_SECOND, "second", &b, 1))
            return false;
    }
    n = domain_collect(&s->domains[b], pitches);
    for (int k = 0; k < n; k++) {
        int pitch = pitches[k];
        if (second_supported_pair(&s->domains[a], pitch, voice_b, voice_a, &s->config))
            continue;
        if (!remove_unsupported(s, b, pitch, CID_SECOND, "second", &a, 1))
            return false;
    }
    return true;
}

static bool revise_second(SolverState *s) {
    int voices = s->config.voices;
    if (voices < 2) voices = 2;
    if (voices > VOICE_MAX) voices = VOICE_MAX;
    int span = canon_span_config(&s->config);
    for (int t = 0; t < span; t++) {
        if (t % 4 != 0) continue;
        for (int va = 0; va < voices; va++) {
            for (int vb = va + 1; vb < voices; vb++) {
                int i0 = canon_map_source(&s->config, va, t);
                int i1 = canon_map_source(&s->config, vb, t);
                if (i0 < 0 || i1 < 0) continue;
                if (!revise_second_pair(s, i0, i1, va, vb))
                    return false;
            }
        }
    }
    return true;
}

static bool indexes_distinct(int a, int b, int c, int d) {
    return a != b && a != c && a != d && b != c && b != d && c != d;
}

/* Ceiling: 128^3 support walk if a domain is full MIDI. Default C4–C5
 * C major stays 8^3. Stop as soon as one legal tuple exists. */
static bool parallel_supported(const MidiDomain *domains[4], int fixed_slot,
                               int fixed_pitch, bool fifth, int voice_lead,
                               int voice_follow, const PieceConfig *config) {
    int pitches[4][128];
    int counts[4];
    for (int i = 0; i < 4; i++) {
        counts[i] = domain_collect(domains[i], pitches[i]);
        if (i != fixed_slot && counts[i] == 0) return false;
    }

    int slots[3];
    int n_free = 0;
    for (int i = 0; i < 4; i++) {
        if (i == fixed_slot) continue;
        slots[n_free++] = i;
    }

    int s0 = slots[0], s1 = slots[1], s2 = slots[2];
    for (int a = 0; a < counts[s0]; a++) {
        for (int b = 0; b < counts[s1]; b++) {
            for (int c = 0; c < counts[s2]; c++) {
                int values[4];
                values[fixed_slot] = fixed_pitch;
                values[s0] = pitches[s0][a];
                values[s1] = pitches[s1][b];
                values[s2] = pitches[s2][c];
                int v0_prev = canon_sounding(config, voice_lead, values[0]);
                int v1_prev = canon_sounding(config, voice_follow, values[2]);
                int v0_now = canon_sounding(config, voice_lead, values[1]);
                int v1_now = canon_sounding(config, voice_follow, values[3]);
                bool parallel = fifth
                    ? is_parallel_fifth(v0_prev, v1_prev, v0_now, v1_now)
                    : is_parallel_octave(v0_prev, v1_prev, v0_now, v1_now);
                if (!parallel) return true;
            }
        }
    }
    return false;
}

static bool revise_parallel_tuple(SolverState *s, int idx[4], bool fifth,
                                  int voice_lead, int voice_follow,
                                  int constraint_id, const char *message) {
    const MidiDomain *domains[4] = {
        &s->domains[idx[0]],
        &s->domains[idx[1]],
        &s->domains[idx[2]],
        &s->domains[idx[3]],
    };

    for (int slot = 0; slot < 4; slot++) {
        int variable = idx[slot];
        int candidates[128];
        int n = domain_collect(&s->domains[variable], candidates);
        for (int k = 0; k < n; k++) {
            int pitch = candidates[k];
            if (parallel_supported(domains, slot, pitch, fifth, voice_lead,
                                  voice_follow, &s->config))
                continue;
            int related[3];
            int nrel = 0;
            for (int r = 0; r < 4; r++) {
                if (idx[r] != variable) related[nrel++] = idx[r];
            }
            if (!remove_unsupported(s, variable, pitch, constraint_id, message,
                                    related, nrel))
                return false;
        }
    }
    return true;
}

static bool revise_parallel(SolverState *s, bool fifth, int constraint_id,
                            const char *message) {
    if (skipped(s, constraint_id)) return true;
    int voices = s->config.voices;
    if (voices < 2) voices = 2;
    if (voices > VOICE_MAX) voices = VOICE_MAX;
    int span = canon_span_config(&s->config);
    for (int t = 0; t + 1 < span; t++) {
        for (int va = 0; va < voices; va++) {
            for (int vb = va + 1; vb < voices; vb++) {
                int idx[4];
                idx[0] = canon_map_source(&s->config, va, t);
                idx[1] = canon_map_source(&s->config, va, t + 1);
                idx[2] = canon_map_source(&s->config, vb, t);
                idx[3] = canon_map_source(&s->config, vb, t + 1);
                if (idx[0] < 0 || idx[1] < 0 || idx[2] < 0 || idx[3] < 0)
                    continue;
                if (!indexes_distinct(idx[0], idx[1], idx[2], idx[3]))
                    continue;
                if (!revise_parallel_tuple(s, idx, fifth, va, vb, constraint_id,
                                          message))
                    return false;
            }
        }
    }
    return true;
}

static int voice_count(const PieceConfig *config) {
    int voices = config->voices;
    if (voices < 1) voices = 1;
    if (voices > VOICE_MAX) voices = VOICE_MAX;
    return voices;
}

/* Invert the canon map when delay is a plain offset. Augment, diminish,
 * and cyclic still walk the span because one source can occupy many times. */
static int source_hits(const PieceConfig *c, int source, int *ts, int *vs,
                       int cap) {
    int n = 0;
    int voices = voice_count(c);
    if (c->augment >= 2 || c->diminish >= 2 || c->cyclic) {
        int span = canon_span_config(c);
        for (int t = 0; t < span && n < cap; t++) {
            for (int v = 0; v < voices && n < cap; v++) {
                if (canon_map_source(c, v, t) == source) {
                    ts[n] = t;
                    vs[n] = v;
                    n++;
                }
            }
        }
        return n;
    }
    for (int v = 0; v < voices && n < cap; v++) {
        int raw = (v > 0 && c->retrograde) ? c->length - 1 - source : source;
        int t = raw + canon_voice_delay(c, v);
        if (canon_map_source(c, v, t) != source) continue;
        ts[n] = t;
        vs[n] = v;
        n++;
    }
    return n;
}

static bool revise_second_touching(SolverState *s, int variable) {
    int ts[SPAN_MAX];
    int vs[SPAN_MAX];
    int n = source_hits(&s->config, variable, ts, vs, SPAN_MAX);
    int voices = voice_count(&s->config);
    if (voices < 2) return true;
    for (int k = 0; k < n; k++) {
        int t = ts[k];
        if (t % 4 != 0) continue;
        int va = vs[k];
        for (int vb = 0; vb < voices; vb++) {
            if (vb == va) continue;
            int other = canon_map_source(&s->config, vb, t);
            if (other < 0) continue;
            int a = variable;
            int b = other;
            int voice_a = va;
            int voice_b = vb;
            if (va > vb) {
                a = other;
                b = variable;
                voice_a = vb;
                voice_b = va;
            }
            if (!revise_second_pair(s, a, b, voice_a, voice_b))
                return false;
        }
    }
    return true;
}

static bool revise_parallel_touching(SolverState *s, int variable, bool fifth,
                                     int constraint_id, const char *message) {
    if (skipped(s, constraint_id)) return true;
    int ts[SPAN_MAX];
    int vs[SPAN_MAX];
    int n = source_hits(&s->config, variable, ts, vs, SPAN_MAX);
    int voices = voice_count(&s->config);
    int span = canon_span_config(&s->config);
    if (voices < 2) return true;
    for (int k = 0; k < n; k++) {
        int hit = ts[k];
        int va = vs[k];
        for (int step = 0; step < 2; step++) {
            int t = hit - (1 - step);
            if (t < 0 || t + 1 >= span) continue;
            for (int vb = 0; vb < voices; vb++) {
                if (vb == va) continue;
                int lead = va < vb ? va : vb;
                int follow = va < vb ? vb : va;
                int idx[4];
                idx[0] = canon_map_source(&s->config, lead, t);
                idx[1] = canon_map_source(&s->config, lead, t + 1);
                idx[2] = canon_map_source(&s->config, follow, t);
                idx[3] = canon_map_source(&s->config, follow, t + 1);
                if (idx[0] < 0 || idx[1] < 0 || idx[2] < 0 || idx[3] < 0)
                    continue;
                if (!indexes_distinct(idx[0], idx[1], idx[2], idx[3]))
                    continue;
                if (!revise_parallel_tuple(s, idx, fifth, lead, follow,
                                          constraint_id, message))
                    return false;
            }
        }
    }
    return true;
}

static int last_strong_time(const PieceConfig *config) {
    int span = canon_span_config(config);
    int t = ((span - 1) / 4) * 4;
    return t < 0 ? 0 : t;
}

static bool revise_harmony_slot(SolverState *s, int idx, int voice, int cid,
                                const char *message, int (*ok)(int)) {
    int pitches[128];
    int n = domain_collect(&s->domains[idx], pitches);
    for (int k = 0; k < n; k++) {
        if (ok(canon_sounding(&s->config, voice, pitches[k]))) continue;
        if (!remove_unsupported(s, idx, pitches[k], cid, message, NULL, 0))
            return false;
    }
    return true;
}

static bool revise_chord_at(SolverState *s, int idx, int voice) {
    return revise_harmony_slot(s, idx, voice, CID_CHORD, "chord", in_c_triad);
}

static bool revise_cadence_at(SolverState *s, int idx, int voice) {
    return revise_harmony_slot(s, idx, voice, CID_CADENCE, "cadence",
                               in_c_dominant);
}

static bool revise_chord_one(SolverState *s, int i) {
    if (skipped(s, CID_CHORD) || s->config.strong_chord == 0) return true;
    int ts[SPAN_MAX];
    int vs[SPAN_MAX];
    int n = source_hits(&s->config, i, ts, vs, SPAN_MAX);
    for (int k = 0; k < n; k++) {
        if (ts[k] % 4 != 0) continue;
        if (!revise_chord_at(s, i, vs[k])) return false;
    }
    return true;
}

static bool revise_cadence_one(SolverState *s, int i) {
    if (skipped(s, CID_CADENCE) || s->config.cadence == 0) return true;
    int t = last_strong_time(&s->config);
    int voices = voice_count(&s->config);
    for (int v = 0; v < voices; v++) {
        if (canon_map_source(&s->config, v, t) != i) continue;
        if (!revise_cadence_at(s, i, v)) return false;
    }
    return true;
}

static bool revise_chord(SolverState *s) {
    if (skipped(s, CID_CHORD) || s->config.strong_chord == 0) return true;
    int span = canon_span_config(&s->config);
    int voices = voice_count(&s->config);
    for (int t = 0; t < span; t += 4) {
        for (int v = 0; v < voices; v++) {
            int idx = canon_map_source(&s->config, v, t);
            if (idx < 0) continue;
            if (!revise_chord_at(s, idx, v)) return false;
        }
    }
    return true;
}

static bool revise_cadence(SolverState *s) {
    if (skipped(s, CID_CADENCE) || s->config.cadence == 0) return true;
    int t = last_strong_time(&s->config);
    int voices = voice_count(&s->config);
    for (int v = 0; v < voices; v++) {
        int idx = canon_map_source(&s->config, v, t);
        if (idx < 0) continue;
        if (!revise_cadence_at(s, idx, v)) return false;
    }
    return true;
}

bool constraints_revise_var(SolverState *s, int variable) {
    if (variable < 0 || variable >= s->config.length) return true;
    if (!revise_scale_one(s, variable)) return false;
    if (!revise_range_one(s, variable)) return false;
    if (!revise_chord_one(s, variable)) return false;
    if (!revise_cadence_one(s, variable)) return false;
    if (variable > 0 && !revise_leap_edge(s, variable - 1, variable))
        return false;
    if (variable + 1 < s->config.length &&
        !revise_leap_edge(s, variable, variable + 1))
        return false;
    if (!revise_second_touching(s, variable)) return false;
    if (!revise_parallel_touching(s, variable, true, CID_PARALLEL_FIFTH,
                                  "parallel fifth"))
        return false;
    if (!revise_parallel_touching(s, variable, false, CID_PARALLEL_OCTAVE,
                                  "parallel octave"))
        return false;
    return true;
}

bool constraints_revise(SolverState *s) {
    if (!revise_scale(s)) return false;
    if (!revise_range(s)) return false;
    if (!revise_leap(s)) return false;
    if (!revise_second(s)) return false;
    if (!revise_parallel(s, true, CID_PARALLEL_FIFTH, "parallel fifth"))
        return false;
    if (!revise_parallel(s, false, CID_PARALLEL_OCTAVE, "parallel octave"))
        return false;
    if (!revise_chord(s)) return false;
    if (!revise_cadence(s)) return false;
    return true;
}
