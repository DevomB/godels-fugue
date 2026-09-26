#include "export.h"

#include "canon.h"
#include "constraint.h"

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

/* 256-sample cycle stored in this file. Triangle, not the live sine path. */
static const short *wavetable_cycle(void) {
    static short table[256];
    static int ready = 0;
    if (!ready) {
        for (int i = 0; i < 256; i++) {
            int rise = i < 128 ? i : 255 - i;
            table[i] = (short)((rise - 64) * 400);
        }
        ready = 1;
    }
    return table;
}

bool export_wav(const char *path, const int *const *lines, int n_voices,
                int length, const int *durations, int sample) {
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
    const short *table = sample ? wavetable_cycle() : NULL;
    unsigned phase[4] = {0, 0, 0, 0};
    unsigned step[4] = {0, 0, 0, 0};
    for (int i = 0; i < length; i++) {
        int units = units_at(durations, i);
        if (units < 1) units = 1;
        int n = units * quarter;
        int rest = durations != NULL && durations[i] <= 0;
        int voices = n_voices > 3 ? 3 : n_voices;
        if (sample && !rest) {
            for (int v = 0; v < voices; v++) {
                if (lines[v] == NULL || lines[v][i] < 0) {
                    step[v] = 0;
                    continue;
                }
                double hz = midi_hz(lines[v][i]);
                step[v] = (unsigned)(hz * 256.0 * 256.0 / (double)rate + 0.5);
            }
        }
        for (int s = 0; s < n; s++) {
            int pcm = 0;
            if (!rest) {
                if (sample) {
                    int acc = 0;
                    int nlive = 0;
                    for (int v = 0; v < voices; v++) {
                        if (step[v] == 0) continue;
                        acc += table[(phase[v] >> 8) & 255];
                        phase[v] += step[v];
                        nlive++;
                    }
                    if (nlive > 0) pcm = acc / nlive;
                } else {
                    double t = (double)(cursor + s) / (double)rate;
                    double mix = 0.0;
                    for (int v = 0; v < voices; v++) {
                        if (lines[v] == NULL || lines[v][i] < 0) continue;
                        mix += sin(2.0 * M_PI * midi_hz(lines[v][i]) * t);
                    }
                    if (voices > 0) mix /= (double)voices;
                    pcm = (int)(mix * 8000.0);
                }
            }
            if (pcm > 32767) pcm = 32767;
            if (pcm < -32768) pcm = -32768;
            if (write_u16le(f, (unsigned int)(pcm & 0xFFFF)) != 0) {
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

bool export_score_page(const char *path, const int *melody,
                       const PieceConfig *config, const SolverState *state,
                       int backtracks, int energy) {
    if (path == NULL || melody == NULL || config == NULL || state == NULL) {
        return false;
    }

    FILE *f = fopen(path, "w");
    if (f == NULL) return false;

    int length = config->length;
    if (fprintf(f,
                "<!DOCTYPE html>\n<html lang=\"en\"><head>"
                "<meta charset=\"utf-8\"><title>Score</title>"
                "<style>body{font:16px/1.4 sans-serif;margin:2rem;}"
                "pre{background:#f6f6f6;padding:1rem;}</style>"
                "</head><body>\n<h1>Score</h1>\n<pre>\nmelody:") < 0) {
        fclose(f);
        return false;
    }
    for (int i = 0; i < length; i++) {
        if (fprintf(f, " %d", melody[i]) < 0) {
            fclose(f);
            return false;
        }
    }
    if (fprintf(f, "\n") < 0) {
        fclose(f);
        return false;
    }

    int span = canon_span_config(config);
    for (int v = 1; v < config->voices; v++) {
        if (fprintf(f, "voice %d:", v + 1) < 0) {
            fclose(f);
            return false;
        }
        for (int t = 0; t < span; t++) {
            int idx = canon_map_source(config, v, t);
            if (idx < 0) {
                if (fprintf(f, " rest") < 0) {
                    fclose(f);
                    return false;
                }
            } else if (fprintf(f, " %d",
                               canon_sounding(config, v, melody[idx])) < 0) {
                fclose(f);
                return false;
            }
        }
        if (fprintf(f, "\n") < 0) {
            fclose(f);
            return false;
        }
    }

    if (fprintf(f, "backtracks: %d\nentropy: %.6f\n", backtracks,
                entropy_bits(state->domains, length)) < 0) {
        fclose(f);
        return false;
    }
    if (config->energy == 1 && fprintf(f, "energy: %d\n", energy) < 0) {
        fclose(f);
        return false;
    }
    if (fprintf(f,
                "</pre>\n<label>variable id <input id=\"varfilter\" "
                "type=\"text\"></label>\n<ul id=\"events\">\n") < 0) {
        fclose(f);
        return false;
    }
    for (int i = 0; i < state->proof.event_count; i++) {
        const ProofEvent *e = &state->proof.events[i];
        if (fprintf(f, "<li data-var=\"%d\">variable %d pitch %d %s</li>\n",
                    e->variable_id, e->variable_id, e->removed_pitch,
                    e->message[0] ? e->message : "") < 0) {
            fclose(f);
            return false;
        }
    }
    if (fprintf(f,
                "</ul>\n<script>"
                "document.getElementById('varfilter').oninput=function(){"
                "var q=this.value.trim();"
                "document.querySelectorAll('#events li').forEach(function(li){"
                "li.style.display=(!q||li.getAttribute('data-var')===q)?'':'none';"
                "});};"
                "</script>\n</body></html>\n") < 0) {
        fclose(f);
        return false;
    }
    return fclose(f) == 0;
}

bool export_report(const char *path, const PieceConfig *config, int backtracks,
                   double entropy, int energy, const int *core, int core_n) {
    if (path == NULL || config == NULL) return false;

    FILE *f = fopen(path, "w");
    if (f == NULL) return false;

    if (fprintf(f,
                "rules: invert=%d retrograde=%d cadence=%d strong_chord=%d "
                "cyclic=%d motif=%d\nbacktracks: %d\nentropy: %.6f\nenergy: %d\n",
                config->invert, config->retrograde, config->cadence,
                config->strong_chord, config->cyclic, config->w_motif,
                backtracks, entropy, energy) < 0) {
        fclose(f);
        return false;
    }
    if (core_n > 0 && core != NULL) {
        if (fprintf(f, "core:") < 0) {
            fclose(f);
            return false;
        }
        for (int i = 0; i < core_n; i++) {
            if (fprintf(f, " %s", constraint_name(core[i])) < 0) {
                fclose(f);
                return false;
            }
        }
        if (fprintf(f, "\n") < 0) {
            fclose(f);
            return false;
        }
    }
    return fclose(f) == 0;
}
