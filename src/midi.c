#include "midi.h"

#include "canon.h"
#include "theory.h"

#include <stdio.h>

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

static int write_conductor(FILE *f, const Score *score) {
    long len_pos = begin_track(f);
    if (len_pos < 0) return -1;
    unsigned tempo = 60000000u / (unsigned)(score->tempo > 0 ? score->tempo : 120);
    unsigned char tempo_data[3] = {(unsigned char)(tempo >> 16), (unsigned char)(tempo >> 8),
                                   (unsigned char)tempo};
    unsigned char meter[4] = {4, 2, 24, 8};
    if (write_meta(f, 0, 0x51, tempo_data, 3) != 0 || write_meta(f, 0, 0x58, meter, 4) != 0 ||
        key_signature(f, 0, score->key[0]) != 0)
        return -1;
    unsigned last = 0;
    if (score->nsections > 1) {
        unsigned at = (unsigned)score->modulate_at * step_ticks(score);
        if (key_signature(f, at, score->key[1]) != 0) return -1;
        last = at;
    }
    unsigned end = (unsigned)score->span * step_ticks(score);
    return end_track(f, len_pos, end > last ? end - last : 0);
}

static int write_voice(FILE *f, const Score *score, int v) {
    long len_pos = begin_track(f);
    if (len_pos < 0) return -1;
    char name[16];
    int n = snprintf(name, sizeof(name), "Voice %d", v + 1);
    unsigned char program[3] = {0, (unsigned char)(0xC0 | v), 0};
    if (write_meta(f, 0, 0x03, (const unsigned char *)name, (unsigned)n) != 0 ||
        write_bytes(f, program, 3) != 0)
        return -1;
    unsigned last = 0;
    const ScoreVoice *voice = &score->voice[v];
    for (int k = 0; k < voice->count; k++) {
        const ScoreNote *note = &voice->notes[k];
        if (note->pitch == SOUND_REST) continue;
        unsigned on = (unsigned)note->start * step_ticks(score);
        unsigned off = on + (unsigned)note->length * step_ticks(score);
        unsigned char on_msg[3] = {(unsigned char)(0x90 | v), (unsigned char)note->pitch, 80};
        unsigned char off_msg[3] = {(unsigned char)(0x80 | v), (unsigned char)note->pitch, 0};
        if (write_vlq(f, on - last) != 0 || write_bytes(f, on_msg, 3) != 0 ||
            write_vlq(f, off - on) != 0 || write_bytes(f, off_msg, 3) != 0)
            return -1;
        last = off;
    }
    unsigned end = (unsigned)score->span * step_ticks(score);
    return end_track(f, len_pos, end > last ? end - last : 0);
}

bool midi_write_score(const char *path, const Score *score) {
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
    if (rc == 0) rc = write_conductor(f, score);
    for (int v = 0; v < score->voices && rc == 0; v++) rc = write_voice(f, score, v);
    if (fclose(f) != 0) rc = -1;
    return rc == 0;
}
