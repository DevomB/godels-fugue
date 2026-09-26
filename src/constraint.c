#include "constraint.h"

#include "canon.h"
#include "theory.h"

#include <stdlib.h>

enum {
    CID_SCALE = 1,
    CID_RANGE = 2,
    CID_LEAP = 3,
    CID_SECOND = 4,
    CID_PARALLEL_FIFTH = 5,
    CID_PARALLEL_OCTAVE = 6
};

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
                               int constraint_id, const char *message) {
    domain_remove(&s->domains[variable], pitch);
    proof_append_removal(&s->proof, variable, pitch, constraint_id, message);
    if (domain_count(&s->domains[variable]) == 0)
        return mark_failed(s, variable);
    return true;
}

static bool revise_scale(SolverState *s) {
    int length = s->config.length;
    int invert = s->config.invert;
    int axis = s->config.axis;
    for (int i = 0; i < length; i++) {
        int pitches[128];
        int n = collect_pitches(&s->domains[i], pitches);
        for (int k = 0; k < n; k++) {
            int pitch = pitches[k];
            if (pitch_in_c_major(pitch) &&
                (invert == 0 || pitch_in_c_major(invert_pitch(axis, pitch))))
                continue;
            if (!remove_unsupported(s, i, pitch, CID_SCALE, "scale"))
                return false;
        }
    }
    return true;
}

