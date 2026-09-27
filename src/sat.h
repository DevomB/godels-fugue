#ifndef SAT_H
#define SAT_H

#include "model.h"

enum { SAT_UNSAT = 0, SAT_SAT = 1, SAT_TOO_LARGE = -1, SAT_LIMIT = -2 };

/* Encodes the model's hard constraints as CNF (one boolean per variable
 * value; each constraint forbids the value tuples its predicate rejects)
 * and runs DPLL with unit propagation. Soft terms are ignored.
 * values receives one value per model variable on SAT_SAT.
 * max_decisions <= 0 means no limit. */
int sat_solve(const Model *m, int *values, long max_decisions);

#endif
