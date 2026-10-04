#include "export.h"

#include "canon.h"
#include "theory.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int key_at(const Score *score, int step) {
    if (score->nsections > 1 && step >= score->modulate_at) return score->key[1];
    return score->key[0];
}


/* A written value (score_written_steps) in eighths. */
static const char *note_type(int eighths, bool *dotted) {
    *dotted = eighths == 3 || eighths == 6;
    switch (eighths) {
    case 1:
        return "eighth";
    case 2:
    case 3:
        return "quarter";
    case 4:
    case 6:
        return "half";
    default:
        return "whole";
    }
}

static void write_key(FILE *f, int key) {
    fprintf(f, "<key><fifths>%d</fifths><mode>%s</mode></key>", key_fifths(key),
            mode_name(key_mode(key)));
}

static void write_segment(FILE *f, const Score *score, int pitch, int steps, int key,
                          bool tie_stop, bool tie_start) {
    bool dotted;
    const char *type = note_type(score_eighths(score, steps), &dotted);
    fprintf(f, "      <note>");
    if (pitch == SOUND_REST) {
        fprintf(f, "<rest/>");
    } else {
        int letter;
        int alter;
        int octave;
        key_spell(key, pitch, &letter, &alter, &octave);
        fprintf(f, "<pitch><step>%c</step>", "CDEFGAB"[letter]);
        if (alter != 0) fprintf(f, "<alter>%d</alter>", alter);
        fprintf(f, "<octave>%d</octave></pitch>", octave);
    }
    fprintf(f, "<duration>%d</duration>", steps);
    if (tie_stop) fprintf(f, "<tie type=\"stop\"/>");
    if (tie_start) fprintf(f, "<tie type=\"start\"/>");
    fprintf(f, "<type>%s</type>%s", type, dotted ? "<dot/>" : "");
    if (tie_stop || tie_start) {
        fprintf(f, "<notations>");
        if (tie_stop) fprintf(f, "<tied type=\"stop\"/>");
        if (tie_start) fprintf(f, "<tied type=\"start\"/>");
        fprintf(f, "</notations>");
    }
    fprintf(f, "</note>\n");
}

static bool low_voice(const Score *score, int v) {
    long sum = 0;
    int n = 0;
    for (int t = 0; t < score->span; t++) {
        if (score->line[v][t] == SOUND_REST) continue;
        sum += score->line[v][t];
        n++;
    }
    return n > 0 && sum / n < 57;
}

