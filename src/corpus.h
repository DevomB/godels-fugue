#ifndef CORPUS_H
#define CORPUS_H

/* A corpus file is whitespace-separated MIDI pitches. Counting pitch
 * classes over a directory of them suggests a cost for each class:
 * common classes are cheap, rare ones cost up to `scale`. */
int corpus_row_counts(const char *path, int counts[12]);
int corpus_dir_counts(const char *dir, int counts[12]);
void corpus_weights_from_counts(const int counts[12], int scale, int weights[12]);

#endif
