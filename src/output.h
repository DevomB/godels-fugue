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
bool output_write_all(Run *run, const OutputPaths *paths);
/* False, with a message, when two output files would share a path. */
bool output_paths_distinct(const OutputPaths *paths, char *err, size_t cap);
bool output_write_report(const char *path, const Run *run);
bool output_write_explanations(const char *path, const Run *run);

void output_print_summary(FILE *out, const Run *run);
void output_print_failure(FILE *err, const Run *run);
void output_print_counterfactual(FILE *out, const Run *run);

/* "s", "e", "e.", "q", "q.", "h", "h.", "w" for a note value of 1, 2, 3, 4,
 * 6, 8, 12 or 16 sixteenths (score_written_steps). */
const char *output_value_name(int sixteenths);

#endif
