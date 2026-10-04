#include "midi.h"
#include "theory.h"
#include "test_util.h"

typedef struct Note {
    int on;
    int off;
    int pitch;
    int channel;
} Note;

typedef struct Track {
    int nnotes;
    Note notes[64];
    int tempo;     /* microseconds per quarter, or -1 */
    int fifths[4]; /* key signatures in order */
    int nkeys;
    int key_tick[4];
    char name[32];
} Track;

static unsigned read_be(const unsigned char *p, int n) {
    unsigned v = 0;
    for (int i = 0; i < n; i++) v = (v << 8) | p[i];
    return v;
}

static unsigned read_vlq(const unsigned char *p, size_t *pos) {
    unsigned v = 0;
    for (;;) {
        unsigned char b = p[(*pos)++];
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) return v;
    }
}

static void parse_track(const unsigned char *p, size_t len, Track *t) {
    memset(t, 0, sizeof(*t));
    t->tempo = -1;
    size_t pos = 0;
    int tick = 0;
    int open_pitch[128];
    for (int i = 0; i < 128; i++) open_pitch[i] = -1;
    while (pos < len) {
        tick += (int)read_vlq(p, &pos);
        unsigned char status = p[pos++];
        if (status == 0xFF) {
            unsigned char type = p[pos++];
            unsigned n = read_vlq(p, &pos);
            if (type == 0x51) t->tempo = (int)read_be(p + pos, 3);
            if (type == 0x59 && t->nkeys < 4) {
                t->key_tick[t->nkeys] = tick;
                t->fifths[t->nkeys++] = (signed char)p[pos];
            }
            if (type == 0x03 && n < sizeof(t->name)) memcpy(t->name, p + pos, n);
            pos += n;
            if (type == 0x2F) return;
            continue;
        }
        unsigned char kind = status & 0xF0;
        if (kind == 0xC0) {
            pos++;
            continue;
        }
        CHECK(kind == 0x90 || kind == 0x80);
        int pitch = p[pos++];
        int vel = p[pos++];
        if (kind == 0x90 && vel > 0) {
            CHECK(t->nnotes < 64);
            open_pitch[pitch] = t->nnotes;
            t->notes[t->nnotes].on = tick;
            t->notes[t->nnotes].pitch = pitch;
            t->notes[t->nnotes].channel = status & 0x0F;
            t->nnotes++;
        } else {
            CHECK(open_pitch[pitch] >= 0);
            t->notes[open_pitch[pitch]].off = tick;
            open_pitch[pitch] = -1;
        }
    }
    CHECK(0); /* no end-of-track event */
}

static int read_file(const char *path, Track *tracks, int max_tracks) {
    long size = 0;
    unsigned char *buf = (unsigned char *)test_slurp(path, &size);
    CHECK(size > 14);
    CHECK(memcmp(buf, "MThd", 4) == 0);
    CHECK(read_be(buf + 4, 4) == 6);
    CHECK(read_be(buf + 8, 2) == 1);
    CHECK(read_be(buf + 12, 2) == MIDI_PPQ);
    int ntrks = (int)read_be(buf + 10, 2);
    CHECK(ntrks <= max_tracks);
    size_t pos = 14;
    for (int k = 0; k < ntrks; k++) {
        CHECK(memcmp(buf + pos, "MTrk", 4) == 0);
        unsigned len = read_be(buf + pos + 4, 4);
        CHECK(pos + 8 + len <= (size_t)size);
        parse_track(buf + pos + 8, len, &tracks[k]);
        pos += 8 + len;
    }
    CHECK(pos == (size_t)size);
    free(buf);
    return ntrks;
}

int main(void) {
    test_output_dir();
    Score s;
    memset(&s, 0, sizeof(s));
    s.voices = 2;
    s.span = 8;
    s.tempo = 90;
    s.nsections = 2;
    s.key[0] = key_id(0, MODE_MAJOR);
    s.key[1] = key_id(9, MODE_MINOR);
    s.modulate_at = 4;
    /* voice 1: half C, quarter rest, quarter E, whole G */
    ScoreNote lead[] = {{0, 2, 60, 0}, {2, 1, SOUND_REST, -1}, {3, 1, 64, 2}, {4, 4, 67, 3}};
    ScoreNote follow[] = {{0, 4, SOUND_REST, -1}, {4, 2, 60, 0}, {6, 2, 64, 2}};
    s.voice[0].count = 4;
    memcpy(s.voice[0].notes, lead, sizeof(lead));
    s.voice[1].count = 3;
    memcpy(s.voice[1].notes, follow, sizeof(follow));
    CHECK(midi_write_score("output/tests/canon.mid", &s));

    Track tracks[4];
    CHECK(read_file("output/tests/canon.mid", tracks, 4) == 3);
    const Track *conductor = &tracks[0];
    CHECK(conductor->nnotes == 0);
    CHECK(conductor->tempo == 60000000 / 90);
    CHECK(conductor->nkeys == 2);
    CHECK(conductor->fifths[0] == 0 && conductor->key_tick[0] == 0);
    CHECK(conductor->fifths[1] == 0 && conductor->key_tick[1] == 4 * MIDI_PPQ);

    const Track *v1 = &tracks[1];
    CHECK(strcmp(v1->name, "Voice 1") == 0);
    CHECK(v1->nnotes == 3);
    CHECK(v1->notes[0].on == 0 && v1->notes[0].off == 2 * MIDI_PPQ);
    CHECK(v1->notes[1].pitch == 64 && v1->notes[1].on == 3 * MIDI_PPQ);
    CHECK(v1->notes[2].off - v1->notes[2].on == 4 * MIDI_PPQ);
    CHECK(v1->notes[0].channel == 0);
    const Track *v2 = &tracks[2];
    CHECK(v2->nnotes == 2);
    CHECK(v2->notes[0].on == 4 * MIDI_PPQ && v2->notes[0].channel == 1);

    /* on the eighth grid a step is half a quarter */
    Score e;
    memset(&e, 0, sizeof(e));
    e.voices = 1;
    e.span = 8;
    e.tempo = 90;
    e.beat_steps = 2;
    e.nsections = 2;
    e.key[0] = key_id(0, MODE_MAJOR);
    e.key[1] = key_id(7, MODE_MAJOR);
    e.modulate_at = 4;
    /* eighth C, dotted quarter E, half G */
    ScoreNote run[] = {{0, 1, 60, 0}, {1, 3, 64, 1}, {4, 4, 67, 4}};
    e.voice[0].count = 3;
    memcpy(e.voice[0].notes, run, sizeof(run));
    CHECK(midi_write_score("output/tests/eighths.mid", &e));
    CHECK(read_file("output/tests/eighths.mid", tracks, 4) == 2);
    CHECK(tracks[0].nkeys == 2 && tracks[0].key_tick[1] == 2 * MIDI_PPQ);
    CHECK(tracks[0].fifths[1] == 1);
    const Track *eighths = &tracks[1];
    CHECK(eighths->nnotes == 3);
    CHECK(eighths->notes[0].on == 0 && eighths->notes[0].off == MIDI_PPQ / 2);
    CHECK(eighths->notes[1].on == MIDI_PPQ / 2 && eighths->notes[1].off == 2 * MIDI_PPQ);
    CHECK(eighths->notes[2].on == 2 * MIDI_PPQ && eighths->notes[2].off == 4 * MIDI_PPQ);

    s.voice[0].notes[0].pitch = 200;
    CHECK(!midi_write_score("output/tests/bad.mid", &s));
    printf("ok\n");
    return 0;
}