bool export_musicxml(const char *path, const Score *score) {
    if (path == NULL || score == NULL || score->voices < 1) return false;
    FILE *f = fopen(path, "w");
    if (f == NULL) return false;
    int bar = score_bar_steps(score);
    int bars = (score->span + bar - 1) / bar;
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 3.1 "
               "Partwise//EN\" \"http://www.musicxml.org/dtds/partwise.dtd\">\n"
               "<score-partwise version=\"3.1\">\n"
               "  <work><work-title>" PROJECT_TITLE "</work-title></work>\n"
               "  <part-list>\n");
    for (int v = 0; v < score->voices; v++) {
        fprintf(f, "    <score-part id=\"P%d\"><part-name>Voice %d</part-name></score-part>\n",
                v + 1, v + 1);
    }
    fprintf(f, "  </part-list>\n");

    for (int v = 0; v < score->voices; v++) {
        fprintf(f, "  <part id=\"P%d\">\n", v + 1);
        /* Walk the voice's notes plus a padding rest to the end of the bar,
         * cutting each at barlines and at the key change. */
        ScoreNote notes[SPAN_MAX + 1];
        int count = score->voice[v].count;
        memcpy(notes, score->voice[v].notes, (size_t)count * sizeof(ScoreNote));
        if (bars * bar > score->span) {
            ScoreNote pad = {score->span, bars * bar - score->span, SOUND_REST, -1};
            if (count > 0 && notes[count - 1].pitch == SOUND_REST) {
                notes[count - 1].length += pad.length;
            } else {
                notes[count++] = pad;
            }
        }
        int measure = 0;
        for (int k = 0; k < count; k++) {
            const ScoreNote *n = &notes[k];
            int t = n->start;
            int end = n->start + n->length;
            while (t < end) {
                if (t % bar == 0 && t / bar + 1 != measure) {
                    if (measure > 0) fprintf(f, "    </measure>\n");
                    measure = t / bar + 1;
                    fprintf(f, "    <measure number=\"%d\">\n", measure);
                    if (measure == 1) {
                        /* a division is one step */
                        fprintf(f, "      <attributes><divisions>%d</divisions>",
                                score_beat_steps(score));
                        write_key(f, score->key[0]);
                        fprintf(f, "<time><beats>4</beats><beat-type>4</beat-type></time>"
                                   "<clef><sign>%s</sign><line>%d</line></clef>"
                                   "</attributes>\n",
                                low_voice(score, v) ? "F" : "G", low_voice(score, v) ? 4 : 2);
                    }
                }
                if (score->nsections > 1 && t == score->modulate_at && t > 0) {
                    fprintf(f, "      <attributes>");
                    write_key(f, score->key[1]);
                    fprintf(f, "</attributes>\n");
                }
                int stop = (t / bar + 1) * bar;
                if (score->nsections > 1 && t < score->modulate_at && score->modulate_at < stop)
                    stop = score->modulate_at;
                if (stop > end) stop = end;
                /* a length no single value writes, such as five eighths, is tied */
                stop = t + score_written_steps(score, stop - t);
                /* a tied note keeps the spelling of its attack */
                int spelling_key = key_at(score, n->start);
                bool tied = n->pitch != SOUND_REST;
                write_segment(f, score, n->pitch, stop - t, spelling_key, tied && t > n->start,
                              tied && stop < end);
                t = stop;
            }
        }
        if (measure > 0) fprintf(f, "    </measure>\n");
        fprintf(f, "  </part>\n");
    }
    fprintf(f, "</score-partwise>\n");
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

bool export_contour(const char *path, const Score *score) {
    static const char *const colors[VOICE_MAX] = {"#1f5fbf", "#c2410c", "#15803d", "#7e22ce"};
    if (path == NULL || score == NULL) return false;
    FILE *f = fopen(path, "w");
    if (f == NULL) return false;
    int low = 127;
    int high = 0;
    for (int v = 0; v < score->voices; v++) {
        for (int t = 0; t < score->span; t++) {
            int p = score->line[v][t];
            if (p == SOUND_REST) continue;
            if (p < low) low = p;
            if (p > high) high = p;
        }
    }
    if (low > high) {
        low = 60;
        high = 72;
    }
    const int width = 640;
    const int height = 240;
    const int pad = 20;
    double dx = (double)(width - 2 * pad) / (score->span > 0 ? score->span : 1);
    double dy = (double)(height - 2 * pad) / (high - low > 0 ? high - low : 1);
    fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" "
               "viewBox=\"0 0 %d %d\">\n  <title>Pitch contour of every voice</title>\n",
            width, height, width, height);
    for (int v = 0; v < score->voices; v++) {
        bool open = false;
        for (int t = 0; t <= score->span; t++) {
            int p = t < score->span ? score->line[v][t] : SOUND_REST;
            if (p == SOUND_REST) {
                if (open) fprintf(f, "\"/>\n");
                open = false;
                continue;
            }
            if (!open) {
                fprintf(f, "  <polyline fill=\"none\" stroke=\"%s\" stroke-width=\"2\" "
                           "stroke-linejoin=\"round\" points=\"",
                        colors[v % VOICE_MAX]);
                open = true;
            } else {
                fprintf(f, " ");
            }
            double y = height - pad - (p - low) * dy;
            fprintf(f, "%.1f,%.1f %.1f,%.1f", pad + t * dx, y, pad + (t + 1) * dx, y);
        }
    }
    fprintf(f, "</svg>\n");
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

