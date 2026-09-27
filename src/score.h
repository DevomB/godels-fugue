#ifndef SCORE_H
#define SCORE_H

#include "canon.h"
#include "model.h"

/* A note or rest in one voice, in steps (quarter notes). */
typedef struct ScoreNote {
    int start;
    int length;
    int pitch;  /* sounding MIDI pitch, or SOUND_REST */
    int source; /* melody index of the attack, or -1 for a rest */
} ScoreNote;

typedef struct ScoreVoice {
    int count;
    ScoreNote notes[SPAN_MAX];
} ScoreVoice;

typedef struct Score {
    int voices;
    int span;
    int tempo;
    int nsections;
    int key[SECTION_MAX];
    int modulate_at;
    int line[VOICE_MAX][SPAN_MAX]; /* sounding pitch at each step */
    ScoreVoice voice[VOICE_MAX];   /* notes covering steps 0..span-1 */
} Score;

/* values holds one collapsed value per model variable. */
void score_build(Score *score, const Model *m, const int *values);

#endif
