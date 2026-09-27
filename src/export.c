#include "export.h"

#include "canon.h"
#include "theory.h"

#include <math.h>
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

/* Step letter, alteration and octave, spelled with flats in flat keys. */
static void spell(int midi, bool flats, char *step, int *alter, int *octave) {
    static const char sharp_steps[12] = {'C', 'C', 'D', 'D', 'E', 'F',
                                         'F', 'G', 'G', 'A', 'A', 'B'};
    static const char flat_steps[12] = {'C', 'D', 'D', 'E', 'E', 'F',
                                        'G', 'G', 'A', 'A', 'B', 'B'};
    static const int sharp_alter[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    static const int flat_alter[12] = {0, -1, 0, -1, 0, 0, -1, 0, -1, 0, -1, 0};
    int pc = pitch_class(midi);
    *step = flats ? flat_steps[pc] : sharp_steps[pc];
    *alter = flats ? flat_alter[pc] : sharp_alter[pc];
    *octave = midi / 12 - 1;
}

static const char *note_type(int steps, bool *dotted) {
    *dotted = steps == 3;
    switch (steps) {
    case 1:
        return "quarter";
    case 2:
    case 3:
        return "half";
    default:
        return "whole";
    }
}

static void write_key(FILE *f, int key) {
    fprintf(f, "<key><fifths>%d</fifths><mode>%s</mode></key>", key_fifths(key),
            key_mode(key) == MODE_MINOR ? "minor" : "major");
}

static void write_segment(FILE *f, int pitch, int steps, bool flats, bool tie_stop,
                          bool tie_start) {
    bool dotted;
    const char *type = note_type(steps, &dotted);
    fprintf(f, "      <note>");
    if (pitch == SOUND_REST) {
        fprintf(f, "<rest/>");
    } else {
        char step;
        int alter;
        int octave;
        spell(pitch, flats, &step, &alter, &octave);
        fprintf(f, "<pitch><step>%c</step>", step);
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
    int bars = (score->span + 3) / 4;
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 3.1 "
               "Partwise//EN\" \"http://www.musicxml.org/dtds/partwise.dtd\">\n"
               "<score-partwise version=\"3.1\">\n"
               "  <work><work-title>Canon Collapse</work-title></work>\n"
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
        if (bars * 4 > score->span) {
            ScoreNote pad = {score->span, bars * 4 - score->span, SOUND_REST, -1};
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
                if (t % 4 == 0 && t / 4 + 1 != measure) {
                    if (measure > 0) fprintf(f, "    </measure>\n");
                    measure = t / 4 + 1;
                    fprintf(f, "    <measure number=\"%d\">\n", measure);
                    if (measure == 1) {
                        fprintf(f, "      <attributes><divisions>1</divisions>");
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
                int stop = (t / 4 + 1) * 4;
                if (score->nsections > 1 && t < score->modulate_at && score->modulate_at < stop)
                    stop = score->modulate_at;
                if (stop > end) stop = end;
                /* a tied note keeps the spelling of its attack */
                bool flats = key_fifths(key_at(score, n->start)) < 0;
                bool tied = n->pitch != SOUND_REST;
                write_segment(f, n->pitch, stop - t, flats, tied && t > n->start,
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
    double dx = score->span > 1 ? (double)(width - 2 * pad) / (score->span - 1) : 0.0;
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
                           "points=\"",
                        colors[v % VOICE_MAX]);
                open = true;
            } else {
                fprintf(f, " ");
            }
            fprintf(f, "%.1f,%.1f", pad + t * dx, height - pad - (p - low) * dy);
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

/* One cycle of a triangle wave, 256 samples. */
static double wavetable(double phase) {
    int i = (int)(phase * 256.0) & 255;
    int rise = i < 128 ? i : 255 - i;
    return (rise - 64) / 64.0;
}

bool export_wav(const char *path, const Score *score, int sample) {
    if (path == NULL || score == NULL) return false;
    const int rate = 44100;
    int tempo = score->tempo > 0 ? score->tempo : 120;
    long per_step = (long)rate * 60 / tempo;
    long total = per_step * (score->span > 0 ? score->span : 1);
    float *mix = calloc((size_t)total, sizeof(float));
    if (mix == NULL) return false;

    const long attack = rate / 200; /* 5 ms */
    const long release = rate / 40; /* 25 ms */
    for (int v = 0; v < score->voices; v++) {
        double phase = 0.0; /* continuous per voice, so re-attacks do not click */
        for (int k = 0; k < score->voice[v].count; k++) {
            const ScoreNote *n = &score->voice[v].notes[k];
            if (n->pitch == SOUND_REST) continue;
            long start = per_step * n->start;
            long len = per_step * n->length;
            double step = midi_hz(n->pitch) / rate;
            for (long i = 0; i < len && start + i < total; i++) {
                double env = 1.0;
                if (i < attack) env = (double)i / attack;
                if (len - i < release) env *= (double)(len - i) / release;
                double wave = sample ? wavetable(phase) : sin(2.0 * M_PI * phase);
                mix[start + i] += (float)(wave * env);
                phase += step;
                if (phase >= 1.0) phase -= floor(phase);
            }
        }
    }

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        free(mix);
        return false;
    }
    unsigned data_bytes = (unsigned)total * 2u;
    int rc = 0;
    if (fwrite("RIFF", 1, 4, f) != 4 || write_u32le(f, 36 + data_bytes) != 0 ||
        fwrite("WAVE", 1, 4, f) != 4 || fwrite("fmt ", 1, 4, f) != 4 ||
        write_u32le(f, 16) != 0 || write_u16le(f, 1) != 0 || write_u16le(f, 1) != 0 ||
        write_u32le(f, (unsigned)rate) != 0 || write_u32le(f, (unsigned)rate * 2u) != 0 ||
        write_u16le(f, 2) != 0 || write_u16le(f, 16) != 0 || fwrite("data", 1, 4, f) != 4 ||
        write_u32le(f, data_bytes) != 0)
        rc = -1;
    double scale = 9000.0 / (score->voices > 0 ? score->voices : 1);
    for (long i = 0; i < total && rc == 0; i++) {
        long pcm = lround(mix[i] * scale);
        if (pcm > 32767) pcm = 32767;
        if (pcm < -32768) pcm = -32768;
        rc = write_u16le(f, (unsigned)(pcm & 0xFFFF));
    }
    free(mix);
    if (fclose(f) != 0) rc = -1;
    return rc == 0;
}
