#ifndef TYPES_H
#define TYPES_H

/* Size limits shared by every module.
 * A melody step is one quarter note; a bar is four steps. */
enum {
    MELODY_MAX = 64,
    VOICE_MAX = 4,
    SPAN_MAX = 128,
    BAR_MAX = SPAN_MAX / 4,
    SECTION_MAX = 2,
    /* pitch + tie per melody note, one chord per bar, one key per section */
    VAR_MAX = 2 * MELODY_MAX + BAR_MAX + SECTION_MAX
};

/* Pitch value 0 stands for a rest, so the lowest playable pitch is 1. */
enum { PITCH_REST = 0 };

/* Tie variable values: a new attack, or a continuation of the previous note. */
enum { TIE_NOTE = 0, TIE_HOLD = 1 };

#endif
