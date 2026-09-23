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

static bool second_supported(const MidiDomain *partner, int pitch) {
    for (int q = domain_next(partner, 0); q >= 0; q = domain_next(partner, q + 1)) {
        if (!is_second(pitch, q)) return true;
    }
    return false;
}

static bool second_supported_lead_invert(const MidiDomain *follower, int pitch,
                                         int axis) {
    for (int q = domain_next(follower, 0); q >= 0; q = domain_next(follower, q + 1)) {
        if (!is_second(pitch, invert_pitch(axis, q))) return true;
    }
    return false;
}

static bool second_supported_follower_invert(const MidiDomain *lead, int pitch,
                                             int axis) {
    for (int p = domain_next(lead, 0); p >= 0; p = domain_next(lead, p + 1)) {
        if (!is_second(p, invert_pitch(axis, pitch))) return true;
    }
    return false;
}

static bool revise_second_pair(SolverState *s, int a, int b) {
    int invert = s->config.invert;
    int axis = s->config.axis;
    int pitches[128];
    int n = collect_pitches(&s->domains[a], pitches);
    for (int k = 0; k < n; k++) {
        int pitch = pitches[k];
        bool ok = invert == 0
            ? second_supported(&s->domains[b], pitch)
            : second_supported_lead_invert(&s->domains[b], pitch, axis);
        if (ok) continue;
        if (!remove_unsupported(s, a, pitch, CID_SECOND, "second"))
            return false;
    }
    n = collect_pitches(&s->domains[b], pitches);
    for (int k = 0; k < n; k++) {
        int pitch = pitches[k];
        bool ok = invert == 0
            ? second_supported(&s->domains[a], pitch)
            : second_supported_follower_invert(&s->domains[a], pitch, axis);
        if (ok) continue;
        if (!remove_unsupported(s, b, pitch, CID_SECOND, "second"))
            return false;
    }
    return true;
}

static bool revise_second(SolverState *s) {
    int length = s->config.length;
    int delay = s->config.delay;
    int span = canon_span(length, delay);
    for (int t = 0; t < span; t++) {
        if (t % 4 != 0) continue;
        int i0 = canon_source_index(0, t, delay, length, s->config.retrograde);
        int i1 = canon_source_index(1, t, delay, length, s->config.retrograde);
        if (i0 < 0 || i1 < 0) continue;
        if (!revise_second_pair(s, i0, i1)) return false;
    }
    return true;
}

static bool indexes_distinct(int a, int b, int c, int d) {
    return a != b && a != c && a != d && b != c && b != d && c != d;
}

/* Support search is cheap under 13 pitches; add an AC-3 queue when a profile
 * says revise dominates. Domains stay <= 8 so 8^3 is fine. */
static bool parallel_supported(const MidiDomain *domains[4], int fixed_slot,
                               int fixed_pitch, bool fifth, int invert,
                               int axis) {
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
                int v0_prev = values[0];
                int v1_prev = values[2];
                int v0_now = values[1];
                int v1_now = values[3];
                if (invert == 1) {
                    v1_prev = invert_pitch(axis, v1_prev);
                    v1_now = invert_pitch(axis, v1_now);
                }
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
                                  int constraint_id, const char *message) {
    const MidiDomain *domains[4] = {
        &s->domains[idx[0]],
        &s->domains[idx[1]],
        &s->domains[idx[2]],
        &s->domains[idx[3]],
    };
    int invert = s->config.invert;
    int axis = s->config.axis;

    for (int slot = 0; slot < 4; slot++) {
        int variable = idx[slot];
        int candidates[128];
        int n = collect_pitches(&s->domains[variable], candidates);
        for (int k = 0; k < n; k++) {
            int pitch = candidates[k];
            if (parallel_supported(domains, slot, pitch, fifth, invert, axis))
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
    int span = canon_span(length, delay);
    for (int t = 0; t + 1 < span; t++) {
        int idx[4];
        idx[0] = canon_source_index(0, t, delay, length, s->config.retrograde);
        idx[1] = canon_source_index(0, t + 1, delay, length, s->config.retrograde);
        idx[2] = canon_source_index(1, t, delay, length, s->config.retrograde);
        idx[3] = canon_source_index(1, t + 1, delay, length, s->config.retrograde);
        if (idx[0] < 0 || idx[1] < 0 || idx[2] < 0 || idx[3] < 0) continue;
        if (!indexes_distinct(idx[0], idx[1], idx[2], idx[3])) continue;
        if (!revise_parallel_tuple(s, idx, fifth, constraint_id, message))
            return false;
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
