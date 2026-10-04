#ifndef SCORE_H
#define SCORE_H

#include "canon.h"
#include "model.h"

/* A note or rest in one voice, in steps (quarter notes, or eighths on the
 * eighth grid). */
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
    int beat_steps; /* steps per quarter-note beat: 1, or 2 on the eighth grid; 0 reads as 1 */
    int nsections;
    int key[SECTION_MAX];
    int modulate_at;
    int line[VOICE_MAX][SPAN_MAX]; /* sounding pitch at each step */
    ScoreVoice voice[VOICE_MAX];   /* notes covering steps 0..span-1 */
} Score;

/* values holds one collapsed value per model variable. */
void score_build(Score *score, const Model *m, const int *values);

/* Steps in one beat and in one 4/4 bar. */
int score_beat_steps(const Score *score);
int score_bar_steps(const Score *score);
/* A length in steps as eighth notes. */
int score_eighths(const Score *score, int steps);
/* The longest value written as one note, plain or dotted and at most a
 * whole note (1, 2, 3, 4, 6 or 8 eighths), that lasts no more than steps;
 * a longer note is written as such values tied. */
int score_written_steps(const Score *score, int steps);

#endif
