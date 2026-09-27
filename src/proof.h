#ifndef PROOF_H
#define PROOF_H

#include "domain.h"
#include "types.h"

#include <stdbool.h>
#include <stdint.h>

/* A set of decision levels (1-based). Every variable carries the set of
 * decisions its current domain depends on; conflicts are unions of them. */
typedef struct LevelSet {
    uint64_t bits[(VAR_MAX + 64) / 64];
} LevelSet;

void levelset_clear(LevelSet *s);
void levelset_add(LevelSet *s, int level);
void levelset_remove(LevelSet *s, int level);
bool levelset_has(const LevelSet *s, int level);
void levelset_union(LevelSet *s, const LevelSet *other);
int levelset_max(const LevelSet *s); /* 0 when empty */
int levelset_count(const LevelSet *s);

enum {
    PROOF_REMOVE, /* a value left a domain */
    PROOF_DECIDE, /* the search chose a value */
    PROOF_FORCED  /* removals left one value */
};

enum { PROOF_PARENT_MAX = 8, PROOF_MESSAGE_MAX = 40 };

typedef struct ProofEvent {
    int type;
    int variable_id;
    int value;      /* removed, chosen or forced value */
    int rule;       /* CID_* of a removal */
    int constraint; /* model constraint index, or -1 */
    int level;      /* decisions on the search path when recorded */
    LevelSet reason;
    int parent_count;
    int parent_events[PROOF_PARENT_MAX]; /* prior event, or -1 */
    int parent_vars[PROOF_PARENT_MAX];
    int parent_values[PROOF_PARENT_MAX]; /* value if the parent was collapsed, else -1 */
    char message[PROOF_MESSAGE_MAX];
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
/* Appends a zeroed event and returns it, or NULL when out of memory. */
ProofEvent *proof_append(ProofLog *log, int type, int variable_id, int value);
void proof_add_parent(ProofEvent *event, int parent_event, int parent_var,
                      int parent_value);
bool proof_append_entropy(ProofLog *log, double bits);
double entropy_bits(const MidiDomain *domains, int count);

#endif