static int write_u16le(FILE *f, unsigned v) {
    unsigned char b[2] = {(unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF)};
    return fwrite(b, 1, 2, f) == 2 ? 0 : -1;
}

static int write_u32le(FILE *f, unsigned v) {
    unsigned char b[4] = {(unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF),
                          (unsigned char)((v >> 16) & 0xFF), (unsigned char)((v >> 24) & 0xFF)};
    return fwrite(b, 1, 4, f) == 4 ? 0 : -1;
}

static double midi_hz(int pitch) {
    return 440.0 * pow(2.0, ((double)pitch - 69.0) / 12.0);
}

enum { WAV_RATE = 44100, WAV_TAIL = WAV_RATE * 3 / 2, PLUCK_LINE_MAX = 8192 };

/* A voice's place in the stereo mix: interleaved left and right frames,
 * and the gain of each side. */
typedef struct Pan {
    float *frames;
    double left;
    double right;
} Pan;

static void pan_add(const Pan *pan, long i, double s) {
    pan->frames[2 * i] += (float)(s * pan->left);
    pan->frames[2 * i + 1] += (float)(s * pan->right);
}

/* Rises from 0 over attack samples and falls back to 0 over the last
 * release samples of a note len samples long. */
static double envelope(long i, long len, long attack, long release) {
    double env = 1.0;
    if (i < attack) env = (double)i / (double)attack;
    if (len - i < release) env *= (double)(len - i) / (double)release;
    return env;
}

/* A fixed-seed generator, so a piece always gets the same noise. */
static double noise(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return (double)(*state >> 8) / 8388608.0 - 1.0;
}

/* Karplus-Strong: a burst of noise circulates through a delay line one
 * period long, and averaging neighbouring samples on each pass dulls and
 * decays it like a plucked string. The string is damped when the note ends. */
static void pluck(const Pan *out, long start, long len, int pitch, uint32_t seed,
                  double *line) {
    double hz = midi_hz(pitch);
    double period = WAV_RATE / hz;
    /* the average delays half a sample, and an allpass adds the fraction left */
    int n = (int)floor(period - 0.6);
    if (n < 3) n = 3;
    if (n > PLUCK_LINE_MAX) n = PLUCK_LINE_MAX;
    double frac = period - 0.5 - n;
    if (frac < 0.1) frac = 0.1;
    double coef = (1.0 - frac) / (1.0 + frac);
    /* falls 60 dB in 3 s at 110 Hz, down to 1 s from 1760 Hz up */
    double ring = 3.0 * pow(110.0 / hz, 0.4);
    if (ring > 3.0) ring = 3.0;
    if (ring < 1.0) ring = 1.0;
    double decay = pow(10.0, -3.0 / (ring * hz)) / cos(M_PI * hz / WAV_RATE);
    if (decay > 1.0) decay = 1.0;

    /* lightly low-passed noise with no offset, at the same level for every note */
    uint32_t state = seed;
    for (int k = 0; k < n; k++) line[k] = noise(&state);
    double first = line[0];
    double mean = 0.0;
    for (int k = 0; k < n; k++) {
        line[k] = 0.5 * (line[k] + (k + 1 < n ? line[k + 1] : first));
        mean += line[k];
    }
    mean /= n;
    double power = 0.0;
    for (int k = 0; k < n; k++) {
        line[k] -= mean;
        power += line[k] * line[k];
    }
    double gain = power > 0.0 ? 0.5 / sqrt(power / n) : 0.0;
    for (int k = 0; k < n; k++) line[k] *= gain;

    const long attack = WAV_RATE * 2 / 1000;
    const long damp = WAV_RATE * 30 / 1000;
    /* starting the filters at rest keeps the circulating sum, and so the
     * offset, at zero */
    double last = 0.0;
    double in1 = 0.0;
    double out1 = 0.0;
    int pos = 0;
    for (long i = 0; i < len; i++) {
        double s = line[pos];
        double avg = 0.5 * (s + last);
        last = s;
        out1 = coef * avg + in1 - coef * out1;
        in1 = avg;
        line[pos] = decay * out1;
        if (++pos == n) pos = 0;
        pan_add(out, start + i, s * envelope(i, len, attack, damp));
    }
}

