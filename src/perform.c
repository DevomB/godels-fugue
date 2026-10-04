#include "perform.h"

#include "theory.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* A mood's way of playing: loudness at the start, the climax and the end
 * (0..1), how strongly the metre is accented, how long notes sound against
 * their written length, the tempo the last two bars slow to (as a share of
 * the tempo), the share of the tempo kept on a phrase's last beat, how far
 * the tempo bends inside a phrase, and how many times its written length
 * the last chord lasts. */
typedef struct Mood {
    double dyn[3];
    double accent;
    double legato;
    double rit;
    double breathe;
    double rubato;
    double hold;
} Mood;

/* MOOD_AUTO's entry is the moderate way auto falls back on; plain is no
 * shaping at all. */
static const Mood moods[MOOD_COUNT] = {
    {{0.62, 0.82, 0.6}, 1.0, 0.92, 0.82, 0.96, 0.02, 1.6},  /* moderate */
    {{0.7, 0.7, 0.7}, 0.0, 1.0, 1.0, 1.0, 0.0, 1.0},        /* plain */
    {{0.4, 0.86, 0.3}, 0.55, 1.02, 0.62, 0.86, 0.05, 2.6},  /* lament */
    {{0.48, 0.8, 0.4}, 0.4, 1.02, 0.7, 0.88, 0.02, 2.4},    /* hymn */
    {{0.62, 0.92, 1.0}, 1.35, 0.84, 0.8, 0.97, 0.01, 2.2},  /* triumph */
    {{0.38, 0.96, 0.34}, 0.7, 1.0, 0.64, 0.86, 0.07, 2.6},  /* longing */
    {{0.72, 0.9, 0.82}, 1.6, 0.6, 0.92, 1.0, 0.0, 1.3},     /* dance */
    {{0.32, 0.66, 0.26}, 0.5, 1.02, 0.66, 0.9, 0.06, 2.8},  /* nocturne */
};

const char *perform_mood_name(int mood) {
    static const char *const names[MOOD_COUNT] = {"moderate", "plain", "lament", "hymn",
                                                  "triumph",  "longing", "dance", "nocturne"};
    return mood >= 0 && mood < MOOD_COUNT ? names[mood] : "moderate";
}

/* Without a mood: slow minor laments, slow major is a hymn, fast dances,
 * the rest is moderate. */
static int resolve_mood(const Score *score, const PieceConfig *c) {
    if (c->mood != MOOD_AUTO) return c->mood;
    bool minor = key_valid(score->key[0]) && key_mode(score->key[0]) == MODE_MINOR;
    if (c->tempo <= 72) return minor ? MOOD_LAMENT : MOOD_HYMN;
    if (c->tempo >= 120) return MOOD_DANCE;
    return MOOD_AUTO;
}

static double ease(double u) {
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;
    return 0.5 - 0.5 * cos(M_PI * u);
}

/* Loudness 0..1 at melody note i: up to the climax and down after it. */
static double level(const Mood *m, int i, int climax, int length) {
    if (i <= climax) return m->dyn[0] + (m->dyn[1] - m->dyn[0]) * ease((double)i / (climax > 0 ? climax : 1));
    int rest = length - 1 - climax;
    return m->dyn[1] + (m->dyn[2] - m->dyn[1]) * ease((double)(i - climax) / (rest > 0 ? rest : 1));
}

/* The phrase boundaries of the whole span: the form's phrase starts in the
 * first voice, the end of its melody, then on at the same phrase length
 * while the other voices finish. */
static int boundaries(const Score *score, const FormPlan *form, int length, int *out, int cap) {
    int bar = score_bar_steps(score);
    int phrase = form->count > 1 ? form->start[1] : 4 * bar;
    int n = 0;
    for (int k = 1; k < form->count && n < cap; k++) out[n++] = form->start[k];
    for (int b = length; b < score->span && n < cap; b += phrase) out[n++] = b;
    return n;
}

/* Is melody note i part of the subject's head, where it is stated or
 * brought back? */
static bool in_head(const FormPlan *form, int i) {
    for (int k = 0; k < form->count; k++) {
        if (form->role[k] == ROLE_CLIMAX) continue;
        if (i >= form->start[k] && i < form->start[k] + form->head) return true;
    }
    return false;
}

static double articulation(const Mood *m, int kind, bool plain) {
    switch (kind) {
    case ARTICULATION_LEGATO:
        return m->legato > 1.0 ? m->legato : 1.02;
    case ARTICULATION_NORMAL:
        return 0.9;
    case ARTICULATION_DETACHED:
        return 0.72;
    case ARTICULATION_STACCATO:
        return 0.45;
    default:
        return plain ? 1.0 : m->legato;
    }
}

