#ifndef TYPES_H
#define TYPES_H

/* Size limits shared by every module.
 * A melody step is one quarter note, or one eighth or sixteenth on the finer
 * grids (config_beat_steps); a 4/4 bar is four steps, eight or sixteen. A
 * melody is at most 8 bars of sixteenths and a piece 16. */
enum {
    MELODY_MAX = 128,
    VOICE_MAX = 4,
    SPAN_MAX = 256,
    BAR_MAX = SPAN_MAX / 4, /* bars of the quarter grid; the finer grids have fewer */
    SECTION_MAX = 2,
    /* pitch + tie per melody note, one chord per bar, one key per section */
    VAR_MAX = 2 * MELODY_MAX + BAR_MAX + SECTION_MAX
};

/* Pitch value 0 stands for a rest, so the lowest playable pitch is 1. */
enum { PITCH_REST = 0 };

/* Tie variable values: a new attack, or a continuation of the previous note. */
enum { TIE_NOTE = 0, TIE_HOLD = 1 };

/* The project's name as score files and reports write it, in UTF-8. */
#define PROJECT_TITLE "G\xc3\xb6" "del's Fugue"

#endif
