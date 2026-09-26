#ifndef EXPORT_H
#define EXPORT_H

#include "proof.h"

#include <stdbool.h>

bool export_musicxml(const char *path, const int *const *lines, int n_voices,
                     int length, const int *durations);
bool export_contour(const char *path, const int *melody, int length);
bool export_wav(const char *path, const int *const *lines, int n_voices,
                int length, const int *durations);
bool export_trace(const char *path, const ProofLog *log);

#endif
