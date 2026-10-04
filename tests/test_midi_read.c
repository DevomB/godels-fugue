#include "corpus.h"
#include "midi.h"
#include "midi_read.h"
#include "theory.h"
#include "test_util.h"

/* A MIDI file built byte by byte. */
typedef struct Bytes {
    unsigned char data[512];
    size_t size;
} Bytes;

static void put(Bytes *b, const unsigned char *p, size_t n) {
    CHECK(b->size + n <= sizeof(b->data));
    memcpy(b->data + b->size, p, n);
    b->size += n;
}

#define PUT(b, ...)                                                            \
    do {                                                                       \
        static const unsigned char bytes_[] = {__VA_ARGS__};                   \
        put(b, bytes_, sizeof(bytes_));                                        \
    } while (0)

/* Starts a file with its header chunk; a division above 0x7FFF is SMPTE time. */
static void header(Bytes *b, int format, int ntracks, int division) {
    unsigned char head[14] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, (unsigned char)format,
                              (unsigned char)(ntracks >> 8), (unsigned char)ntracks,
                              (unsigned char)(division >> 8), (unsigned char)division};
    b->size = 0;
    put(b, head, sizeof(head));
}

static void track(Bytes *b, const unsigned char *events, size_t n) {
    unsigned char head[8] = {'M',
                             'T',
                             'r',
                             'k',
                             (unsigned char)(n >> 24),
                             (unsigned char)(n >> 16),
                             (unsigned char)(n >> 8),
                             (unsigned char)n};
    put(b, head, sizeof(head));
    put(b, events, n);
}

static void read_ok(const Bytes *b, MidiFile *f) {
    char err[200];
    if (!midi_read_bytes(f, b->data, b->size, err, sizeof(err))) {
        fprintf(stderr, "midi_read_bytes: %s\n", err);
        exit(1);
    }
}

/* Track k of b spans n steps holding want, and nothing after them. */
static void expect_steps(const Bytes *b, int k, const int *want, int n) {
    MidiFile f;
    read_ok(b, &f);
    int steps[16];
    CHECK(midi_track_steps(&f, k, steps, 16) == n);
    for (int s = 0; s < 16; s++) CHECK(steps[s] == (s < n ? want[s] : PITCH_REST));
    midi_file_free(&f);
}

static void expect_error(const Bytes *b, const char *words) {
    MidiFile f;
    char err[200] = "";
    CHECK(!midi_read_bytes(&f, b->data, b->size, err, sizeof(err)));
    if (strstr(err, words) == NULL) {
        fprintf(stderr, "error \"%s\" lacks \"%s\"\n", err, words);
        exit(1);
    }
    CHECK(f.ntracks == 0 && f.tracks == NULL);
}

static const char *const corpus_dir = "output/tests/midi_corpus";
static const char *const written = "output/tests/midi_corpus/canon.mid";

