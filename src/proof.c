#include "proof.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { LEVEL_WORDS = (VAR_MAX + 64) / 64 };

void levelset_clear(LevelSet *s) {
    memset(s, 0, sizeof(*s));
}

void levelset_add(LevelSet *s, int level) {
    if (level <= 0 || level >= LEVEL_WORDS * 64) return;
    s->bits[level >> 6] |= (uint64_t)1 << (level & 63);
}

void levelset_remove(LevelSet *s, int level) {
    if (level <= 0 || level >= LEVEL_WORDS * 64) return;
    s->bits[level >> 6] &= ~((uint64_t)1 << (level & 63));
}

bool levelset_has(const LevelSet *s, int level) {
    if (level <= 0 || level >= LEVEL_WORDS * 64) return false;
    return (s->bits[level >> 6] >> (level & 63)) & 1;
}

void levelset_union(LevelSet *s, const LevelSet *other) {
    for (int w = 0; w < LEVEL_WORDS; w++) s->bits[w] |= other->bits[w];
}

int levelset_max(const LevelSet *s) {
    for (int w = LEVEL_WORDS - 1; w >= 0; w--) {
        uint64_t word = s->bits[w];
        if (word == 0) continue;
        int bit = 63;
        while (((word >> bit) & 1) == 0) bit--;
        return w * 64 + bit;
    }
    return 0;
}

int levelset_count(const LevelSet *s) {
    int n = 0;
    for (int w = 0; w < LEVEL_WORDS; w++) {
        for (uint64_t word = s->bits[w]; word != 0; word &= word - 1) n++;
    }
    return n;
}

void proof_init(ProofLog *log) {
    memset(log, 0, sizeof(*log));
}

void proof_free(ProofLog *log) {
    free(log->events);
    free(log->samples);
    memset(log, 0, sizeof(*log));
}

ProofMark proof_mark(const ProofLog *log) {
    ProofMark mark;
    mark.events = log->event_count;
    mark.samples = log->sample_count;
    return mark;
}

void proof_truncate(ProofLog *log, ProofMark mark) {
    if (mark.events < 0) mark.events = 0;
    if (mark.samples < 0) mark.samples = 0;
    if (mark.events < log->event_count) log->event_count = mark.events;
    if (mark.samples < log->sample_count) log->sample_count = mark.samples;
}

ProofEvent *proof_append(ProofLog *log, int type, int variable_id, int value) {
    if (log->event_count == log->event_capacity) {
        int cap = log->event_capacity == 0 ? 256 : log->event_capacity * 2;
        ProofEvent *grown = realloc(log->events, (size_t)cap * sizeof(*grown));
        if (grown == NULL) return NULL;
        log->events = grown;
        log->event_capacity = cap;
    }
    ProofEvent *ev = &log->events[log->event_count++];
    memset(ev, 0, sizeof(*ev));
    ev->type = type;
    ev->variable_id = variable_id;
    ev->value = value;
    ev->constraint = -1;
    return ev;
}

void proof_add_parent(ProofEvent *event, int parent_event, int parent_var,
                      int parent_value) {
    if (event->parent_count >= PROOF_PARENT_MAX) return;
    event->parent_events[event->parent_count] = parent_event;
    event->parent_vars[event->parent_count] = parent_var;
    event->parent_values[event->parent_count] = parent_value;
    event->parent_count++;
}

bool proof_append_entropy(ProofLog *log, double bits) {
    if (log->sample_count == log->sample_capacity) {
        int cap = log->sample_capacity == 0 ? 64 : log->sample_capacity * 2;
        EntropySample *grown = realloc(log->samples, (size_t)cap * sizeof(*grown));
        if (grown == NULL) return false;
        log->samples = grown;
        log->sample_capacity = cap;
    }
    EntropySample *sample = &log->samples[log->sample_count++];
    sample->after_event = log->event_count;
    sample->bits = bits;
    return true;
}

/* Exact for powers of two, which a C library's log2 need not be. */
static double size_entropy(int n) {
    if (n <= 1) return 0.0;
    if ((n & (n - 1)) == 0) {
        int bits = 0;
        while (n > 1) {
            n >>= 1;
            bits++;
        }
        return (double)bits;
    }
    return log2((double)n);
}

double entropy_bits(const MidiDomain *domains, int count) {
    double total = 0.0;
    for (int i = 0; i < count; i++) total += size_entropy(domain_count(&domains[i]));
    return total;
}
