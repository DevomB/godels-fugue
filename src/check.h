#ifndef CHECK_H
#define CHECK_H

#include "model.h"

#include <stdio.h>

enum { CHECK_MAX = 64, CHECK_TEXT_MAX = 512 };

/* A hard rule broken by values the config fixes before any search. */
typedef struct Violation {
    int con; /* index into model->cons */
    int nvars;
    int vars[MELODY_MAX];
    int vals[MELODY_MAX];
} Violation;

/* Judges every hard constraint whose variables are all fixed before the
 * search (given notes, locks, a fixed key) and stores the broken ones.
 * A constraint touching any free variable is skipped. Returns the count
 * stored, at most cap. */
int check_given(const Model *m, Violation *out, int cap);
/* "scale: notes stay in the key (x1 = C#4, key = C major)" */
void check_text(const Model *m, const Violation *v, char *buf, size_t cap);
/* One "violation: ..." line per violation. */
void check_print(FILE *f, const Model *m, const Violation *v, int n);

#endif