void perform_build(Performance *p, const Score *score, const PieceConfig *c,
                   const FormPlan *form) {
    memset(p, 0, sizeof(*p));
    p->mood = resolve_mood(score, c);
    const Mood *m = &moods[p->mood];
    bool plain = p->mood == MOOD_PLAIN;
    int beat = score_beat_steps(score);
    int bar = score_bar_steps(score);
    int span = score->span;
    int length = c->length > 0 ? c->length : 1;
    double base = 60.0 / (c->tempo > 0 ? c->tempo : 120) / beat;
    p->climax = form->climax;

    /* the time map: rubato inside each phrase, a breath on its last beat,
     * and the last two bars broadening (left out of the looping map) */
    int bounds[MELODY_MAX + SPAN_MAX];
    int nb = boundaries(score, form, length, bounds, (int)(sizeof(bounds) / sizeof(bounds[0])));
    int rit_from = span - 2 * bar > 0 ? span - 2 * bar : 0;
    int from = 0;
    int next = 0;
    for (int s = 0; s < span; s++) {
        while (next < nb && bounds[next] <= s) from = bounds[next++];
        int to = next < nb ? bounds[next] : span;
        double u = to > from ? (double)(s - from) / (to - from) : 0.0;
        double stretch = 1.0 - m->rubato * sin(M_PI * u);
        if (next < nb && s >= to - beat && s < rit_from) stretch /= m->breathe;
        double ritard = 1.0;
        if (s >= rit_from && !c->cyclic) {
            double w = (s - rit_from + 0.5) / (span - rit_from > 0 ? span - rit_from : 1);
            ritard = 1.0 + (1.0 / m->rit - 1.0) * w * w;
        }
        p->loop_times[s + 1] = p->loop_times[s] + base * stretch;
        p->times[s + 1] = p->times[s] + base * stretch * ritard;
    }
    p->hold = c->cyclic ? 0.0 : (m->hold - 1.0) * 2 * beat * base;

    for (int v = 0; v < score->voices && v < VOICE_MAX; v++) {
        int voices = score->voices;
        p->volume[v] = c->volume[v];
        p->pan[v] = c->pan[v] != PAN_AUTO ? c->pan[v]
                    : voices > 1     ? (int)lround(-60.0 + 120.0 * v / (voices - 1))
                                     : 0;
        double shape = plain ? 0.0 : c->intensity[v] / 100.0;
        double art = articulation(m, c->articulation[v], plain);
        bool first = true;
        const ScoreVoice *voice = &score->voice[v];
        for (int k = 0; k < voice->count; k++) {
            const ScoreNote *n = &voice->notes[k];
            PerformNote *out = &p->note[v][k];
            out->sounding = 1.0;
            if (n->pitch == SOUND_REST) continue;
            int i = n->source >= 0 ? n->source : 0;
            double mean = (m->dyn[0] + m->dyn[1] + m->dyn[2]) / 3.0;
            double vel = 24.0 + 96.0 * (mean + (level(m, i, form->climax, length) - mean) * shape);
            if (plain) vel = 24.0 + 96.0 * m->dyn[0];
            int in_bar = n->start % bar;
            double accent = in_bar == 0 ? 9 : in_bar % beat == 0 ? ((in_bar / beat) % 2 == 0 ? 5 : 2) : -4;
            vel += m->accent * accent * shape;
            /* a swell across each phrase of the melody */
            int k0 = 0;
            while (k0 + 1 < form->count && form->start[k0 + 1] <= i) k0++;
            int plen = form->start[k0 + 1] - form->start[k0];
            vel += 6.0 * shape * sin(M_PI * (i - form->start[k0]) / (plen > 0 ? plen : 1));
            /* each entry and the subject's head brought out a little */
            if (first) vel += 8.0 * shape;
            if (in_head(form, i)) vel += 4.0 * shape;
            /* a little life, the same every time */
            unsigned h = (unsigned)(v + 1) * 2654435761u ^ (unsigned)(n->start + 1) * 40503u;
            h ^= h >> 15;
            h *= 2246822519u;
            h ^= h >> 13;
            vel += 3.0 * shape * ((double)(h % 2001) / 1000.0 - 1.0);
            if (vel < 18) vel = 18;
            if (vel > 124) vel = 124;
            out->velocity = (int)lround(vel);
            first = false;
            /* notes shorter than a beat sound a little shorter unless legato */
            double sounding = art;
            if (!plain && n->length < beat && c->articulation[v] != ARTICULATION_LEGATO &&
                sounding > 0.9)
                sounding = 0.9;
            /* the last notes sound to the end, and the hold */
            if (n->start + n->length >= span) sounding = 1.0;
            out->sounding = sounding;
        }
    }
}

void perform_span(const Performance *p, const Score *score, int v, int k, bool looping,
                  double *on, double *off) {
    const ScoreNote *n = &score->voice[v].notes[k];
    const double *t = looping ? p->loop_times : p->times;
    *on = t[n->start];
    *off = *on + (t[n->start + n->length] - t[n->start]) * p->note[v][k].sounding;
    if (!looping && n->start + n->length >= score->span) *off += p->hold;
}

double perform_gain(const Performance *p, int v) {
    double g = p->volume[v] / 100.0;
    return g * g;
}