/* Organ: the first four harmonics; sine: the fundamental alone. The phase
 * runs on across a voice's notes, so a re-attack does not click. */
static void tone(const Pan *out, long start, long len, int pitch, bool organ,
                 double *phase) {
    static const double partials[4] = {1.0, 0.5, 0.25, 0.12};
    const int count = organ ? 4 : 1;
    const double norm = organ ? 1.0 / 1.87 : 1.0;
    const long attack = WAV_RATE * (organ ? 15 : 10) / 1000;
    const long release = WAV_RATE * (organ ? 60 : 40) / 1000;
    double step = midi_hz(pitch) / WAV_RATE;
    for (long i = 0; i < len; i++) {
        double wave = 0.0;
        for (int h = 0; h < count; h++) {
            if ((h + 1) * step < 0.5) wave += partials[h] * sin(2.0 * M_PI * (h + 1) * *phase);
        }
        pan_add(out, start + i, wave * norm * envelope(i, len, attack, release));
        *phase += step;
        if (*phase >= 1.0) *phase -= floor(*phase);
    }
}

/* A small Schroeder reverb in the manner of Freeverb: per channel, four
 * feedback combs with a low-pass in the loop run in parallel, then two
 * allpasses in series. The right channel's delays are a little longer, so
 * the sides differ. */
enum { COMB_COUNT = 4, ALLPASS_COUNT = 2, REVERB_SPREAD = 23 };
enum { DELAY_MAX = 1356 + REVERB_SPREAD };
static const int comb_delay[COMB_COUNT] = {1116, 1188, 1277, 1356};
static const int allpass_delay[ALLPASS_COUNT] = {556, 441};
static const double comb_feedback = 0.84;
static const double comb_damp = 0.2;
static const double allpass_feedback = 0.5;
static const double reverb_wet = 0.18;

typedef struct Delay {
    double buf[DELAY_MAX];
    int size;
    int pos;
    double low; /* the comb's low-pass memory */
} Delay;

typedef struct ReverbSide {
    Delay comb[COMB_COUNT];
    Delay allpass[ALLPASS_COUNT];
} ReverbSide;

static void reverb_init(ReverbSide *side, int spread) {
    memset(side, 0, sizeof(*side));
    for (int k = 0; k < COMB_COUNT; k++) side->comb[k].size = comb_delay[k] + spread;
    for (int k = 0; k < ALLPASS_COUNT; k++) side->allpass[k].size = allpass_delay[k] + spread;
}

static double reverb_run(ReverbSide *side, double in) {
    double wet = 0.0;
    for (int k = 0; k < COMB_COUNT; k++) {
        Delay *d = &side->comb[k];
        double out = d->buf[d->pos];
        d->low = out * (1.0 - comb_damp) + d->low * comb_damp;
        d->buf[d->pos] = in + d->low * comb_feedback;
        if (++d->pos == d->size) d->pos = 0;
        wet += out;
    }
    wet /= COMB_COUNT;
    for (int k = 0; k < ALLPASS_COUNT; k++) {
        Delay *d = &side->allpass[k];
        double held = d->buf[d->pos];
        d->buf[d->pos] = wet + held * allpass_feedback;
        if (++d->pos == d->size) d->pos = 0;
        wet = held - wet;
    }
    return wet;
}