/* What midi_write_score writes reads back step for step. */
static void test_round_trip(void) {
    Score s;
    memset(&s, 0, sizeof(s));
    s.voices = 2;
    s.span = 8;
    s.tempo = 90;
    s.nsections = 1;
    s.key[0] = key_id(2, MODE_MAJOR);
    /* voice 1: half D, quarter rest, quarter F#, whole A; voice 2 repeats a note */
    ScoreNote lead[] = {{0, 2, 62, 0}, {2, 1, SOUND_REST, -1}, {3, 1, 66, 2}, {4, 4, 69, 3}};
    ScoreNote follow[] = {{0, 4, SOUND_REST, -1}, {4, 2, 62, 0}, {6, 1, 66, 2}, {7, 1, 66, 3}};
    s.voice[0].count = 4;
    memcpy(s.voice[0].notes, lead, sizeof(lead));
    s.voice[1].count = 4;
    memcpy(s.voice[1].notes, follow, sizeof(follow));
    test_mkdir(corpus_dir);
    CHECK(midi_write_score(written, &s));

    MidiFile f;
    char err[200];
    CHECK(midi_read_file(&f, written, err, sizeof(err)));
    CHECK(f.format == 1 && f.ppq == MIDI_PPQ && f.ntracks == 3);
    CHECK(f.tracks[0].count == 0 && midi_first_track(&f) == 1);
    CHECK(f.tracks[1].count == 3 && f.tracks[2].count == 3);
    CHECK(f.tracks[2].notes[0].channel == 1);
    int steps[16];
    static const int lead_steps[8] = {62, 62, PITCH_REST, 66, 69, 69, 69, 69};
    static const int follow_steps[8] = {PITCH_REST, PITCH_REST, PITCH_REST, PITCH_REST,
                                        62,         62,         66,         66};
    CHECK(midi_track_steps(&f, 1, steps, 16) == 8);
    for (int i = 0; i < 16; i++) CHECK(steps[i] == (i < 8 ? lead_steps[i] : PITCH_REST));
    CHECK(midi_track_steps(&f, 2, steps, 16) == 8);
    for (int i = 0; i < 8; i++) CHECK(steps[i] == follow_steps[i]);
    /* the conductor has no notes, and a short buffer still learns the span */
    CHECK(midi_track_steps(&f, 0, steps, 16) == 0);
    CHECK(midi_track_steps(&f, 1, steps, 3) == 8);
    CHECK(steps[0] == 62 && steps[1] == 62 && steps[2] == PITCH_REST);
    CHECK(midi_track_steps(&f, 3, steps, 16) == 0);
    midi_file_free(&f);
    CHECK(f.ntracks == 0 && f.tracks == NULL);

    CHECK(!midi_read_file(&f, "output/tests/none.mid", err, sizeof(err)));
    CHECK(strstr(err, "cannot read MIDI file") != NULL);

    /* a corpus counts the pitch class of every note of a MIDI file */
    int counts[12] = {0};
    CHECK(corpus_midi_counts(written, counts) == 6);
    CHECK(counts[2] == 2 && counts[6] == 3 && counts[9] == 1);
    memset(counts, 0, sizeof(counts));
    CHECK(corpus_dir_counts(corpus_dir, counts) == 1);
    CHECK(counts[2] == 2 && counts[6] == 3 && counts[9] == 1);
}

/* An eighth-grid score reads back step for step on the eighth grid, and
 * at half the steps, rounded, on the quarter grid. */
static void test_eighth_grid(void) {
    Score s;
    memset(&s, 0, sizeof(s));
    s.voices = 1;
    s.span = 8;
    s.tempo = 90;
    s.beat_steps = 2;
    s.nsections = 1;
    s.key[0] = key_id(0, MODE_MAJOR);
    /* eighth C, eighth D, quarter rest, dotted quarter E, eighth F */
    ScoreNote lead[] = {{0, 1, 60, 0}, {1, 1, 62, 1}, {2, 2, SOUND_REST, -1}, {4, 3, 64, 4},
                        {7, 1, 65, 7}};
    s.voice[0].count = 5;
    memcpy(s.voice[0].notes, lead, sizeof(lead));
    CHECK(midi_write_score("output/tests/eighths_read.mid", &s));
    MidiFile f;
    char err[200];
    CHECK(midi_read_file(&f, "output/tests/eighths_read.mid", err, sizeof(err)));
    int steps[16];
    static const int want[8] = {60, 62, PITCH_REST, PITCH_REST, 64, 64, 64, 65};
    CHECK(midi_track_grid(&f, 1, 2, steps, 16) == 8);
    for (int i = 0; i < 16; i++) CHECK(steps[i] == (i < 8 ? want[i] : PITCH_REST));
    /* on quarters each onset and end rounds to the nearest quarter, and a
     * note keeps one step at least: D and F start half a quarter late */
    static const int quarters[5] = {60, 62, 64, 64, 65};
    CHECK(midi_track_grid(&f, 1, 1, steps, 16) == 5);
    for (int i = 0; i < 5; i++) CHECK(steps[i] == quarters[i]);
    CHECK(midi_track_steps(&f, 1, steps, 16) == 5);
    for (int i = 0; i < 5; i++) CHECK(steps[i] == quarters[i]);
    CHECK(midi_track_grid(&f, 1, 0, steps, 16) == 0);
    midi_file_free(&f);
}

static void test_quantize(void) {
    Bytes b;
    header(&b, 0, 1, 96);
    /* onsets and ends off the grid; 64 starts after 67 but is lower, so
     * 67 holds step 3; 72 is shorter than a step and still takes one; MIDI
     * note 0, which would read as a rest, does not lengthen the melody */
    static const unsigned char events[] = {
        5,   0x90, 60, 80, 90, 0x80, 60, 0,  5,  0x90, 62, 80, 85,   0x80, 62,   0,
        105, 0x90, 67, 80, 10, 0x90, 64, 80, 80, 0x80, 64, 0,  90,   0x80, 67,   0,
        10,  0x90, 72, 80, 10, 0x80, 72, 64, 10, 0x90, 0,  80, 0x7F, 0x80, 0,    0,
        0,   0xFF, 0x2F, 0};
    track(&b, events, sizeof(events));
    static const int want[6] = {60, 62, PITCH_REST, 67, 67, 72};
    expect_steps(&b, 0, want, 6);
}

