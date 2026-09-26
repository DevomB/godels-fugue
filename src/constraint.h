#ifndef CONSTRAINT_H
#define CONSTRAINT_H

#include "types.h"

#include <stdbool.h>

bool constraints_revise(SolverState *s);
bool constraints_revise_var(SolverState *s, int variable);

#endif
