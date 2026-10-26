/*
 * The sound of the Wii U, Switch and 3DS ports. Effects are the pack's 16-bit mono samples,
 * music is the original soundtrack as .ogg, decoded with stb_vorbis while it is mixed. The
 * port's audio thread calls audio_mix; everything here runs under port_audio_lock.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
#include "audio.h"
#include "pack.h"
#include "port.h"

static const char *sndNames[SND_COUNT] = {
    "snd/eat_ghost", "snd/eat_pelletB", "snd/eat_pelletB2", "snd/eat_pelletF", "snd/eat_pelletF2",
    "snd/eat_superpellet", "snd/extra_appear", "snd/extra_eaten", "snd/extra_life", "snd/gameover",
    "snd/ghost_starts", "snd/level_clear", "snd/player_die", "snd/pop", "snd/running_home", "snd/scared",
};
static const char *musNames[MUS_COUNT] = {
    "Pacland", "PacManiac", "PacDreamer", "Loopback", "PacTitle", "Ending",
};

typedef struct { const s16 *data; int n; int rate; } Sample;
static Sample samples[SND_COUNT];

#define NUM_VOICES 16
typedef struct { int id; const s16 *data; int n; unsigned pos, step; int loop; } Voice;
static Voice voices[NUM_VOICES];
static float sndVolume = 0.7f, musVolume = 0.3f;

static char musicDir[256];
static stb_vorbis *vf;
static int vfChannels, track = -1, want, ended, loopTrack;

void audio_init(const char *dir) {
    strncpy(musicDir, dir, sizeof musicDir - 1);
    for (int i = 0; i < SND_COUNT; i++) {
        const PackEntry *e = pack_find(sndNames[i]);
        if (!e) continue;
        samples[i].data = (const s16 *)pack_data(e);
        samples[i].n = (int)(e->size / 2);
        samples[i].rate = e->rate;
    }
    for (int i = 0; i < NUM_VOICES; i++) voices[i].id = -1;
    port_audio_open();
}

void audio_update(void) {}

/* ------------------------------------------------------------------ effects */

static void play(int id, int loop) {
    if (id < 0 || id >= SND_COUNT || !samples[id].data) return;
    port_audio_lock();
    int v = -1;
    for (int i = 0; i < NUM_VOICES; i++)
        if (voices[i].id < 0) { v = i; break; }
    if (v < 0) v = 0;
    voices[v].id = id;
    voices[v].data = samples[id].data;
    voices[v].n = samples[id].n;
    voices[v].pos = 0;
    voices[v].step = (unsigned)((double)samples[id].rate / AUDIO_RATE * 65536.0);
    voices[v].loop = loop;
    port_audio_unlock();
}

void snd_play(int id) { play(id, 0); }
void snd_play_loop(int id) { play(id, 1); }

void snd_stop(int id) {
    port_audio_lock();
    for (int i = 0; i < NUM_VOICES; i++)
        if (voices[i].id == id) voices[i].id = -1;
    port_audio_unlock();
}

void snd_stop_all(void) {
    port_audio_lock();
    for (int i = 0; i < NUM_VOICES; i++) voices[i].id = -1;
    port_audio_unlock();
}

int snd_is_playing(int id) {
    int r = 0;
    port_audio_lock();
    for (int i = 0; i < NUM_VOICES; i++)
        if (voices[i].id == id) r = 1;
    port_audio_unlock();
    return r;
}

void snd_set_volume(float v) { sndVolume = v; }

/* ------------------------------------------------------------------ music */

static void close_track(void) {
    if (vf) { stb_vorbis_close(vf); vf = NULL; }
}

static int open_track(int t) {
    char path[320];
    snprintf(path, sizeof path, "%s/%s.ogg", musicDir, musNames[t]);
    int err = 0;
    vf = stb_vorbis_open_filename(path, &err, NULL);
    if (!vf) return 0;
    vfChannels = stb_vorbis_get_info(vf).channels;
    if (vfChannels < 1 || vfChannels > 2) { close_track(); return 0; }
    return 1;
}

void mus_play(int t, int loop) {
    if (t < 0 || t >= MUS_COUNT) return;
    port_audio_lock();
    close_track();
    track = t;
    loopTrack = loop;
    ended = !open_track(t);
    want = 1;
    port_audio_unlock();
}

void mus_stop(void) {
    port_audio_lock();
    want = 0;          /* the decoder keeps its place for mus_resume */
    port_audio_unlock();
}

void mus_resume(void) {
    port_audio_lock();
    if (track >= 0 && !ended) want = 1;
    port_audio_unlock();
}

int mus_is_playing(void) {
    port_audio_lock();
    int r = want && !ended;
    port_audio_unlock();
    return r;
}

int mus_current(void) { return track; }
void mus_set_volume(float v) { musVolume = v; }

/* ------------------------------------------------------------------ the mixer */

static inline int clip(int v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }

void audio_mix(s16 *out, int frames) {
    static int acc[2048 * 2];
    while (frames > 0) {
        int n = frames > 2048 ? 2048 : frames;
        memset(acc, 0, sizeof(int) * n * 2);

        if (want && !ended && vf) {
            static s16 tmp[2048 * 2];
            int got = 0;
            while (got < n) {
                int r = stb_vorbis_get_samples_short_interleaved(vf, vfChannels, tmp + got * vfChannels,
                                                                 (n - got) * vfChannels);
                if (r <= 0) {
                    if (loopTrack && stb_vorbis_seek_start(vf)) continue;
                    ended = 1;
                    break;
                }
                got += r;
            }
            int vol = (int)(musVolume * 256.0f);
            for (int i = 0; i < got; i++) {
                int l = tmp[i * vfChannels], r = tmp[i * vfChannels + (vfChannels - 1)];
                acc[i * 2] += l * vol >> 8;
                acc[i * 2 + 1] += r * vol >> 8;
            }
        }

        int vol = (int)(sndVolume * 256.0f);
        for (int v = 0; v < NUM_VOICES; v++) {
            Voice *s = &voices[v];
            if (s->id < 0) continue;
            for (int i = 0; i < n; i++) {
                unsigned idx = s->pos >> 16;
                if (idx >= (unsigned)s->n) {
                    if (s->loop) { s->pos -= (unsigned)s->n << 16; idx = s->pos >> 16; }
                    else { s->id = -1; break; }
                }
                int a = s->data[idx], b = idx + 1 < (unsigned)s->n ? s->data[idx + 1] : a;
                int f = (s->pos >> 8) & 255;
                int smp = (a * (256 - f) + b * f) >> 8;
                smp = smp * vol >> 8;
                acc[i * 2] += smp;
                acc[i * 2 + 1] += smp;
                s->pos += s->step;
            }
        }
        for (int i = 0; i < n * 2; i++) out[i] = (s16)clip(acc[i]);
        out += n * 2;
        frames -= n;
    }
}