static void test_running_status(void) {
    Bytes b;
    header(&b, 1, 2, 480);
    static const unsigned char tempo[] = {0, 0xFF, 0x51, 3, 0x07, 0xA1, 0x20, 0, 0xFF, 0x2F, 0};
    track(&b, tempo, sizeof(tempo));
    /* one note-on status for every note, velocity 0 for each end, and a
     * text event that leaves running status alone; 480 and 960 ticks take
     * two bytes */
    static const unsigned char notes[] = {
        0, 0xC0, 5,  0, 0x90, 60, 80, 0x83, 0x60, 60,   0,    0, 62, 80, 0x83, 0x60, 62, 0,
        0, 0xFF, 1,  2, 'h',  'i', 0, 64,   80,   0x87, 0x40, 64, 0, 0,  0xFF, 0x2F, 0};
    track(&b, notes, sizeof(notes));
    MidiFile f;
    read_ok(&b, &f);
    CHECK(f.ntracks == 2 && midi_first_track(&f) == 1 && f.tracks[1].count == 3);
    midi_file_free(&f);
    static const int want[4] = {60, 62, 64, 64};
    expect_steps(&b, 1, want, 4);
}

static void test_format0(void) {
    Bytes b;
    header(&b, 0, 1, 480);
    PUT(&b, 'X', 'F', 'I', 'H', 0, 0, 0, 3, 1, 2, 3); /* a chunk of another type */
    static const unsigned char events[] = {
        0,    0xFF, 0x51, 3,   0x07, 0xA1, 0x20, /* tempo */
        0,    0xF0, 5,    0x7E, 0x7F, 0x09, 0x01, 0xF7, /* sysex */
        0,    0xB2, 7,    100, /* volume */
        0,    0x99, 36,   100, /* a drum, on channel 10 */
        0,    0x92, 60,   80,
        0,    0xE2, 0,    64, /* pitch bend */
        0,    0xD2, 64, /* channel pressure */
        0x83, 0x60, 0x82, 60,  64,
        0,    0x92, 67,   80, /* never released */
        0,    0x89, 36,   0,
        0x87, 0x40, 0xFF, 0x2F, 0};
    track(&b, events, sizeof(events));
    MidiFile f;
    read_ok(&b, &f);
    CHECK(f.format == 0 && f.ntracks == 1 && f.tracks[0].count == 2);
    CHECK(f.tracks[0].notes[0].channel == 2 && f.tracks[0].notes[0].off == MIDI_PPQ);
    CHECK(f.tracks[0].notes[1].off == 3 * MIDI_PPQ);
    midi_file_free(&f);
    static const int want[3] = {60, 67, 67};
    expect_steps(&b, 0, want, 3);
}

