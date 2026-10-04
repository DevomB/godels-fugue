#include "notation.h"

#include "canon.h"
#include "theory.h"

#include <stdio.h>
#include <string.h>

/* A note or rest cut at barlines, at the key change, and into values
 * written as one note. */
typedef struct Segment {
    int start;
    int steps;
    int pitch;
    int spelling_key; /* a tied note keeps the spelling of its attack */
    bool tie;         /* tied to the next segment */
} Segment;

enum { SEGMENT_MAX = SPAN_MAX + 8 }; /* each lasts a step or more, to the end of a bar */

static bool modulates(const Score *score) {
    return score->nsections > 1 && score->modulate_at > 0 && score->modulate_at < score->span;
}

static int key_at(const Score *score, int step) {
    if (score->nsections > 1 && step >= score->modulate_at) return score->key[1];
    return score->key[0];
}

/* The voice's notes plus a rest padding the last bar. */
static int voice_segments(const Score *score, int v, Segment *out) {
    const ScoreVoice *voice = &score->voice[v];
    int bar = score_bar_steps(score);
    int pad = (score->span + bar - 1) / bar * bar - score->span;
    int n = 0;
    for (int k = 0; k <= voice->count; k++) {
        ScoreNote note = {score->span, pad, SOUND_REST, -1};
        if (k < voice->count) {
            note = voice->notes[k];
            if (k == voice->count - 1 && note.pitch == SOUND_REST) {
                note.length += pad; /* a final rest runs on to the barline */
                pad = 0;
            }
        }
        int t = note.start;
        int end = note.start + note.length;
        while (t < end && n < SEGMENT_MAX) {
            int stop = (t / bar + 1) * bar;
            if (modulates(score) && t < score->modulate_at && score->modulate_at < stop)
                stop = score->modulate_at;
            if (stop > end) stop = end;
            stop = t + score_written_steps(score, stop - t);
            Segment s = {t, stop - t, note.pitch, key_at(score, note.start),
                         note.pitch != SOUND_REST && stop < end};
            out[n++] = s;
            t = stop;
        }
    }
    return n;
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

static bool finish(FILE *f) {
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

static const char *ly_alter(int alter) {
    switch (alter) {
    case 2:
        return "isis";
    case 1:
        return "is";
    case -1:
        return "es";
    case -2:
        return "eses";
    default:
        return "";
    }
}

static void ly_key(FILE *f, int key) {
    int letter;
    int alter;
    int octave;
    key_spell(key, key_tonic(key), &letter, &alter, &octave);
    fprintf(f, "\\key %c%s \\%s", "cdefgab"[letter], ly_alter(alter), mode_name(key_mode(key)));
}

/* A segment's value: 1, 2, 3, 4, 6, 8, 12 or 16 sixteenths. */
static const char *ly_duration(const Score *score, const Segment *s) {
    switch (score_sixteenths(score, s->steps)) {
    case 1:
        return "16";
    case 2:
        return "8";
    case 3:
        return "8.";
    case 4:
        return "4";
    case 6:
        return "4.";
    case 8:
        return "2";
    case 12:
        return "2.";
    default:
        return "1";
    }
}

/* c' is middle C. */
static void ly_segment(FILE *f, const Score *score, const Segment *s) {
    const char *duration = ly_duration(score, s);
    if (s->pitch == SOUND_REST) {
        fprintf(f, "r%s", duration);
        return;
    }
    int letter;
    int alter;
    int octave;
    key_spell(s->spelling_key, s->pitch, &letter, &alter, &octave);
    fprintf(f, "%c%s", "cdefgab"[letter], ly_alter(alter));
    for (int o = octave; o > 3; o--) fputc('\'', f);
    for (int o = octave; o < 3; o++) fputc(',', f);
    fprintf(f, "%s%s", duration, s->tie ? "~" : "");
}

bool export_lilypond(const char *path, const Score *score) {
    if (path == NULL || score == NULL || score->voices < 1) return false;
    FILE *f = fopen(path, "wb"); /* the same line endings everywhere */
    if (f == NULL) return false;
    fprintf(f, "\\version \"2.24.0\"\n"
               "\\header { title = \"" PROJECT_TITLE "\" tagline = ##f }\n\n"
               "\\score {\n  \\new StaffGroup <<\n");
    for (int v = 0; v < score->voices; v++) {
        Segment segs[SEGMENT_MAX];
        int n = voice_segments(score, v, segs);
        fprintf(f, "    \\new Staff \\with { instrumentName = \"Voice %d\" } {\n      \\clef %s ",
                v + 1, low_voice(score, v) ? "bass" : "treble");
        ly_key(f, score->key[0]);
        fprintf(f, " \\time 4/4");
        if (v == 0) fprintf(f, " \\tempo 4 = %d", score->tempo > 0 ? score->tempo : 120);
        fprintf(f, "\n     ");
        for (int k = 0; k < n; k++) {
            if (modulates(score) && segs[k].start == score->modulate_at) {
                fprintf(f, " ");
                ly_key(f, score->key[1]);
            }
            fprintf(f, " ");
            ly_segment(f, score, &segs[k]);
            if ((segs[k].start + segs[k].steps) % score_bar_steps(score) == 0)
                fputs(k + 1 < n ? " |\n     " : " \\bar \"|.\"\n", f);
        }
        fprintf(f, "    }\n");
    }
    fprintf(f, "  >>\n  \\layout { }\n  \\midi { }\n}\n");
    return finish(f);
}

static void abc_key(FILE *f, int key) {
    static const char *const modes[MODE_COUNT] = {"", "m", "dor", "phr", "lyd", "mix", "loc"};
    int letter;
    int alter;
    int octave;
    key_spell(key, key_tonic(key), &letter, &alter, &octave);
    fprintf(f, "K:%c%s%s", "CDEFGAB"[letter], alter > 0 ? "#" : alter < 0 ? "b" : "",
            modes[key_mode(key)]);
}

/* The alteration the key signature gives each letter, C..B. */
static void signature(int key, int *alter) {
    static const int sharps[7] = {3, 0, 4, 1, 5, 2, 6}; /* F C G D A E B */
    int fifths = key_fifths(key);
    for (int l = 0; l < 7; l++) alter[l] = 0;
    for (int i = 0; i < fifths && i < 7; i++) alter[sharps[i]] = 1;
    for (int i = 0; i < -fifths && i < 7; i++) alter[sharps[6 - i]] = -1;
}

/* C is middle C. An accidental lasts to the end of the bar, in its own
 * octave or in all of them depending on the reader, so after the first one
 * on a letter every note on that letter in the bar writes its own. */
static void abc_segment(FILE *f, const Segment *s, const int *sig, bool *marked) {
    if (s->pitch == SOUND_REST) {
        fputc('z', f);
    } else {
        static const char *const marks[5] = {"__", "_", "=", "^", "^^"};
        int letter;
        int alter;
        int octave;
        key_spell(s->spelling_key, s->pitch, &letter, &alter, &octave);
        if (alter != sig[letter] || marked[letter]) {
            if (alter >= -2 && alter <= 2) fputs(marks[alter + 2], f);
            marked[letter] = true;
        }
        fputc(octave >= 5 ? "cdefgab"[letter] : "CDEFGAB"[letter], f);
        for (int o = octave; o > 5; o--) fputc('\'', f);
        for (int o = octave; o < 4; o++) fputc(',', f);
    }
    if (s->steps > 1) fprintf(f, "%d", s->steps);
    if (s->tie) fputc('-', f);
}

bool export_abc(const char *path, const Score *score) {
    if (path == NULL || score == NULL || score->voices < 1) return false;
    FILE *f = fopen(path, "wb"); /* the same line endings everywhere */
    if (f == NULL) return false;
    /* the unit note length is one step */
    fprintf(f, "X:1\nT:" PROJECT_TITLE "\nM:4/4\nL:1/%d\nQ:1/4=%d\n", 4 * score_beat_steps(score),
            score->tempo > 0 ? score->tempo : 120);
    for (int v = 0; v < score->voices; v++) {
        fprintf(f, "V:%d clef=%s name=\"Voice %d\"\n", v + 1,
                low_voice(score, v) ? "bass" : "treble", v + 1);
    }
    abc_key(f, score->key[0]);
    fprintf(f, "\n");
    for (int v = 0; v < score->voices; v++) {
        Segment segs[SEGMENT_MAX];
        int n = voice_segments(score, v, segs);
        int sig[7];
        bool marked[7] = {false};
        signature(score->key[0], sig);
        fprintf(f, "V:%d\n", v + 1);
        for (int k = 0; k < n; k++) {
            if (modulates(score) && segs[k].start == score->modulate_at) {
                fputc('[', f);
                abc_key(f, score->key[1]);
                fprintf(f, "] ");
                signature(score->key[1], sig);
            }
            abc_segment(f, &segs[k], sig, marked);
            int end = segs[k].start + segs[k].steps;
            if (end % score_bar_steps(score) != 0) {
                /* no space inside a beat, so the eighths or sixteenths of a
                 * beat are beamed */
                if (end % score_beat_steps(score) == 0) fputc(' ', f);
                continue;
            }
            fputs(k + 1 < n ? " |\n" : " |]\n", f);
            memset(marked, 0, sizeof(marked));
        }
    }
    return finish(f);
}
