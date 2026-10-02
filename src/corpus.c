#include "corpus.h"

#include "midi_read.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#endif

/* Counts every whitespace-separated MIDI pitch; other words are skipped. */
int corpus_row_counts(const char *path, int counts[12]) {
    if (path == NULL || counts == NULL) return 0;
    FILE *f = fopen(path, "r");
    if (f == NULL) return 0;
    char word[64];
    int n = 0;
    while (fscanf(f, "%63s", word) == 1) {
        char *end = NULL;
        long pitch = strtol(word, &end, 10);
        if (end == word || *end != '\0' || pitch < 0 || pitch > 127) continue;
        counts[pitch % 12] += 1;
        n++;
    }
    fclose(f);
    return n;
}

/* Counts every note of every track; a file that fails to read counts nothing. */
int corpus_midi_counts(const char *path, int counts[12]) {
    if (path == NULL || counts == NULL) return 0;
    MidiFile file;
    char err[300];
    if (!midi_read_file(&file, path, err, sizeof(err))) return 0;
    int n = 0;
    for (int k = 0; k < file.ntracks; k++) {
        const MidiTrack *t = &file.tracks[k];
        for (int i = 0; i < t->count; i++) counts[t->notes[i].pitch % 12] += 1;
        n += t->count;
    }
    midi_file_free(&file);
    return n;
}

/* .mid or .midi, in any case. */
static bool is_midi_name(const char *name) {
    const char *dot = strrchr(name, '.');
    if (dot == NULL || strlen(dot) > 5) return false;
    char ext[6];
    size_t i = 0;
    for (; dot[i] != '\0'; i++) ext[i] = (char)tolower((unsigned char)dot[i]);
    ext[i] = '\0';
    return strcmp(ext, ".mid") == 0 || strcmp(ext, ".midi") == 0;
}

static int count_entry(const char *dir, const char *name, int counts[12]) {
    char path[512];
    if (name[0] == '.') return 0;
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path))
        return 0;
    int n = is_midi_name(name) ? corpus_midi_counts(path, counts)
                               : corpus_row_counts(path, counts);
    return n > 0;
}

/* Returns the number of files that held at least one pitch. */
int corpus_dir_counts(const char *dir, int counts[12]) {
    if (dir == NULL || counts == NULL) return 0;
    int files = 0;
#ifdef _WIN32
    char pattern[512];
    if (snprintf(pattern, sizeof(pattern), "%s/*", dir) >= (int)sizeof(pattern))
        return 0;
    struct _finddata_t entry;
    intptr_t handle = _findfirst(pattern, &entry);
    if (handle == -1) return 0;
    do {
        if (entry.attrib & _A_SUBDIR) continue;
        files += count_entry(dir, entry.name, counts);
    } while (_findnext(handle, &entry) == 0);
    _findclose(handle);
#else
    DIR *d = opendir(dir);
    if (d == NULL) return 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) files += count_entry(dir, ent->d_name, counts);
    closedir(d);
#endif
    return files;
}

/* Scale so the rarest class costs `scale` and the most common costs 0,
 * whatever the corpus size. */
void corpus_weights_from_counts(const int counts[12], int scale, int weights[12]) {
    int maxc = 0;
    for (int i = 0; i < 12; i++) {
        if (counts[i] > maxc) maxc = counts[i];
    }
    for (int i = 0; i < 12; i++) {
        long long gap = (long long)(maxc - counts[i]) * scale;
        weights[i] = maxc == 0 ? 0 : (int)((gap + maxc / 2) / maxc);
    }
}
