#include "domain.h"

static uint64_t bit_mask(int pitch) {
    return (uint64_t)1 << (pitch & 63);
}

static int popcount(uint64_t word) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(word);
#else
    int count = 0;
    while (word != 0) {
        word &= word - 1;
        count++;
    }
    return count;
#endif
}

/* word must be nonzero */
static int lowest_bit(uint64_t word) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(word);
#else
    int bit = 0;
    while ((word & 1) == 0) {
        word >>= 1;
        bit++;
    }
    return bit;
#endif
}

void domain_clear(MidiDomain *d) {
    d->bits[0] = 0;
    d->bits[1] = 0;
}

void domain_add(MidiDomain *d, int pitch) {
    if (pitch < 0 || pitch > 127) return;
    d->bits[pitch >> 6] |= bit_mask(pitch);
}

void domain_fill_range(MidiDomain *d, int low, int high) {
    domain_clear(d);
    if (low < 0) low = 0;
    if (high > 127) high = 127;
    for (int pitch = low; pitch <= high; pitch++) domain_add(d, pitch);
}

bool domain_contains(const MidiDomain *d, int pitch) {
    if (pitch < 0 || pitch > 127) return false;
    return (d->bits[pitch >> 6] & bit_mask(pitch)) != 0;
}

void domain_remove(MidiDomain *d, int pitch) {
    if (pitch < 0 || pitch > 127) return;
    d->bits[pitch >> 6] &= ~bit_mask(pitch);
}

int domain_count(const MidiDomain *d) {
    return popcount(d->bits[0]) + popcount(d->bits[1]);
}

bool domain_singleton(const MidiDomain *d) {
    return domain_count(d) == 1;
}

/* Next legal pitch that is >= n, or -1 if none remain. */
int domain_next(const MidiDomain *d, int n) {
    if (n < 0) n = 0;
    for (int w = n >> 6; w < 2; w++) {
        uint64_t word = d->bits[w];
        if (w == n >> 6) word &= ~(uint64_t)0 << (n & 63);
        if (word != 0) return w * 64 + lowest_bit(word);
    }
    return -1;
}

int domain_value(const MidiDomain *d) {
    if (!domain_singleton(d)) return -1;
    return domain_next(d, 0);
}

int domain_collect(const MidiDomain *d, int *out) {
    int n = 0;
    for (int p = domain_next(d, 0); p >= 0; p = domain_next(d, p + 1))
        out[n++] = p;
    return n;
}

bool domain_equal(const MidiDomain *a, const MidiDomain *b) {
    return a->bits[0] == b->bits[0] && a->bits[1] == b->bits[1];
}
