#include "export.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *pitch_step(int midi, int *alter, int *octave) {
    static const char *steps[] = {"C", "C", "D", "D", "E", "F",
                                  "F", "G", "G", "A", "A", "B"};
    static const int alters[] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    int pc = midi % 12;
    if (pc < 0) pc += 12;
    *alter = alters[pc];
    *octave = midi / 12 - 1;
    return steps[pc];
}

static int units_at(const int *durations, int i) {
    if (durations == NULL) return 1;
    return durations[i];
}

bool export_musicxml(const char *path, const int *const *lines, int n_voices,
                     int length, const int *durations) {
    if (path == NULL || lines == NULL || n_voices < 1 || length < 0) {
        return false;
    }

    FILE *f = fopen(path, "w");
    if (f == NULL) return false;

    if (fprintf(f,
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<score-partwise version=\"3.1\">\n"
                "  <part-list>\n") < 0) {
        fclose(f);
        return false;
    }
    for (int v = 0; v < n_voices; v++) {
        if (fprintf(f,
                    "    <score-part id=\"P%d\"><part-name>Voice %d</part-name>"
                    "</score-part>\n",
                    v + 1, v + 1) < 0) {
            fclose(f);
            return false;
        }
    }
    if (fprintf(f, "  </part-list>\n") < 0) {
        fclose(f);
        return false;
    }

    for (int v = 0; v < n_voices; v++) {
        if (lines[v] == NULL ||
            fprintf(f, "  <part id=\"P%d\">\n    <measure number=\"1\">\n"
                       "      <attributes><divisions>1</divisions>"
                       "<key><fifths>0</fifths></key>"
                       "<time><beats>4</beats><beat-type>4</beat-type></time>"
                       "<clef><sign>G</sign><line>2</line></clef></attributes>\n",
                    v + 1) < 0) {
            fclose(f);
            return false;
        }
        for (int i = 0; i < length; i++) {
            int units = units_at(durations, i);
            if (units <= 0 || lines[v][i] < 0) {
                if (fprintf(f,
                            "      <note><rest/><duration>1</duration>"
                            "<type>quarter</type></note>\n") < 0) {
                    fclose(f);
                    return false;
                }
                continue;
            }
            int alter = 0;
            int octave = 4;
            const char *step = pitch_step(lines[v][i], &alter, &octave);
            const char *type = units >= 2 ? "half" : "quarter";
            if (fprintf(f, "      <note><pitch><step>%s</step>", step) < 0) {
                fclose(f);
                return false;
            }
            if (alter != 0 &&
                fprintf(f, "<alter>%d</alter>", alter) < 0) {
                fclose(f);
                return false;
            }
            if (fprintf(f,
                        "<octave>%d</octave></pitch><duration>%d</duration>"
                        "<type>%s</type></note>\n",
                        octave, units, type) < 0) {
                fclose(f);
                return false;
            }
        }
        if (fprintf(f, "    </measure>\n  </part>\n") < 0) {
            fclose(f);
            return false;
        }
    }

    if (fprintf(f, "</score-partwise>\n") < 0) {
        fclose(f);
        return false;
    }
    return fclose(f) == 0;
}

bool export_contour(const char *path, const int *melody, int length) {
    if (path == NULL || (length > 0 && melody == NULL) || length < 0) {
        return false;
    }

    FILE *f = fopen(path, "w");
    if (f == NULL) return false;

    if (fprintf(f,
                "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                "width=\"640\" height=\"240\" viewBox=\"0 0 640 240\">\n"
                "  <polyline fill=\"none\" stroke=\"#222\" "
                "stroke-width=\"2\" points=\"") < 0) {
        fclose(f);
        return false;
    }

    for (int i = 0; i < length; i++) {
        int x = length <= 1 ? 20 : 20 + (600 * i) / (length - 1);
        int y = 220 - (melody[i] - 48) * 4;
        if (y < 10) y = 10;
        if (y > 230) y = 230;
        if (fprintf(f, "%s%d,%d", i == 0 ? "" : " ", x, y) < 0) {
            fclose(f);
            return false;
        }
    }

    if (fprintf(f, "\"/>\n</svg>\n") < 0) {
        fclose(f);
        return false;
    }
    return fclose(f) == 0;
}

