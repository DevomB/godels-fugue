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

static int collect_pitches(const MidiDomain *d, int *out) {
    int n = 0;
    for (int p = domain_next(d, 0); p >= 0; p = domain_next(d, p + 1))
        out[n++] = p;
    return n;
}

static bool mark_failed(SolverState *s, int variable) {
    s->failed = true;
    s->failed_variable = variable;
    return false;
}

static bool remove_unsupported(SolverState *s, int variable, int pitch,
                               int constraint_id, const char *message,
                               const int *related, int related_count) {
    solver_trail_push(s, variable);
    domain_remove(&s->domains[variable], pitch);
    proof_append_removal_deps(&s->proof, variable, pitch, constraint_id, message,
                              related, related_count, s->domains);
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
    int n = collect_pitches(&s->domains[i], pitches);
    for (int k = 0; k < n; k++) {
        int pitch = pitches[k];
        if (pitch_in_c_major(pitch) &&
            pitch_in_c_major(canon_sounding(&s->config, 1, pitch)))
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
    int n = collect_pitches(&s->domains[i], pitches);
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
    int n = collect_pitches(&s->domains[a], pitches);
    for (int k = 0; k < n; k++) {
        int pitch = pitches[k];
        if (leap_supported(&s->domains[b], pitch, max_leap)) continue;
        if (!remove_unsupported(s, a, pitch, CID_LEAP, "melodic leap", &b, 1))
            return false;
    }
    n = collect_pitches(&s->domains[b], pitches);
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
    int n = collect_pitches(&s->domains[a], pitches);
    for (int k = 0; k < n; k++) {
        int pitch = pitches[k];
        if (second_supported_pair(&s->domains[b], pitch, voice_a, voice_b, &s->config))
            continue;
        if (!remove_unsupported(s, a, pitch, CID_SECOND, "second", &b, 1))
            return false;
    }
    n = collect_pitches(&s->domains[b], pitches);
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

/* Support search is cheap under 13 pitches; add an AC-3 queue when a profile
 * says revise dominates. Domains stay <= 8 so 8^3 is fine. */
static bool parallel_supported(const MidiDomain *domains[4], int fixed_slot,
                               int fixed_pitch, bool fifth, int voice_lead,
                               int voice_follow, const PieceConfig *config) {
    int pitches[4][8];
    int counts[4];
    for (int i = 0; i < 4; i++) {
        counts[i] = 0;
        for (int p = domain_next(domains[i], 0); p >= 0;
             p = domain_next(domains[i], p + 1)) {
            if (counts[i] < 8) pitches[i][counts[i]] = p;
            counts[i]++;
        }
        if (counts[i] > 8) counts[i] = 8;
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
        int n = collect_pitches(&s->domains[variable], candidates);
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

static bool index_in_tuple(const int *idx, int n, int variable) {
    for (int i = 0; i < n; i++) {
        if (idx[i] == variable) return true;
    }
    return false;
}

static bool revise_second_touching(SolverState *s, int variable) {
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
                if (i0 != variable && i1 != variable) continue;
                if (!revise_second_pair(s, i0, i1, va, vb))
                    return false;
            }
        }
    }
    return true;
}

static bool revise_parallel_touching(SolverState *s, int variable, bool fifth,
                                     int constraint_id, const char *message) {
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
                if (!index_in_tuple(idx, 4, variable)) continue;
                if (!revise_parallel_tuple(s, idx, fifth, va, vb, constraint_id,
                                          message))
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

static int voice_count(const PieceConfig *config) {
    int voices = config->voices;
    if (voices < 1) voices = 1;
    if (voices > VOICE_MAX) voices = VOICE_MAX;
    return voices;
}

static bool revise_harmony_slot(SolverState *s, int idx, int voice, int cid,
                                const char *message, int (*ok)(int)) {
    int pitches[128];
    int n = collect_pitches(&s->domains[idx], pitches);
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
    int span = canon_span_config(&s->config);
    int voices = voice_count(&s->config);
    for (int t = 0; t < span; t += 4) {
        for (int v = 0; v < voices; v++) {
            if (canon_map_source(&s->config, v, t) != i) continue;
            if (!revise_chord_at(s, i, v)) return false;
        }
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
