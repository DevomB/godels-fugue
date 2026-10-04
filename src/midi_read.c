#include "midi_read.h"

#include "types.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PERCUSSION = 9 }; /* channel 10, counting from 1 */

/* Later ticks are refused, so a tick fits in a long. */
#define TICK_LIMIT 0x3FFFFFFFUL

static unsigned long read_be(const unsigned char *p, int n) {
    unsigned long v = 0;
    for (int i = 0; i < n; i++) v = (v << 8) | p[i];
    return v;
}

/* A variable-length quantity: seven bits a byte, at most four bytes. */
static bool read_vlq(const unsigned char *data, size_t end, size_t *pos, unsigned long *out,
                     const char **problem) {
    unsigned long v = 0;
    for (int i = 0; i < 4; i++) {
        if (*pos >= end) {
            *problem = "truncated event";
            return false;
        }
        unsigned char b = data[(*pos)++];
        v = (v << 7) | (b & 0x7Fu);
        if (!(b & 0x80)) {
            *out = v;
            return true;
        }
    }
    *problem = "variable-length number longer than four bytes";
    return false;
}

static bool add_note(MidiTrack *t, long on, int pitch, int channel) {
    if (t->count == t->capacity) {
        int capacity = t->capacity > 0 ? 2 * t->capacity : 64;
        MidiNote *grown = realloc(t->notes, (size_t)capacity * sizeof(MidiNote));
        if (grown == NULL) return false;
        t->notes = grown;
        t->capacity = capacity;
    }
    MidiNote *note = &t->notes[t->count++];
    note->on = on;
    note->off = -1;
    note->pitch = pitch;
    note->channel = channel;
    return true;
}

/* Ends the oldest of the *sounding notes of a pitch and channel, the one at
 * *oldest, and moves *oldest to the next of them. */
static void end_oldest(MidiTrack *t, int *oldest, int *sounding, long tick) {
    MidiNote *ended = &t->notes[*oldest];
    ended->off = tick;
    if (--*sounding == 0) return;
    for (int k = *oldest + 1; k < t->count; k++) {
        if (t->notes[k].pitch == ended->pitch && t->notes[k].channel == ended->channel) {
            *oldest = k;
            return;
        }
    }
}

/* Parses the events in data[pos..end). Meta and sysex events are skipped
 * and, as most readers allow, leave running status in force. */
static bool parse_track(MidiTrack *t, int number, const unsigned char *data, size_t pos,
                        size_t end, char *err, size_t cap) {
    /* The notes sounding on each channel and pitch, first in first out: the
     * oldest, and how many. Ended notes always started before sounding ones,
     * so the next of the pitch and channel after the oldest is the next to end. */
    int oldest[16][128];
    int sounding[16][128];
    memset(sounding, 0, sizeof(sounding));
    unsigned long tick = 0;
    unsigned char running = 0;
    const char *problem = NULL;
    size_t at = pos;
    while (pos < end) {
        at = pos;
        unsigned long delta = 0;
        if (!read_vlq(data, end, &pos, &delta, &problem)) break;
        tick += delta;
        if (tick > TICK_LIMIT) {
            problem = "time past 2^30 ticks";
            break;
        }
        if (pos >= end) {
            problem = "truncated event";
            break;
        }
        unsigned char status = running;
        if (data[pos] & 0x80) status = data[pos++];
        if (status == 0xFF || status == 0xF0 || status == 0xF7) {
            unsigned char type = 0;
            if (status == 0xFF) {
                if (pos >= end) {
                    problem = "truncated event";
                    break;
                }
                type = data[pos++];
            }
            unsigned long length = 0;
            if (!read_vlq(data, end, &pos, &length, &problem)) break;
            if (length > end - pos) {
                problem = "truncated event";
                break;
            }
            pos += (size_t)length;
            if (status == 0xFF && type == 0x2F) break; /* end of track */
            continue;
        }
        if (status < 0x80) {
            problem = "data byte without a status";
            break;
        }
        if (status >= 0xF0) {
            problem = "system message inside a track";
            break;
        }
        running = status;
        unsigned char kind = (unsigned char)(status & 0xF0);
        int count = kind == 0xC0 || kind == 0xD0 ? 1 : 2;
        if ((size_t)count > end - pos) {
            problem = "truncated event";
            break;
        }
        int key = data[pos];
        int velocity = count == 2 ? data[pos + 1] : 0;
        pos += (size_t)count;
        if ((key | velocity) & 0x80) {
            problem = "data byte above 127";
            break;
        }
        int channel = status & 0x0F;
        if ((kind != 0x80 && kind != 0x90) || channel == PERCUSSION) continue;
        if (kind == 0x90 && velocity > 0) {
            if (!add_note(t, (long)tick, key, channel)) {
                snprintf(err, cap, "out of memory");
                return false;
            }
            if (sounding[channel][key]++ == 0) oldest[channel][key] = t->count - 1;
        } else if (sounding[channel][key] > 0) {
            end_oldest(t, &oldest[channel][key], &sounding[channel][key], (long)tick);
        }
    }
    if (problem != NULL) {
        snprintf(err, cap, "track %d: %s at byte %lu", number, problem, (unsigned long)at);
        return false;
    }
    for (int k = 0; k < t->count; k++) {
        if (t->notes[k].off < 0) t->notes[k].off = (long)tick;
    }
    return true;
}

void midi_file_free(MidiFile *file) {
    if (file == NULL) return;
    for (int k = 0; k < file->ntracks; k++) free(file->tracks[k].notes);
    free(file->tracks);
    memset(file, 0, sizeof(*file));
}