static bool revise_range(SolverState *s) {
    int length = s->config.length;
    int low = s->config.range_low;
    int high = s->config.range_high;
    int invert = s->config.invert;
    int axis = s->config.axis;
    for (int i = 0; i < length; i++) {
        int pitches[128];
        int n = collect_pitches(&s->domains[i], pitches);
        for (int k = 0; k < n; k++) {
            int pitch = pitches[k];
            if (pitch >= low && pitch <= high) {
                if (invert == 0) continue;
                int mirror = invert_pitch(axis, pitch);
                if (mirror >= low && mirror <= high) continue;
            }
            if (!remove_unsupported(s, i, pitch, CID_RANGE, "range"))
                return false;
        }
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
    int max_leap = s->config.max_leap;
    int pitches[128];
    int n = collect_pitches(&s->domains[a], pitches);
    for (int k = 0; k < n; k++) {
        int pitch = pitches[k];
        if (leap_supported(&s->domains[b], pitch, max_leap)) continue;
        if (!remove_unsupported(s, a, pitch, CID_LEAP, "melodic leap"))
            return false;
    }
    n = collect_pitches(&s->domains[b], pitches);
    for (int k = 0; k < n; k++) {
        int pitch = pitches[k];
        if (leap_supported(&s->domains[a], pitch, max_leap)) continue;
        if (!remove_unsupported(s, b, pitch, CID_LEAP, "melodic leap"))
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

static int sounding_pitch(int pitch, int invert_voice, int axis) {
    return invert_voice ? invert_pitch(axis, pitch) : pitch;
}

static int voice_inverts(const PieceConfig *c, int voice) {
    return c->invert != 0 && voice > 0;
}

static bool second_supported_pair(const MidiDomain *partner, int pitch,
                                  int invert_self, int invert_other, int axis) {
    int self = sounding_pitch(pitch, invert_self, axis);
    for (int q = domain_next(partner, 0); q >= 0; q = domain_next(partner, q + 1)) {
        if (!is_second(self, sounding_pitch(q, invert_other, axis))) return true;
    }
    return false;
}

static bool revise_second_pair(SolverState *s, int a, int b, int invert_a,
                              int invert_b) {
    int axis = s->config.axis;
    int pitches[128];
    int n = collect_pitches(&s->domains[a], pitches);
    for (int k = 0; k < n; k++) {
        int pitch = pitches[k];
        if (second_supported_pair(&s->domains[b], pitch, invert_a, invert_b, axis))
            continue;
        if (!remove_unsupported(s, a, pitch, CID_SECOND, "second"))
            return false;
    }
    n = collect_pitches(&s->domains[b], pitches);
    for (int k = 0; k < n; k++) {
        int pitch = pitches[k];
        if (second_supported_pair(&s->domains[a], pitch, invert_b, invert_a, axis))
            continue;
        if (!remove_unsupported(s, b, pitch, CID_SECOND, "second"))
            return false;
    }
    return true;
}

static bool revise_second(SolverState *s) {
    int length = s->config.length;
    int delay = s->config.delay;
    int voices = s->config.voices;
    if (voices < 2) voices = 2;
    if (voices > VOICE_MAX) voices = VOICE_MAX;
    int span = canon_span_voices(length, delay, voices);
    for (int t = 0; t < span; t++) {
        if (t % 4 != 0) continue;
        for (int va = 0; va < voices; va++) {
            for (int vb = va + 1; vb < voices; vb++) {
                int i0 = canon_source_index(va, t, delay, length, s->config.retrograde);
                int i1 = canon_source_index(vb, t, delay, length, s->config.retrograde);
                if (i0 < 0 || i1 < 0) continue;
                if (!revise_second_pair(s, i0, i1, voice_inverts(&s->config, va),
                                       voice_inverts(&s->config, vb)))
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
                               int fixed_pitch, bool fifth, int invert_lead,
                               int invert_follow, int axis) {
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
                int v0_prev = sounding_pitch(values[0], invert_lead, axis);
                int v1_prev = sounding_pitch(values[2], invert_follow, axis);
                int v0_now = sounding_pitch(values[1], invert_lead, axis);
                int v1_now = sounding_pitch(values[3], invert_follow, axis);
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
                                  int invert_lead, int invert_follow,
                                  int constraint_id, const char *message) {
    const MidiDomain *domains[4] = {
        &s->domains[idx[0]],
        &s->domains[idx[1]],
        &s->domains[idx[2]],
        &s->domains[idx[3]],
    };
    int axis = s->config.axis;

    for (int slot = 0; slot < 4; slot++) {
        int variable = idx[slot];
        int candidates[128];
        int n = collect_pitches(&s->domains[variable], candidates);
        for (int k = 0; k < n; k++) {
            int pitch = candidates[k];
            if (parallel_supported(domains, slot, pitch, fifth, invert_lead,
                                  invert_follow, axis))
                continue;
            if (!remove_unsupported(s, variable, pitch, constraint_id, message))
                return false;
        }
    }
    return true;
}

static bool revise_parallel(SolverState *s, bool fifth, int constraint_id,
                            const char *message) {
    int length = s->config.length;
    int delay = s->config.delay;
    int voices = s->config.voices;
    if (voices < 2) voices = 2;
    if (voices > VOICE_MAX) voices = VOICE_MAX;
    int span = canon_span_voices(length, delay, voices);
    for (int t = 0; t + 1 < span; t++) {
        for (int va = 0; va < voices; va++) {
            for (int vb = va + 1; vb < voices; vb++) {
                int idx[4];
                idx[0] = canon_source_index(va, t, delay, length,
                                           s->config.retrograde);
                idx[1] = canon_source_index(va, t + 1, delay, length,
                                           s->config.retrograde);
                idx[2] = canon_source_index(vb, t, delay, length,
                                           s->config.retrograde);
                idx[3] = canon_source_index(vb, t + 1, delay, length,
                                           s->config.retrograde);
                if (idx[0] < 0 || idx[1] < 0 || idx[2] < 0 || idx[3] < 0)
                    continue;
                if (!indexes_distinct(idx[0], idx[1], idx[2], idx[3]))
                    continue;
                if (!revise_parallel_tuple(s, idx, fifth,
                                          voice_inverts(&s->config, va),
                                          voice_inverts(&s->config, vb),
                                          constraint_id, message))
                    return false;
            }
        }
    }
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
    return true;
}
