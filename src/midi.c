#include "midi.h"

#include "canon.h"
#include "parts.h"
#include "theory.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int write_u16be(FILE *f, unsigned v) {
    unsigned char b[2] = {(unsigned char)((v >> 8) & 0xFF), (unsigned char)(v & 0xFF)};
    return fwrite(b, 1, 2, f) == 2 ? 0 : -1;
}

static int write_u32be(FILE *f, unsigned v) {
    unsigned char b[4] = {(unsigned char)((v >> 24) & 0xFF), (unsigned char)((v >> 16) & 0xFF),
                          (unsigned char)((v >> 8) & 0xFF), (unsigned char)(v & 0xFF)};
    return fwrite(b, 1, 4, f) == 4 ? 0 : -1;
}

static int write_vlq(FILE *f, unsigned value) {
    unsigned long buffer = value & 0x7Fu;
    while ((value >>= 7) > 0) {
        buffer <<= 8;
        buffer |= 0x80u;
        buffer |= (value & 0x7Fu);
    }
    for (;;) {
        if (fputc((int)(buffer & 0xFFu), f) == EOF) return -1;
        if (!(buffer & 0x80u)) return 0;
        buffer >>= 8;
    }
}

static int write_bytes(FILE *f, const unsigned char *bytes, size_t n) {
    if (n == 0) return 0;
    return fwrite(bytes, 1, n, f) == n ? 0 : -1;
}

static int write_meta(FILE *f, unsigned delta, unsigned char type, const unsigned char *data,
                      unsigned len) {
    unsigned char head[2] = {0xFF, type};
    if (write_vlq(f, delta) != 0 || write_bytes(f, head, 2) != 0 || write_vlq(f, len) != 0)
        return -1;
    return write_bytes(f, data, len);
}

/* Writes the "MTrk" header with a zero length and returns where the
 * length goes, so end_track can patch it. */
static long begin_track(FILE *f) {
    if (fwrite("MTrk", 1, 4, f) != 4) return -1;
    long len_pos = ftell(f);
    if (len_pos < 0 || write_u32be(f, 0) != 0) return -1;
    return len_pos;
}

static int end_track(FILE *f, long len_pos, unsigned delta) {
    if (write_meta(f, delta, 0x2F, NULL, 0) != 0) return -1;
    long end = ftell(f);
    if (end < 0) return -1;
    if (fseek(f, len_pos, SEEK_SET) != 0) return -1;
    if (write_u32be(f, (unsigned)(end - len_pos - 4)) != 0) return -1;
    return fseek(f, end, SEEK_SET) == 0 ? 0 : -1;
}

/* Ticks in one step: a quarter note, or an eighth or a sixteenth on the
 * finer grids. */
static unsigned step_ticks(const Score *score) {
    return (unsigned)(MIDI_PPQ / score_beat_steps(score));
}

static int key_signature(FILE *f, unsigned delta, int key) {
    unsigned char data[2];
    data[0] = (unsigned char)(signed char)key_fifths(key);
    data[1] = key_mode(key) == MODE_MINOR ? 1 : 0;
    return write_meta(f, delta, 0x59, data, 2);
}

static int tempo_event(FILE *f, unsigned delta, unsigned us_per_quarter) {
    unsigned char data[3] = {(unsigned char)(us_per_quarter >> 16),
                             (unsigned char)(us_per_quarter >> 8), (unsigned char)us_per_quarter};
    return write_meta(f, delta, 0x51, data, 3);
}

/* Microseconds per quarter note at step s: the performance's time map, or
 * the written tempo. */
static unsigned step_tempo(const Score *score, const Performance *perf, int s) {
    if (perf == NULL) return 60000000u / (unsigned)(score->tempo > 0 ? score->tempo : 120);
    if (s >= score->span) s = score->span - 1;
    if (s < 0) s = 0;
    double step = perf->times[s + 1] - perf->times[s];
    return (unsigned)lround(step * score_beat_steps(score) * 1e6);
}

/* The tick a time in seconds falls on under the time map; past the last
 * step the last step's tempo carries on (the held final chord). */
static unsigned ticks_at(const Score *score, const Performance *perf, double seconds) {
    double st = step_ticks(score);
    int span = score->span;
    for (int s = 0; s < span; s++) {
        double a = perf->times[s];
        double b = perf->times[s + 1];
        if (seconds < b) {
            double u = b > a ? (seconds - a) / (b - a) : 0.0;
            return (unsigned)lround((s + (u > 0.0 ? u : 0.0)) * st);
        }
    }
    double last = perf->times[span] - perf->times[span - 1];
    double extra = last > 0.0 ? (seconds - perf->times[span]) / last : 0.0;
    return (unsigned)lround((span + extra) * st);
}

/* The last tick any note sounds to. */
static unsigned end_tick(const Score *score, const Performance *perf) {
    unsigned end = (unsigned)score->span * step_ticks(score);
    if (perf != NULL) {
        unsigned held = ticks_at(score, perf, perf->times[score->span] + perf->hold);
        if (held > end) end = held;
    }
    return end;
}

