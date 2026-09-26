#include "midi.h"

#include <stdio.h>

static int write_u16be(FILE *f, unsigned int v)
{
	unsigned char b[2] = {(unsigned char)((v >> 8) & 0xFF),
			      (unsigned char)(v & 0xFF)};
	return fwrite(b, 1, 2, f) == 2 ? 0 : -1;
}

static int write_u32be(FILE *f, unsigned int v)
{
	unsigned char b[4] = {(unsigned char)((v >> 24) & 0xFF),
			      (unsigned char)((v >> 16) & 0xFF),
			      (unsigned char)((v >> 8) & 0xFF),
			      (unsigned char)(v & 0xFF)};
	return fwrite(b, 1, 4, f) == 4 ? 0 : -1;
}

static int write_vlq(FILE *f, unsigned int value)
{
	unsigned long buffer = value & 0x7Fu;
	while ((value >>= 7) > 0) {
		buffer <<= 8;
		buffer |= 0x80u;
		buffer |= (value & 0x7Fu);
	}
	for (;;) {
		if (fputc((int)(buffer & 0xFFu), f) == EOF)
			return -1;
		if (buffer & 0x80u)
			buffer >>= 8;
		else
			break;
	}
	return 0;
}

static int write_track(FILE *f, const int *melody, int length, int channel,
		       int start_tick, const int *durations)
{
	if (fwrite("MTrk", 1, 4, f) != 4)
		return -1;

	long len_pos = ftell(f);
	if (len_pos < 0)
		return -1;
	if (write_u32be(f, 0) != 0)
		return -1;

	long data_start = ftell(f);
	if (data_start < 0)
		return -1;

	int last_tick = 0;

	if (write_vlq(f, 0) != 0)
		return -1;
	if (fputc(0xC0 | channel, f) == EOF)
		return -1;
	if (fputc(0, f) == EOF)
		return -1;

	for (int i = 0; i < length; i++) {
		int units = durations != NULL ? durations[i] : 1;
		if (units <= 0)
			continue;
		int on_tick = start_tick + i * 480;
		int off_tick = on_tick + units * 480;

		if (write_vlq(f, (unsigned int)(on_tick - last_tick)) != 0)
			return -1;
		if (fputc(0x90 | channel, f) == EOF)
			return -1;
		if (fputc(melody[i], f) == EOF)
			return -1;
		if (fputc(80, f) == EOF)
			return -1;
		last_tick = on_tick;

		if (write_vlq(f, (unsigned int)(off_tick - last_tick)) != 0)
			return -1;
		if (fputc(0x80 | channel, f) == EOF)
			return -1;
		if (fputc(melody[i], f) == EOF)
			return -1;
		if (fputc(0, f) == EOF)
			return -1;
		last_tick = off_tick;
	}

	if (write_vlq(f, 0) != 0)
		return -1;
	if (fputc(0xFF, f) == EOF)
		return -1;
	if (fputc(0x2F, f) == EOF)
		return -1;
	if (fputc(0x00, f) == EOF)
		return -1;

	long data_end = ftell(f);
	if (data_end < 0)
		return -1;

	unsigned int track_len = (unsigned int)(data_end - data_start);
	if (fseek(f, len_pos, SEEK_SET) != 0)
		return -1;
	if (write_u32be(f, track_len) != 0)
		return -1;
	if (fseek(f, data_end, SEEK_SET) != 0)
		return -1;

	return 0;
}

bool midi_write_voices(const char *path, const int *const *lines,
		       const int *start_ticks, int n_voices, int length,
		       const int *durations)
{
	if (path == NULL || lines == NULL || start_ticks == NULL)
		return false;
	if (length < 0 || n_voices < 1 || n_voices > 16)
		return false;

	for (int v = 0; v < n_voices; v++) {
		if (start_ticks[v] < 0)
			return false;
		if (length > 0 && lines[v] == NULL)
			return false;
		for (int i = 0; i < length; i++) {
			if (lines[v][i] < 0 || lines[v][i] > 127)
				return false;
		}
	}

	FILE *f = fopen(path, "wb");
	if (f == NULL)
		return false;

	if (fwrite("MThd", 1, 4, f) != 4)
		goto fail;
	if (write_u32be(f, 6) != 0)
		goto fail;
	if (write_u16be(f, 1) != 0)
		goto fail;
	if (write_u16be(f, (unsigned int)n_voices) != 0)
		goto fail;
	if (write_u16be(f, 480) != 0)
		goto fail;

	for (int v = 0; v < n_voices; v++) {
		if (write_track(f, lines[v], length, v, start_ticks[v],
				durations) != 0)
			goto fail;
	}

	if (fclose(f) != 0)
		return false;
	return true;

fail:
	fclose(f);
	return false;
}

bool midi_write_canon(const char *path, const int *lead, const int *follow,
		      int length, int delay)
{
	const int *lines[2];
	int starts[2];

	if (delay < 0)
		return false;
	lines[0] = lead;
	lines[1] = follow;
	starts[0] = 0;
	starts[1] = delay * 480;
	return midi_write_voices(path, lines, starts, 2, length, NULL);
}