bool export_wav(const char *path, const Score *score, int instrument) {
    if (path == NULL || score == NULL) return false;
    int tempo = score->tempo > 0 ? score->tempo : 120;
    double per_step = (double)WAV_RATE * 60.0 / tempo / score_beat_steps(score);
    long total = lround(per_step * (score->span > 0 ? score->span : 1)) + WAV_TAIL;
    float *frames = calloc((size_t)total * 2, sizeof(float));
    double *line = malloc(PLUCK_LINE_MAX * sizeof(double));
    ReverbSide *reverb = malloc(2 * sizeof(ReverbSide));
    if (frames == NULL || line == NULL || reverb == NULL) {
        free(frames);
        free(line);
        free(reverb);
        return false;
    }

    for (int v = 0; v < score->voices; v++) {
        /* equal-power pan, the voices spread evenly from left to right */
        double place = score->voices > 1 ? -0.6 + 1.2 * v / (score->voices - 1) : 0.0;
        double angle = (place + 1.0) * M_PI / 4.0;
        Pan pan = {frames, cos(angle), sin(angle)};
        double phase = 0.0;
        for (int k = 0; k < score->voice[v].count; k++) {
            const ScoreNote *n = &score->voice[v].notes[k];
            if (n->pitch == SOUND_REST) continue;
            long start = lround(per_step * n->start);
            long len = lround(per_step * (n->start + n->length)) - start;
            if (start + len > total) len = total - start;
            if (len <= 0) continue;
            if (instrument == INSTRUMENT_PLUCK) {
                uint32_t seed = 0x9E3779B9u * (uint32_t)(v + 1) ^
                                0x85EBCA6Bu * (uint32_t)n->pitch ^
                                0xC2B2AE35u * (uint32_t)(n->start + 1);
                pluck(&pan, start, len, n->pitch, seed, line);
            } else {
                tone(&pan, start, len, n->pitch, instrument == INSTRUMENT_ORGAN, &phase);
            }
        }
    }
    free(line);

    /* both sides of the reverb hear the middle of the mix */
    reverb_init(&reverb[0], 0);
    reverb_init(&reverb[1], REVERB_SPREAD);
    double peak = 0.0;
    for (long i = 0; i < total; i++) {
        float *frame = &frames[2 * i];
        double in = 0.5 * ((double)frame[0] + frame[1]);
        for (int c = 0; c < 2; c++) {
            frame[c] = (float)(frame[c] + reverb_wet * reverb_run(&reverb[c], in));
            if (fabs(frame[c]) > peak) peak = fabs(frame[c]);
        }
    }
    free(reverb);

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        free(frames);
        return false;
    }
    unsigned data_bytes = (unsigned)total * 4u;
    int rc = 0;
    if (fwrite("RIFF", 1, 4, f) != 4 || write_u32le(f, 36 + data_bytes) != 0 ||
        fwrite("WAVE", 1, 4, f) != 4 || fwrite("fmt ", 1, 4, f) != 4 ||
        write_u32le(f, 16) != 0 || write_u16le(f, 1) != 0 || write_u16le(f, 2) != 0 ||
        write_u32le(f, (unsigned)WAV_RATE) != 0 || write_u32le(f, WAV_RATE * 4u) != 0 ||
        write_u16le(f, 4) != 0 || write_u16le(f, 16) != 0 || fwrite("data", 1, 4, f) != 4 ||
        write_u32le(f, data_bytes) != 0)
        rc = -1;
    /* the loudest sample lands at -1 dBFS; silence stays silent */
    double scale = peak > 1e-6 ? 32767.0 * pow(10.0, -1.0 / 20.0) / peak : 0.0;
    unsigned char block[4096];
    size_t used = 0;
    for (long i = 0; i < 2 * total && rc == 0; i++) {
        long pcm = lround(frames[i] * scale);
        if (pcm > 32767) pcm = 32767;
        if (pcm < -32768) pcm = -32768;
        unsigned bits = (unsigned)(pcm & 0xFFFF);
        block[used++] = (unsigned char)(bits & 0xFF);
        block[used++] = (unsigned char)(bits >> 8);
        if (used == sizeof(block) || i + 1 == 2 * total) {
            if (fwrite(block, 1, used, f) != used) rc = -1;
            used = 0;
        }
    }
    free(frames);
    if (fclose(f) != 0) rc = -1;
    return rc == 0;
}
