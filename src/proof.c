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

static int last_event_for(const ProofLog *log, int variable_id) {
    for (int i = log->event_count - 1; i >= 0; i--) {
        if (log->events[i].variable_id == variable_id) return i;
    }
    return -1;
}

bool proof_append_removal_deps(ProofLog *log, int variable_id, int removed_pitch,
                               int constraint_id, const char *message,
                               const int *related_vars, int related_count,
                               const MidiDomain *domains) {
    if (!grow_events(log)) return false;
    ProofEvent *ev = &log->events[log->event_count];
    ev->variable_id = variable_id;
    ev->removed_pitch = removed_pitch;
    ev->constraint_id = constraint_id;
    ev->parent_count = 0;
    copy_message(ev->message, message);

    for (int i = 0; i < related_count && ev->parent_count < PROOF_PARENT_MAX; i++) {
        int other = related_vars[i];
        if (other < 0 || other == variable_id) continue;
        int prior = last_event_for(log, other);
        if (prior >= 0) {
            ev->parent_events[ev->parent_count] = prior;
            ev->parent_vars[ev->parent_count] = other;
            ev->parent_pitches[ev->parent_count] =
                (domains != NULL && domain_singleton(&domains[other]))
                    ? domain_value(&domains[other])
                    : -1;
            ev->parent_count++;
            continue;
        }
        if (domains != NULL && domain_singleton(&domains[other])) {
            ev->parent_events[ev->parent_count] = -1;
            ev->parent_vars[ev->parent_count] = other;
            ev->parent_pitches[ev->parent_count] = domain_value(&domains[other]);
            ev->parent_count++;
        }
    }

    log->event_count++;
    return true;
}

bool proof_append_removal(ProofLog *log, int variable_id, int removed_pitch,
                          int constraint_id, const char *message) {
    return proof_append_removal_deps(log, variable_id, removed_pitch, constraint_id,
                                     message, NULL, 0, NULL);
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

bool proof_write_dag(const ProofLog *log, const char *path) {
    FILE *fp = fopen(path, "w");
    if (!fp) return false;

    for (int i = 0; i < log->event_count; i++) {
        const ProofEvent *ev = &log->events[i];
        fprintf(fp, "event %d variable %d pitch %d %s\n", i, ev->variable_id,
                ev->removed_pitch, ev->message);
        for (int p = 0; p < ev->parent_count; p++) {
            if (ev->parent_events[p] >= 0) {
                fprintf(fp, "  parent event %d\n", ev->parent_events[p]);
            } else {
                fprintf(fp, "  parent assign %d=%d\n", ev->parent_vars[p],
                        ev->parent_pitches[p]);
            }
        }
    }

    fclose(fp);
    return true;
}