static int write_conductor(FILE *f, const Score *score, const Performance *perf) {
    long len_pos = begin_track(f);
    if (len_pos < 0) return -1;
    unsigned char meter[4] = {4, 2, 24, 8};
    unsigned tempo = step_tempo(score, perf, 0);
    if (tempo_event(f, 0, tempo) != 0 || write_meta(f, 0, 0x58, meter, 4) != 0 ||
        key_signature(f, 0, score->key[0]) != 0)
        return -1;
    unsigned last = 0;
    /* a tempo change wherever the performance bends the time, and the key
     * change where the piece modulates */
    for (int s = 1; s < score->span; s++) {
        unsigned at = (unsigned)s * step_ticks(score);
        if (score->nsections > 1 && s == score->modulate_at) {
            if (key_signature(f, at - last, score->key[1]) != 0) return -1;
            last = at;
        }
        unsigned now = step_tempo(score, perf, s);
        if (now == tempo) continue;
        if (tempo_event(f, at - last, now) != 0) return -1;
        tempo = now;
        last = at;
    }
    unsigned end = end_tick(score, perf);
    return end_track(f, len_pos, end > last ? end - last : 0);
}

static int write_voice(FILE *f, const Score *score, int v, const Performance *perf) {
    long len_pos = begin_track(f);
    if (len_pos < 0) return -1;
    /* the instrument's name and General MIDI program, its volume and pan;
     * MIDI is always at sounding pitch, whatever the part is written in */
    char name[40];
    score_part_name(score, v, name, sizeof(name));
    const Part *part = score_part(score, v);
    unsigned char program[3] = {0, (unsigned char)(0xC0 | v),
                                (unsigned char)(part != NULL ? part->program : 0)};
    if (write_meta(f, 0, 0x03, (const unsigned char *)name, (unsigned)strlen(name)) != 0 ||
        write_bytes(f, program, 3) != 0)
        return -1;
    if (perf != NULL) {
        int pan = 64 + (int)lround(perf->pan[v] * 63.0 / 100.0);
        /* two controller events, each after a delta time of 0 */
        unsigned char mix[8] = {0, (unsigned char)(0xB0 | v), 7,  (unsigned char)perf->volume[v],
                                0, (unsigned char)(0xB0 | v), 10, (unsigned char)pan};
        if (write_bytes(f, mix, sizeof(mix)) != 0) return -1;
    }
    unsigned last = 0;
    const ScoreVoice *voice = &score->voice[v];
    for (int k = 0; k < voice->count; k++) {
        const ScoreNote *note = &voice->notes[k];
        if (note->pitch == SOUND_REST) continue;
        unsigned on = (unsigned)note->start * step_ticks(score);
        unsigned off = on + (unsigned)note->length * step_ticks(score);
        int velocity = 80;
        if (perf != NULL) {
            double t_on;
            double t_off;
            perform_span(perf, score, v, k, false, &t_on, &t_off);
            off = ticks_at(score, perf, t_off);
            velocity = perf->note[v][k].velocity;
            /* one voice is one line: a legato note ends where the next begins */
            for (int j = k + 1; j < voice->count; j++) {
                if (voice->notes[j].pitch == SOUND_REST) continue;
                unsigned next = (unsigned)voice->notes[j].start * step_ticks(score);
                if (off > next) off = next;
                break;
            }
            if (off <= on) off = on + 1;
        }
        unsigned char on_msg[3] = {(unsigned char)(0x90 | v), (unsigned char)note->pitch,
                                   (unsigned char)velocity};
        unsigned char off_msg[3] = {(unsigned char)(0x80 | v), (unsigned char)note->pitch, 0};
        if (write_vlq(f, on - last) != 0 || write_bytes(f, on_msg, 3) != 0 ||
            write_vlq(f, off - on) != 0 || write_bytes(f, off_msg, 3) != 0)
            return -1;
        last = off;
    }
    unsigned end = end_tick(score, perf);
    return end_track(f, len_pos, end > last ? end - last : 0);
}

bool midi_write_performed(const char *path, const Score *score, const Performance *perf) {
    if (path == NULL || score == NULL || score->voices < 1 || score->voices > 15) return false;
    for (int v = 0; v < score->voices; v++) {
        for (int k = 0; k < score->voice[v].count; k++) {
            int p = score->voice[v].notes[k].pitch;
            if (p != SOUND_REST && (p < 0 || p > 127)) return false;
        }
    }
    FILE *f = fopen(path, "wb");
    if (f == NULL) return false;
    int rc = 0;
    if (fwrite("MThd", 1, 4, f) != 4 || write_u32be(f, 6) != 0 || write_u16be(f, 1) != 0 ||
        write_u16be(f, (unsigned)score->voices + 1) != 0 || write_u16be(f, MIDI_PPQ) != 0)
        rc = -1;
    if (rc == 0) rc = write_conductor(f, score, perf);
    for (int v = 0; v < score->voices && rc == 0; v++) rc = write_voice(f, score, v, perf);
    if (fclose(f) != 0) rc = -1;
    return rc == 0;
}

bool midi_write_score(const char *path, const Score *score) {
    return midi_write_performed(path, score, NULL);
}