static int write_u16le(FILE *f, unsigned int v) {
    unsigned char b[2] = {(unsigned char)(v & 0xFF),
                          (unsigned char)((v >> 8) & 0xFF)};
    return fwrite(b, 1, 2, f) == 2 ? 0 : -1;
}

static int write_u32le(FILE *f, unsigned int v) {
    unsigned char b[4] = {(unsigned char)(v & 0xFF),
                          (unsigned char)((v >> 8) & 0xFF),
                          (unsigned char)((v >> 16) & 0xFF),
                          (unsigned char)((v >> 24) & 0xFF)};
    return fwrite(b, 1, 4, f) == 4 ? 0 : -1;
}

static double midi_hz(int pitch) {
    return 440.0 * pow(2.0, ((double)pitch - 69.0) / 12.0);
}

bool export_wav(const char *path, const int *const *lines, int n_voices,
                int length, const int *durations) {
    if (path == NULL || lines == NULL || n_voices < 1 || length < 0) {
        return false;
    }

    const int rate = 44100;
    const int quarter = rate / 4;
    int samples = 0;
    for (int i = 0; i < length; i++) {
        int units = units_at(durations, i);
        if (units < 1) units = 1;
        samples += units * quarter;
    }
    if (samples < 1) samples = quarter;

    FILE *f = fopen(path, "wb");
    if (f == NULL) return false;

    unsigned int data_bytes = (unsigned int)samples * 2u;
    if (fwrite("RIFF", 1, 4, f) != 4 || write_u32le(f, 36 + data_bytes) != 0 ||
        fwrite("WAVE", 1, 4, f) != 4 || fwrite("fmt ", 1, 4, f) != 4 ||
        write_u32le(f, 16) != 0 || write_u16le(f, 1) != 0 ||
        write_u16le(f, 1) != 0 || write_u32le(f, (unsigned int)rate) != 0 ||
        write_u32le(f, (unsigned int)rate * 2u) != 0 || write_u16le(f, 2) != 0 ||
        write_u16le(f, 16) != 0 || fwrite("data", 1, 4, f) != 4 ||
        write_u32le(f, data_bytes) != 0) {
        fclose(f);
        return false;
    }

    int cursor = 0;
    for (int i = 0; i < length; i++) {
        int units = units_at(durations, i);
        if (units < 1) units = 1;
        int n = units * quarter;
        int rest = durations != NULL && durations[i] <= 0;
        for (int s = 0; s < n; s++) {
            double t = (double)(cursor + s) / (double)rate;
            double mix = 0.0;
            if (!rest) {
                int voices = n_voices > 3 ? 3 : n_voices;
                for (int v = 0; v < voices; v++) {
                    if (lines[v] == NULL || lines[v][i] < 0) continue;
                    mix += sin(2.0 * M_PI * midi_hz(lines[v][i]) * t);
                }
                if (voices > 0) mix /= (double)voices;
            }
            int sample = (int)(mix * 8000.0);
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
            if (write_u16le(f, (unsigned int)(sample & 0xFFFF)) != 0) {
                fclose(f);
                return false;
            }
        }
        cursor += n;
    }

    return fclose(f) == 0;
}

static int json_escape(FILE *f, const char *s) {
    if (s == NULL) return 0;
    for (int i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            if (fputc('\\', f) == EOF || fputc((int)c, f) == EOF) return -1;
        } else if (c < 32) {
            if (fprintf(f, "\\u%04x", c) < 0) return -1;
        } else if (fputc((int)c, f) == EOF) {
            return -1;
        }
    }
    return 0;
}

bool export_trace(const char *path, const ProofLog *log) {
    if (path == NULL || log == NULL) return false;

    FILE *f = fopen(path, "w");
    if (f == NULL) return false;

    if (fprintf(f, "{\"events\":[") < 0) {
        fclose(f);
        return false;
    }
    for (int i = 0; i < log->event_count; i++) {
        const ProofEvent *e = &log->events[i];
        if (fprintf(f,
                    "%s{\"variable\":%d,\"pitch\":%d,\"constraint\":%d,"
                    "\"message\":\"",
                    i == 0 ? "" : ",", e->variable_id, e->removed_pitch,
                    e->constraint_id) < 0 ||
            json_escape(f, e->message) != 0 || fprintf(f, "\"}") < 0) {
            fclose(f);
            return false;
        }
    }
    if (fprintf(f, "]}\n") < 0) {
        fclose(f);
        return false;
    }
    return fclose(f) == 0;
}
