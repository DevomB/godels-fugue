#include "proof.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static bool grow_events(ProofLog *log) {
    if (log->event_count < log->event_capacity) return true;
    int new_cap = log->event_capacity == 0 ? 32 : log->event_capacity * 2;
    ProofEvent *grown = realloc(log->events, (size_t)new_cap * sizeof(*grown));
    if (!grown) return false;
    log->events = grown;
    log->event_capacity = new_cap;
    return true;
}

static bool grow_samples(ProofLog *log) {
    if (log->sample_count < log->sample_capacity) return true;
    int new_cap = log->sample_capacity == 0 ? 32 : log->sample_capacity * 2;
    EntropySample *grown = realloc(log->samples, (size_t)new_cap * sizeof(*grown));
    if (!grown) return false;
    log->samples = grown;
    log->sample_capacity = new_cap;
    return true;
}

static void copy_message(char dest[160], const char *message) {
    if (!message) {
        dest[0] = '\0';
        return;
    }
    size_t i = 0;
    for (; i < 159 && message[i] != '\0'; i++) dest[i] = message[i];
    dest[i] = '\0';
}

bool proof_append_removal(ProofLog *log, int variable_id, int removed_pitch,
                          int constraint_id, const char *message) {
    if (!grow_events(log)) return false;
    ProofEvent *ev = &log->events[log->event_count];
    ev->variable_id = variable_id;
    ev->removed_pitch = removed_pitch;
    ev->constraint_id = constraint_id;
    copy_message(ev->message, message);
    log->event_count++;
    return true;
}

bool proof_append_entropy(ProofLog *log, double bits) {
    if (!grow_samples(log)) return false;
    EntropySample *sample = &log->samples[log->sample_count];
    sample->after_event = log->event_count;
    sample->bits = bits;
    log->sample_count++;
    return true;
}

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

bool proof_write(const ProofLog *log, const char *path) {
    FILE *fp = fopen(path, "w");
    if (!fp) return false;

    int ei = 0;
    int si = 0;
    while (ei < log->event_count || si < log->sample_count) {
        if (si < log->sample_count && log->samples[si].after_event == ei) {
            fprintf(fp, "entropy %.6f\n", log->samples[si].bits);
            si++;
        } else if (ei < log->event_count) {
            const ProofEvent *ev = &log->events[ei];
            fprintf(fp, "variable %d pitch %d %s\n", ev->variable_id, ev->removed_pitch,
                    ev->message);
            ei++;
        } else {
            /* Remaining samples whose after_event is past written events. */
            fprintf(fp, "entropy %.6f\n", log->samples[si].bits);
            si++;
        }
    }

    fclose(fp);
    return true;
}
