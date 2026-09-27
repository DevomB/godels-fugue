#ifndef DOMAIN_H
#define DOMAIN_H

#include <stdbool.h>
#include <stdint.h>

typedef struct MidiDomain {
    uint64_t bits[2];
} MidiDomain;

void domain_clear(MidiDomain *d);
void domain_add(MidiDomain *d, int pitch);
void domain_fill_range(MidiDomain *d, int low, int high);
bool domain_contains(const MidiDomain *d, int pitch);
void domain_remove(MidiDomain *d, int pitch);
int domain_count(const MidiDomain *d);
bool domain_singleton(const MidiDomain *d);
int domain_value(const MidiDomain *d);
int domain_next(const MidiDomain *d, int n); /* next pitch >= n, or -1 */
int domain_collect(const MidiDomain *d, int *out);
bool domain_equal(const MidiDomain *a, const MidiDomain *b);
int domain_rank(const MidiDomain *d, int value); /* members below value */

#endif
