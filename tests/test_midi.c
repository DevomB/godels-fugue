#include "midi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define CHECK(cond, msg)                                                       \
	do {                                                                   \
		if (!(cond)) {                                                 \
			fprintf(stderr, "FAIL: %s\n", (msg));                  \
			exit(1);                                               \
		}                                                              \
	} while (0)

static unsigned int read_u16be(const unsigned char *p)
{
	return ((unsigned int)p[0] << 8) | (unsigned int)p[1];
}

static unsigned int read_u32be(const unsigned char *p)
{
	return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
	       ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

static int read_vlq(const unsigned char *p, size_t len, size_t *pos,
		    unsigned int *out)
{
	unsigned int value = 0;
	int bytes = 0;
	for (;;) {
		if (*pos >= len || bytes >= 4)
			return -1;
		unsigned char b = p[(*pos)++];
		bytes++;
		value = (value << 7) | (unsigned int)(b & 0x7F);
		if ((b & 0x80) == 0)
			break;
	}
	*out = value;
	return 0;
}

typedef struct {
	int tick;
	int pitch;
	int channel;
	int velocity;
} NoteOn;

static int parse_track_note_ons(const unsigned char *p, size_t len,
				NoteOn *ons, int max_ons, int *n_ons)
{
	size_t pos = 0;
	int tick = 0;
	*n_ons = 0;

	while (pos < len) {
		unsigned int delta;
		if (read_vlq(p, len, &pos, &delta) != 0)
			return -1;
		tick += (int)delta;
		if (pos >= len)
			return -1;

		unsigned char status = p[pos++];
		if (status == 0xFF) {
			if (pos + 1 >= len)
				return -1;
			unsigned char type = p[pos++];
			unsigned int meta_len;
			if (read_vlq(p, len, &pos, &meta_len) != 0)
				return -1;
			if (pos + meta_len > len)
				return -1;
			pos += meta_len;
			if (type == 0x2F)
				return 0;
			continue;
		}

		int channel = status & 0x0F;
		unsigned char kind = status & 0xF0;

		if (kind == 0xC0) {
			if (pos >= len)
				return -1;
			pos++; /* program */
			continue;
		}
		if (kind == 0x90 || kind == 0x80) {
			if (pos + 1 >= len)
				return -1;
			int pitch = p[pos++];
			int vel = p[pos++];
			if (kind == 0x90 && vel > 0) {
				if (*n_ons >= max_ons)
					return -1;
				ons[*n_ons].tick = tick;
				ons[*n_ons].pitch = pitch;
				ons[*n_ons].channel = channel;
				ons[*n_ons].velocity = vel;
				(*n_ons)++;
			}
			continue;
		}
		return -1;
	}
	return -1;
}

int main(void)
{
#ifdef _WIN32
	_mkdir("output");
#else
	mkdir("output", 0755);
#endif

	const int melody[] = {60, 64, 67, 72};
	const int length = 4;
	const int delay = 2;
	const char *path = "output/test_canon.mid";

	CHECK(midi_write_canon(path, melody, melody, length, delay),
	      "write failed");

	FILE *f = fopen(path, "rb");
	CHECK(f != NULL, "open output");
	CHECK(fseek(f, 0, SEEK_END) == 0, "seek end");
	long sz = ftell(f);
	CHECK(sz > 0, "empty file");
	CHECK(fseek(f, 0, SEEK_SET) == 0, "seek start");

	unsigned char *buf = malloc((size_t)sz);
	CHECK(buf != NULL, "malloc");
	CHECK(fread(buf, 1, (size_t)sz, f) == (size_t)sz, "fread");
	fclose(f);

	CHECK(sz >= 14, "too short");
	CHECK(memcmp(buf, "MThd", 4) == 0, "MThd magic");
	CHECK(read_u32be(buf + 4) == 6, "header length");
	CHECK(read_u16be(buf + 8) == 1, "format");
	CHECK(read_u16be(buf + 10) == 2, "ntrks");
	CHECK(read_u16be(buf + 12) == 480, "division");

	size_t pos = 14;
	int tracks_found = 0;
	NoteOn voice0[8], voice1[8];
	int n0 = 0, n1 = 0;

	while (pos + 8 <= (size_t)sz) {
		CHECK(memcmp(buf + pos, "MTrk", 4) == 0, "MTrk magic");
		pos += 4;
		unsigned int chunk_len = read_u32be(buf + pos);
		pos += 4;
		CHECK(pos + chunk_len <= (size_t)sz, "chunk overrun");

		NoteOn ons[8];
		int n_ons = 0;
		CHECK(parse_track_note_ons(buf + pos, chunk_len, ons, 8,
					  &n_ons) == 0,
		      "parse track");

		if (tracks_found == 0) {
			memcpy(voice0, ons, sizeof(ons));
			n0 = n_ons;
		} else if (tracks_found == 1) {
			memcpy(voice1, ons, sizeof(ons));
			n1 = n_ons;
		}
		tracks_found++;
		pos += chunk_len;
	}

	CHECK(tracks_found == 2, "two MTrk");

	CHECK(n0 == 4, "voice0 note count");
	const int expect_ticks0[] = {0, 480, 960, 1440};
	const int expect_pitches[] = {60, 64, 67, 72};
	for (int i = 0; i < 4; i++) {
		CHECK(voice0[i].tick == expect_ticks0[i], "voice0 tick");
		CHECK(voice0[i].pitch == expect_pitches[i], "voice0 pitch");
		CHECK(voice0[i].channel == 0, "voice0 channel");
		CHECK(voice0[i].velocity == 80, "voice0 velocity");
	}

	CHECK(n1 == 4, "voice1 note count");
	const int expect_ticks1[] = {960, 1440, 1920, 2400};
	for (int i = 0; i < 4; i++) {
		CHECK(voice1[i].tick == expect_ticks1[i], "voice1 tick");
		CHECK(voice1[i].pitch == expect_pitches[i], "voice1 pitch");
		CHECK(voice1[i].channel == 1, "voice1 channel");
		CHECK(voice1[i].velocity == 80, "voice1 velocity");
	}

	free(buf);
	printf("ok\n");
	return 0;
}