static void test_malformed(void) {
    Bytes b;
    static const unsigned char end[] = {0, 0xFF, 0x2F, 0};

    header(&b, 1, 1, 480);
    b.data[3] = 'x';
    track(&b, end, sizeof(end));
    expect_error(&b, "MThd");
    header(&b, 1, 1, 480);
    b.size = 10;
    expect_error(&b, "MThd");
    header(&b, 1, 1, 480);
    b.data[7] = 5;
    track(&b, end, sizeof(end));
    expect_error(&b, "header length");
    header(&b, 3, 1, 480);
    track(&b, end, sizeof(end));
    expect_error(&b, "format 3");
    header(&b, 1, 1, 0xE728);
    track(&b, end, sizeof(end));
    expect_error(&b, "SMPTE");
    header(&b, 1, 1, 0);
    track(&b, end, sizeof(end));
    expect_error(&b, "ticks per quarter");
    header(&b, 1, 0, 480);
    track(&b, end, sizeof(end));
    expect_error(&b, "no tracks");
    header(&b, 1, 2, 480);
    track(&b, end, sizeof(end));
    expect_error(&b, "2 tracks in the header, room for 1");
    header(&b, 1, 2, 480);
    track(&b, end, sizeof(end));
    PUT(&b, 'X', 'F', 'I', 'H', 0, 0, 0, 0);
    expect_error(&b, "ends before track 2");

    /* a chunk longer than the file, by a little and by a lot */
    header(&b, 1, 1, 480);
    track(&b, end, sizeof(end));
    b.data[21] = 5;
    expect_error(&b, "chunk at byte 14 runs past the end");
    b.data[18] = b.data[19] = b.data[20] = b.data[21] = 0xFF;
    expect_error(&b, "chunk at byte 14 runs past the end");

    static const unsigned char vlq[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x90, 60, 80};
    header(&b, 1, 1, 480);
    track(&b, vlq, sizeof(vlq));
    expect_error(&b, "track 1: variable-length number longer than four bytes at byte 22");
    static const unsigned char orphan[] = {0, 60, 80, 0, 0xFF, 0x2F, 0};
    header(&b, 1, 1, 480);
    track(&b, orphan, sizeof(orphan));
    expect_error(&b, "without a status");
    static const unsigned char cut[] = {0, 0x90, 60};
    header(&b, 1, 1, 480);
    track(&b, cut, sizeof(cut));
    expect_error(&b, "truncated event");
    static const unsigned char cut_delta[] = {0, 0x90, 60, 80, 0x83};
    header(&b, 1, 1, 480);
    track(&b, cut_delta, sizeof(cut_delta));
    expect_error(&b, "truncated event at byte 26");
    static const unsigned char meta[] = {0, 0xFF, 0x01, 0x7F, 'a'};
    header(&b, 1, 1, 480);
    track(&b, meta, sizeof(meta));
    expect_error(&b, "truncated event");
    static const unsigned char clock[] = {0, 0xF8, 0, 0xFF, 0x2F, 0};
    header(&b, 1, 1, 480);
    track(&b, clock, sizeof(clock));
    expect_error(&b, "system message");
    static const unsigned char high[] = {0, 0x90, 60, 0x80, 0, 0xFF, 0x2F, 0};
    header(&b, 1, 1, 480);
    track(&b, high, sizeof(high));
    expect_error(&b, "above 127");
    /* five of the longest delta times run past the latest tick read */
    header(&b, 1, 1, 480);
    PUT(&b, 'M', 'T', 'r', 'k', 0, 0, 0, 35);
    for (int i = 0; i < 5; i++) PUT(&b, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0x01, 0);
    expect_error(&b, "2^30 ticks");

    /* a track without an end-of-track event still reads */
    static const unsigned char open_end[] = {0, 0x90, 60, 80};
    header(&b, 1, 1, 480);
    track(&b, open_end, sizeof(open_end));
    static const int one[1] = {60};
    expect_steps(&b, 0, one, 1);
}

/* A note-off ends the oldest sounding note of its pitch and channel. */
static void test_repeated_pitch(void) {
    Bytes b;
    header(&b, 0, 1, 480);
    /* a repeated note whose next note-on comes before the note-off at the
     * same tick: two quarters, no rest between them */
    static const unsigned char same_tick[] = {
        0, 0x90, 60, 80, 0x83, 0x60, 0x90, 60, 80, 0, 0x80, 60, 0,
        0x83, 0x60, 0x80, 60, 0, 0, 0xFF, 0x2F, 0};
    track(&b, same_tick, sizeof(same_tick));
    MidiFile f;
    read_ok(&b, &f);
    CHECK(f.tracks[0].count == 2);
    CHECK(f.tracks[0].notes[0].on == 0 && f.tracks[0].notes[0].off == 480);
    CHECK(f.tracks[0].notes[1].on == 480 && f.tracks[0].notes[1].off == 960);
    midi_file_free(&f);
    static const int two[2] = {60, 60};
    expect_steps(&b, 0, two, 2);

    /* two overlapping notes of one pitch, the first held two beats and the
     * second from beat two to beat four; a third never released */
    header(&b, 0, 1, 480);
    static const unsigned char overlap[] = {
        0, 0x90, 62, 80, 0x83, 0x60, 0x90, 62, 80, 0x83, 0x60, 0x80, 62, 0,
        0x83, 0x60, 0x90, 62, 0, 0, 0x90, 62, 80, 0x83, 0x60, 0xFF, 0x2F, 0};
    track(&b, overlap, sizeof(overlap));
    read_ok(&b, &f);
    CHECK(f.tracks[0].count == 3);
    CHECK(f.tracks[0].notes[0].off == 960 && f.tracks[0].notes[1].off == 1440);
    CHECK(f.tracks[0].notes[2].on == 1440 && f.tracks[0].notes[2].off == 1920);
    midi_file_free(&f);
    static const int four[4] = {62, 62, 62, 62};
    expect_steps(&b, 0, four, 4);
}

