#ifndef PROOF_H
#define PROOF_H

#include "domain.h"

#include <stdbool.h>

typedef struct ProofEvent {
    int variable_id;
    int removed_pitch;
    int constraint_id;
    char message[160];
} ProofEvent;

typedef struct EntropySample {
    int after_event;
    double bits;
} EntropySample;

typedef struct ProofLog {
    ProofEvent *events;
    int event_count;
    int event_capacity;
    EntropySample *samples;
    int sample_count;
    int sample_capacity;
} ProofLog;

typedef struct ProofMark {
    int events;
    int samples;
} ProofMark;

void proof_init(ProofLog *log);
void proof_free(ProofLog *log);
ProofMark proof_mark(const ProofLog *log);
void proof_truncate(ProofLog *log, ProofMark mark);
bool proof_append_removal(ProofLog *log, int variable_id, int removed_pitch,
                          int constraint_id, const char *message);
bool proof_append_entropy(ProofLog *log, double bits);
bool proof_write(const ProofLog *log, const char *path);
double entropy_bits(const MidiDomain *domains, int count);

#endif
