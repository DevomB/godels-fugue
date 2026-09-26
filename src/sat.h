#ifndef SAT_H
#define SAT_H

#include "types.h"

/* Ceiling: intended for 12 notes and domains of size <= 8.
 * If the encoding would exceed 256 bools, return -1 (caller prints
 * sat: too large and exits 2). Not a CDCL library. */
int sat_solve(const PieceConfig *config, int *melody);

#endif