/* Writes a file of size bytes: a one-note file padded by a chunk of another
 * type. */
static void write_padded(const char *path, long size) {
    Bytes b;
    header(&b, 0, 1, 480);
    static const unsigned char note[] = {0,  0x90, 60, 80, 0x83, 0x60, 0x80,
                                         60, 0,    0,  0xFF, 0x2F, 0};
    track(&b, note, sizeof(note));
    unsigned long pad = (unsigned long)size - (unsigned long)b.size - 8;
    unsigned char head[8] = {'X',
                             'F',
                             'I',
                             'H',
                             (unsigned char)(pad >> 24),
                             (unsigned char)(pad >> 16),
                             (unsigned char)(pad >> 8),
                             (unsigned char)pad};
    FILE *out = fopen(path, "wb");
    CHECK(out != NULL);
    CHECK(fwrite(b.data, 1, 14, out) == 14);
    CHECK(fwrite(head, 1, 8, out) == 8);
    for (unsigned long k = 0; k < pad; k++) CHECK(fputc(0, out) != EOF);
    CHECK(fwrite(b.data + 14, 1, b.size - 14, out) == b.size - 14);
    CHECK(fclose(out) == 0);
}

/* A file of exactly MIDI_FILE_LIMIT bytes reads; one byte more does not. */
static void test_file_limit(void) {
    const char *path = "output/tests/limit.mid";
    MidiFile f;
    char err[200];
    write_padded(path, MIDI_FILE_LIMIT);
    CHECK(midi_read_file(&f, path, err, sizeof(err)));
    CHECK(f.ntracks == 1 && f.tracks[0].count == 1);
    midi_file_free(&f);
    write_padded(path, MIDI_FILE_LIMIT + 1L);
    CHECK(!midi_read_file(&f, path, err, sizeof(err)));
    CHECK(strstr(err, "too large") != NULL);
    remove(path);
}

/* Every truncated copy of a valid file fails, and corrupted copies either
 * fail with a message or read as some file; none may crash or read out of
 * bounds (the sanitizer build checks the latter). */
static void test_fuzz(void) {
    long size = 0;
    unsigned char *good = (unsigned char *)test_slurp(written, &size);
    CHECK(size > 14);
    unsigned char *copy = (unsigned char *)malloc((size_t)size);
    CHECK(copy != NULL);
    MidiFile f;
    char err[200];
    for (long n = 0; n < size; n++) {
        memcpy(copy, good, (size_t)n);
        err[0] = '\0';
        CHECK(!midi_read_bytes(&f, copy, (size_t)n, err, sizeof(err)));
        CHECK(err[0] != '\0');
    }
    static const unsigned char values[] = {0x00, 0x01, 0x7F, 0x80, 0xFF};
    unsigned seed = 1;
    int read = 0;
    int failed = 0;
    for (long i = 0; i < size; i++) {
        for (int v = 0; v < 7; v++) {
            memcpy(copy, good, (size_t)size);
            if (v < 5) {
                copy[i] = values[v];
            } else {
                /* this byte and another, at random */
                seed = seed * 1103515245u + 12345u;
                copy[i] = (unsigned char)(seed >> 16);
                seed = seed * 1103515245u + 12345u;
                unsigned j = (seed >> 16) % (unsigned)size;
                copy[j] = (unsigned char)(copy[j] ^ (1u << (seed % 8)));
            }
            err[0] = '\0';
            if (midi_read_bytes(&f, copy, (size_t)size, err, sizeof(err))) {
                for (int k = 0; k < f.ntracks; k++) {
                    int steps[MELODY_MAX];
                    CHECK(midi_track_steps(&f, k, steps, MELODY_MAX) >= 0);
                    for (int s = 0; s < MELODY_MAX; s++) CHECK(steps[s] >= 0 && steps[s] <= 127);
                }
                midi_file_free(&f);
                read++;
            } else {
                CHECK(err[0] != '\0' && f.tracks == NULL);
                failed++;
            }
        }
    }
    CHECK(read > 0 && failed > 0);
    free(copy);
    free(good);
}

int main(void) {
    test_output_dir();
    test_round_trip();
    test_eighth_grid();
    test_quantize();
    test_running_status();
    test_format0();
    test_malformed();
    test_repeated_pitch();
    test_file_limit();
    test_fuzz();
    printf("ok\n");
    return 0;
}
