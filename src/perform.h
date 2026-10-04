#ifndef PERFORM_H
#define PERFORM_H

#include "score.h"

/* How the piece is played: one plan that canon.mid, voices.wav and the
 * score page all follow, so they agree. The mood sets it (mood key): the
 * dynamics rise to the climax the form planned and fall away, the metre is
 * accented, phrases breathe at the form's phrase ends, the tempo bends a
 * little and broadens over the last two bars, and the last chord is held.
 * Each voice follows its own place in the melody, so a follower reaches the
 * climax when it plays it, and each has its own mix and articulation
 * (players keys). Nothing here is random: a piece plays the same each time. */
typedef struct PerformNote {
    int velocity;   /* 1..127 */
    double sounding; /* fraction of the written length that sounds */
} PerformNote;

typedef struct Performance {
    int mood;                      /* the mood played: MOOD_* other than MOOD_AUTO */
    double times[SPAN_MAX + 1];    /* seconds from the start to each step */
    double loop_times[SPAN_MAX + 1]; /* the same without the closing broadening */
    double hold;                   /* seconds the last notes ring past the end */
    int climax;                    /* step where voice 1 reaches the climax */
    int volume[VOICE_MAX];         /* 0..127 */
    int pan[VOICE_MAX];            /* -100..100 */
    PerformNote note[VOICE_MAX][SPAN_MAX]; /* one per score note, rests included */
} Performance;

void perform_build(Performance *p, const Score *score, const PieceConfig *config,
                   const FormPlan *form);
const char *perform_mood_name(int mood);
/* Seconds note k of voice v sounds from and to; looping leaves out the
 * closing broadening and the hold. */
void perform_span(const Performance *p, const Score *score, int v, int k, bool looping,
                  double *on, double *off);
/* A voice's mix gain, 0..1, from its volume as MIDI players take it. */
double perform_gain(const Performance *p, int v);

#endif
