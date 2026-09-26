#ifndef EXPORT_H
#define EXPORT_H

#include "proof.h"
#include "types.h"

#include <stdbool.h>

bool export_musicxml(const char *path, const int *const *lines, int n_voices,
                     int length, const int *durations);
bool export_contour(const char *path, const int *melody, int length);
bool export_wav(const char *path, const int *const *lines, int n_voices,
                int length, const int *durations);
bool export_trace(const char *path, const ProofLog *log);
bool export_score_page(const char *path, const int *melody,
                       const PieceConfig *config, const SolverState *state,
                       int backtracks, int energy);
bool export_report(const char *path, const PieceConfig *config, int backtracks,
                   double entropy, int energy, const int *core, int core_n);

#endif
