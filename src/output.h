#ifndef OUTPUT_H
#define OUTPUT_H

#include "run.h"

#include <stdbool.h>
#include <stdio.h>

typedef struct OutputPaths {
    const char *midi;    /* the other score files go beside it */
    const char *proof;   /* proof.dag and proof.json go beside it */
    const char *entropy;
} OutputPaths;

/* Writes every output file the run produced. Returns false if any
 * write failed. */
bool output_write_all(const Run *run, const OutputPaths *paths);
bool output_write_report(const char *path, const Run *run);
bool output_write_explanations(const char *path, const Run *run);

void output_print_summary(FILE *out, const Run *run);
void output_print_failure(FILE *err, const Run *run);
void output_print_counterfactual(FILE *out, const Run *run);

/* "q", "h", "h.", "w" for a note of 1-4 steps. */
const char *output_length_name(int steps);

#endif
