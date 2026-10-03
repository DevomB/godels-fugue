#ifndef CHECK_H
#define CHECK_H

#include "model.h"

#include <stdio.h>

/* CHECK_TEXT_MAX fits the rest limit naming a melody of MELODY_MAX rests. */
enum { CHECK_MAX = 64, CHECK_TEXT_MAX = 1024 };

/* A hard rule broken by values the config fixes before any search. */
typedef struct Violation {
    int con; /* index into model->cons */
    int nvars;
    int vars[MELODY_MAX]; /* the fixed variables only */
    int vals[MELODY_MAX];
} Violation;

/* Judges every hard constraint on a variable fixed before the search
 * (given notes, locks, rest_at, a fixed key): it is broken when no values
 * of its free variables, each from its initial domain, satisfy it. A
 * constraint with too many free values to try is skipped. Stores the
 * first cap broken ones and returns how many there are in all. */
int check_given(const Model *m, Violation *out, int cap);
/* How many of check_given's n violations a CHECK_MAX array holds. */
static inline int check_stored(int n) { return n < CHECK_MAX ? n : CHECK_MAX; }
/* "scale: notes stay in the key (x1 = C#4, key = C major)" */
void check_text(const Model *m, const Violation *v, char *buf, size_t cap);
/* One "violation: ..." line per stored violation, then "... and N more"
 * past CHECK_MAX; n is check_given's count. */
void check_print(FILE *f, const Model *m, const Violation *v, int n);

#endif