bool midi_read_bytes(MidiFile *file, const unsigned char *data, size_t size, char *err,
                     size_t cap) {
    memset(file, 0, sizeof(*file));
    if (data == NULL || size < 14 || memcmp(data, "MThd", 4) != 0) {
        snprintf(err, cap, "not a Standard MIDI file (no MThd header)");
        return false;
    }
    unsigned long header = read_be(data + 4, 4);
    unsigned long format = read_be(data + 8, 2);
    unsigned long ntracks = read_be(data + 10, 2);
    unsigned long division = read_be(data + 12, 2);
    if (header < 6 || header > size - 8) {
        snprintf(err, cap, "bad header length %lu", header);
        return false;
    }
    if (format > 2) {
        snprintf(err, cap, "unknown MIDI format %lu", format);
        return false;
    }
    if (division & 0x8000) {
        snprintf(err, cap, "SMPTE time is not supported, only ticks per quarter note");
        return false;
    }
    if (division == 0) {
        snprintf(err, cap, "zero ticks per quarter note");
        return false;
    }
    if (ntracks == 0) {
        snprintf(err, cap, "no tracks");
        return false;
    }
    size_t pos = 8 + (size_t)header;
    /* each track needs an eight-byte chunk header at least */
    if (ntracks > (size - pos) / 8) {
        snprintf(err, cap, "%lu tracks in the header, room for %lu in the file", ntracks,
                 (unsigned long)((size - pos) / 8));
        return false;
    }
    file->tracks = calloc((size_t)ntracks, sizeof(MidiTrack));
    if (file->tracks == NULL) {
        snprintf(err, cap, "out of memory");
        return false;
    }
    file->format = (int)format;
    file->ppq = (int)division;
    while ((unsigned long)file->ntracks < ntracks) {
        if (size - pos < 8) {
            snprintf(err, cap, "the file ends before track %d", file->ntracks + 1);
            midi_file_free(file);
            return false;
        }
        unsigned long length = read_be(data + pos + 4, 4);
        size_t body = pos + 8;
        if (length > size - body) {
            snprintf(err, cap, "chunk at byte %lu runs past the end of the file",
                     (unsigned long)pos);
            midi_file_free(file);
            return false;
        }
        /* chunks of other types are skipped, as the standard asks */
        if (memcmp(data + pos, "MTrk", 4) == 0) {
            MidiTrack *t = &file->tracks[file->ntracks++];
            if (!parse_track(t, file->ntracks, data, body, body + (size_t)length, err, cap)) {
                midi_file_free(file);
                return false;
            }
        }
        pos = body + (size_t)length;
    }
    return true;
}

/* The whole file, or NULL; too_big says it is longer than MIDI_FILE_LIMIT. */
static unsigned char *read_all(const char *path, size_t *size, bool *too_big) {
    *size = 0;
    *too_big = false;
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    size_t cap = 4096;
    size_t len = 0;
    unsigned char *data = malloc(cap);
    while (data != NULL) {
        len += fread(data + len, 1, cap - len, f);
        if (len < cap) break;
        if (cap >= (size_t)MIDI_FILE_LIMIT) {
            if (fgetc(f) == EOF) break; /* exactly the limit */
            *too_big = true;
            free(data);
            data = NULL;
            break;
        }
        cap *= 2;
        unsigned char *grown = realloc(data, cap);
        if (grown == NULL) free(data);
        data = grown;
    }
    /* a directory opens on POSIX and only fails when read */
    if (data != NULL && ferror(f)) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = len;
    return data;
}

bool midi_read_file(MidiFile *file, const char *path, char *err, size_t cap) {
    memset(file, 0, sizeof(*file));
    size_t size = 0;
    bool too_big = false;
    unsigned char *data = read_all(path, &size, &too_big);
    if (data == NULL) {
        if (too_big) {
            snprintf(err, cap, "%s: too large for a MIDI file", path);
        } else {
            snprintf(err, cap, "cannot read MIDI file: %s", path);
        }
        return false;
    }
    char problem[200];
    bool ok = midi_read_bytes(file, data, size, problem, sizeof(problem));
    free(data);
    if (!ok) snprintf(err, cap, "%s: %s", path, problem);
    return ok;
}

int midi_first_track(const MidiFile *file) {
    for (int k = 0; k < file->ntracks; k++) {
        for (int n = 0; n < file->tracks[k].count; n++) {
            if (file->tracks[k].notes[n].pitch != PITCH_REST) return k;
        }
    }
    return -1;
}

static long long nearest_step(long tick, int ppq, int beat_steps) {
    return ((long long)tick * beat_steps + ppq / 2) / ppq;
}

int midi_track_grid(const MidiFile *file, int track, int beat_steps, int *steps, int cap) {
    for (int s = 0; s < cap; s++) steps[s] = PITCH_REST;
    if (track < 0 || track >= file->ntracks || file->ppq <= 0 || beat_steps < 1) return 0;
    const MidiTrack *t = &file->tracks[track];
    long long span = 0;
    for (int k = 0; k < t->count; k++) {
        const MidiNote *note = &t->notes[k];
        if (note->pitch == PITCH_REST) continue;
        long long start = nearest_step(note->on, file->ppq, beat_steps);
        long long end = nearest_step(note->off, file->ppq, beat_steps);
        if (end <= start) end = start + 1;
        if (end > span) span = end;
        for (long long s = start; s < end && s < cap; s++) {
            if (note->pitch > steps[s]) steps[s] = note->pitch;
        }
    }
    return span > INT_MAX ? INT_MAX : (int)span;
}

int midi_track_steps(const MidiFile *file, int track, int *steps, int cap) {
    return midi_track_grid(file, track, 1, steps, cap);
}
