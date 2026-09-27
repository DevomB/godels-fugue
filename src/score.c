#include "score.h"

#include "canon.h"

#include <string.h>

/* Melody index where the note containing index i was attacked. */
static int attack_of(const Model *m, const int *values, int i) {
    while (i > 0 && m->tie[i] >= 0 && values[m->tie[i]] == TIE_HOLD) i--;
    return i;
}

void score_build(Score *score, const Model *m, const int *values) {
    memset(score, 0, sizeof(*score));
    score->voices = m->voices;
    score->span = m->span;
    score->tempo = m->config.tempo;
    score->nsections = m->nsections;
    score->modulate_at = m->nsections > 1 ? m->config.modulate_at : -1;
    for (int s = 0; s < m->nsections; s++) score->key[s] = values[m->key[s]];

    for (int v = 0; v < m->voices; v++) {
        ScoreVoice *voice = &score->voice[v];
        int prev_attack = -1;
        for (int t = 0; t < m->span; t++) {
            int i = m->source[v][t];
            int pitch = i >= 0 ? values[m->pitch[i]] : PITCH_REST;
            int sound = canon_sounding(&m->config, v, pitch);
            score->line[v][t] = sound;
            int attack = sound == SOUND_REST ? -1 : attack_of(m, values, i);
            ScoreNote *last = voice->count > 0 ? &voice->notes[voice->count - 1] : NULL;
            /* A rest joins the rest before it; a note continues when the
             * same melody attack is still sounding (a tie, or a follower
             * holding one melody note under augmentation). */
            bool extend = last != NULL &&
                          (sound == SOUND_REST ? last->pitch == SOUND_REST
                                               : (last->pitch != SOUND_REST &&
                                                  attack == prev_attack));
            if (extend) {
                last->length++;
            } else {
                ScoreNote *n = &voice->notes[voice->count++];
                n->start = t;
                n->length = 1;
                n->pitch = sound;
                n->source = sound == SOUND_REST ? -1 : attack;
            }
            prev_attack = attack;
        }
    }
}
