#include "canon.h"

#include "theory.h"

int canon_voice_delay(const PieceConfig *config, int voice) {
    if (config == NULL || voice <= 0) return 0;
    if (voice >= VOICE_MAX) return -1;
    int base = config->voice_delay[voice] != 0 ? config->voice_delay[voice]
                                               : voice * config->delay;
    return base + config->phase;
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
        if (!config->cyclic) return -1;
        raw %= length;
        if (raw < 0) raw += length;
    }
    if (voice > 0 && config->retrograde) return length - 1 - raw;
    return raw;
}

int canon_span_config(const PieceConfig *config) {
    if (config == NULL) return 0;
    if (config->cyclic) return config->length;
    int voices = config_voice_count(config);
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
    if (source_pitch == PITCH_REST) return SOUND_REST;
    int pitch = source_pitch;
    if (config == NULL || voice <= 0) return pitch;
    if (config->invert) {
        pitch = config->invert_mod12 ? invert_pitch_mod12(config->axis, pitch)
                                     : invert_pitch(config->axis, pitch);
    }
    return pitch + config->transpose;
}
