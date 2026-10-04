#ifndef MIDI_READ_H
#define MIDI_READ_H

#include <stdbool.h>
#include <stddef.h>

/* One note of a Standard MIDI file, in ticks from the start of its track. */
typedef struct MidiNote {
    long on;
    long off;
    int pitch;
    int channel;
} MidiNote;

typedef struct MidiTrack {
    int count;
    int capacity;
    MidiNote *notes; /* in the order they start */
} MidiTrack;

enum { MIDI_FILE_LIMIT = 1 << 24 }; /* bytes; a larger file is refused */

/* The notes of every track of a format 0, 1 or 2 file. */
typedef struct MidiFile {
    int format;
    int ppq; /* ticks per quarter note */
    int ntracks;
    MidiTrack *tracks;
} MidiFile;

/* Reads the notes of every track. A note runs from its note-on to a note-off
 * (or note-on with velocity 0) of its pitch and channel, or to the end of its
 * track; when one pitch sounds more than once, each note-off ends the oldest.
 * Notes on channel 10, General MIDI percussion, are drum sounds rather than
 * pitches and are skipped. Returns false and fills err for anything that is
 * not a well-formed file, leaving the file empty; free a file that was read
 * with midi_file_free. */
bool midi_read_file(MidiFile *file, const char *path, char *err, size_t cap);
bool midi_read_bytes(MidiFile *file, const unsigned char *data, size_t size, char *err,
                     size_t cap);
void midi_file_free(MidiFile *file);

/* The first track with a note, or -1. */
int midi_first_track(const MidiFile *file);

/* A track on the canon grid of one step per quarter note. Onsets and ends
 * round to the nearest quarter, a note covers each step from its onset to its
 * end (at least one), the highest of overlapping notes wins, and a step no
 * note covers is PITCH_REST. MIDI note 0 would read as that rest and is
 * skipped. Fills steps[0..cap) and returns how many steps the track spans to
 * the end of its last note, which may be more than cap; 0 for a track
 * without notes. */
int midi_track_steps(const MidiFile *file, int track, int *steps, int cap);
/* The same on a grid of beat_steps steps to a quarter note: 2 reads the
 * track in eighths. */
int midi_track_grid(const MidiFile *file, int track, int beat_steps, int *steps, int cap);

#endif
