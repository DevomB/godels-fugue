#include "canon.h"

#include "theory.h"

int canon_melody_index(int voice, int time, int delay, int length) {
    if (length <= 0 || voice < 0 || voice >= VOICE_MAX) return -1;
    int index = time - voice * delay;
    if (index < 0 || index >= length) return -1;
    return index;
}

int canon_span(int length, int delay) {
    return canon_span_voices(length, delay, 2);
}

int canon_span_voices(int length, int delay, int voices) {
    if (voices < 1) voices = 1;
    if (voices > VOICE_MAX) voices = VOICE_MAX;
    return length + (voices - 1) * delay;
}

int canon_source_index(int voice, int time, int delay, int length, int retrograde) {
    if (voice < 0 || voice >= VOICE_MAX) return -1;
    if (length <= 0) return -1;
    int base = time - voice * delay;
    if (base < 0 || base >= length) return -1;
    if (voice > 0 && retrograde) return length - 1 - base;
    return base;
}

int canon_voice_delay(const PieceConfig *config, int voice) {
    if (config == NULL || voice <= 0) return 0;
    if (voice >= VOICE_MAX) return -1;
    int base = config->voice_delay[voice] != 0 ? config->voice_delay[voice]
                                               : voice * config->delay;
    if (voice > 0) base += config->phase;
    return base;
}

int canon_map_source(const PieceConfig *config, int voice, int time) {
    if (config == NULL || voice < 0 || voice >= VOICE_MAX) return -1;
    int length = config->length;
    if (length <= 0) return -1;
    int delay = canon_voice_delay(config, voice);
    if (delay < 0) return -1;
    int raw = time - delay;
    if (voice > 0 && config->augment >= 2) {
        if (raw < 0) return -1;
        raw = raw / config->augment;
    } else if (voice > 0 && config->diminish >= 2) {
        if (raw < 0) return -1;
        raw = raw * config->diminish;
    }
    if (raw < 0 || raw >= length) {
        if (!config->cyclic || length <= 0) return -1;
        raw %= length;
        if (raw < 0) raw += length;
    }
    if (voice > 0 && config->retrograde) return length - 1 - raw;
    return raw;
}

int canon_span_config(const PieceConfig *config) {
    if (config == NULL) return 0;
    int voices = config->voices;
    if (voices < 1) voices = 1;
    if (voices > VOICE_MAX) voices = VOICE_MAX;
    int end = config->length;
    for (int v = 1; v < voices; v++) {
        int d = canon_voice_delay(config, v);
        int follow;
        if (config->augment >= 2) {
            follow = d + config->length * config->augment;
        } else if (config->diminish >= 2) {
            follow = d + (config->length + config->diminish - 1) / config->diminish;
        } else {
            follow = d + config->length;
        }
        if (follow > end) end = follow;
    }
    return end;
}

int canon_sounding(const PieceConfig *config, int voice, int source_pitch) {
    int pitch = source_pitch;
    if (config == NULL || voice <= 0) return pitch;
    if (config->invert) {
        pitch = config->invert_mod12
                    ? invert_pitch_mod12(config->axis, pitch)
                    : invert_pitch(config->axis, pitch);
    }
    pitch += config->transpose;
    return pitch;
}
