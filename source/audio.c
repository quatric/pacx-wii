/*
 * Sound effects play from the pack (16-bit mono, 22050 Hz) on ASND voices 1..15.
 * Music is the original soundtrack converted to .ogg, streamed from the SD card: a worker
 * thread decodes into a ring of buffers that the voice callback queues on voice 0.
 */
#include <gccore.h>
#include <asndlib.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <tremor/ivorbisfile.h>
#include "audio.h"
#include "pack.h"

static const char *sndNames[SND_COUNT] = {
    "snd/eat_ghost", "snd/eat_pelletB", "snd/eat_pelletB2", "snd/eat_pelletF", "snd/eat_pelletF2",
    "snd/eat_superpellet", "snd/extra_appear", "snd/extra_eaten", "snd/extra_life", "snd/gameover",
    "snd/ghost_starts", "snd/level_clear", "snd/player_die", "snd/pop", "snd/running_home", "snd/scared",
};
static const char *musNames[MUS_COUNT] = {
    "Pacland", "PacManiac", "PacDreamer", "Loopback", "PacTitle", "Ending",
};

typedef struct { const void *data; u32 size; int rate; } Sample;
static Sample samples[SND_COUNT];

#define FIRST_SFX_VOICE 1
#define NUM_SFX_VOICES 15
static int voiceSnd[16];          /* sound id on each voice, -1 if none */
static int nextVoice;
static float sndVolume = 0.7f;

static int level(float v) {
    int l = (int)(255 * v + 0.5f);
    return l > 255 ? 255 : l < 0 ? 0 : l;
}

/* ------------------------------------------------------------------ effects */

static int voice_busy(int v) { return ASND_StatusVoice(v) != SND_UNUSED; }

static int pick_voice(void) {
    for (int k = 0; k < NUM_SFX_VOICES; k++) {
        int v = FIRST_SFX_VOICE + (nextVoice + k) % NUM_SFX_VOICES;
        if (!voice_busy(v)) { nextVoice = (v - FIRST_SFX_VOICE + 1) % NUM_SFX_VOICES; return v; }
    }
    int v = FIRST_SFX_VOICE + nextVoice;
    nextVoice = (nextVoice + 1) % NUM_SFX_VOICES;
    ASND_StopVoice(v);
    return v;
}

static void play(int id, int loop) {
    if (id < 0 || id >= SND_COUNT || !samples[id].data) return;
    int v = pick_voice();
    int l = level(sndVolume);
    voiceSnd[v] = id;
    if (loop)
        ASND_SetInfiniteVoice(v, VOICE_MONO_16BIT, samples[id].rate, 0, (void *)samples[id].data,
                              samples[id].size, l, l);
    else
        ASND_SetVoice(v, VOICE_MONO_16BIT, samples[id].rate, 0, (void *)samples[id].data,
                      samples[id].size, l, l, NULL);
}

void snd_play(int id) { play(id, 0); }
void snd_play_loop(int id) { play(id, 1); }

void snd_stop(int id) {
    for (int v = FIRST_SFX_VOICE; v < FIRST_SFX_VOICE + NUM_SFX_VOICES; v++)
        if (voiceSnd[v] == id) { ASND_StopVoice(v); voiceSnd[v] = -1; }
}

void snd_stop_all(void) {
    for (int v = FIRST_SFX_VOICE; v < FIRST_SFX_VOICE + NUM_SFX_VOICES; v++) {
        ASND_StopVoice(v);
        voiceSnd[v] = -1;
    }
}

int snd_is_playing(int id) {
    for (int v = FIRST_SFX_VOICE; v < FIRST_SFX_VOICE + NUM_SFX_VOICES; v++)
        if (voiceSnd[v] == id && voice_busy(v)) return 1;
    return 0;
}

void snd_set_volume(float v) {
    sndVolume = v;
    int l = level(v);
    for (int k = FIRST_SFX_VOICE; k < FIRST_SFX_VOICE + NUM_SFX_VOICES; k++)
        if (voice_busy(k)) ASND_ChangeVolumeVoice(k, l, l);
}

/* ------------------------------------------------------------------ music */

#define MUSIC_VOICE 0
#define FRAMES 4096
#define NBUF 4
#define BUFBYTES (FRAMES * 2 * 2)
#define SILENCE_BYTES 4096

static char musicDir[256];
static s16 *buf[NBUF], *silence;
static volatile int st[NBUF];            /* 0 empty, 1 decoded, 2 queued on the voice */
static int blen[NBUF];
static int fillIdx, cbIdx, fifo[3], nfifo;
static volatile int want, playing, restart, ended, loopTrack;
static volatile int track = -1, reqTrack = -1;
static int vfOpen, rate, ch;
static FILE *mf;
static OggVorbis_File vf;
static lwp_t thread;
static lwpq_t queue;
static float musVolume = 0.3f;
static int lastVol = -1;
static mutex_t lock;

static size_t f_read(void *p, size_t s, size_t n, void *src) { return fread(p, s, n, (FILE *)src); }
static int f_seek(void *src, ogg_int64_t off, int wh) { return fseek((FILE *)src, (long)off, wh); }
static int f_close(void *src) { return fclose((FILE *)src); }
static long f_tell(void *src) { return ftell((FILE *)src); }

static void close_track(void) {
    if (vfOpen) { ov_clear(&vf); vfOpen = 0; mf = NULL; }
    else if (mf) { fclose(mf); mf = NULL; }
}

static int open_track(int t) {
    char path[320];
    snprintf(path, sizeof path, "%s/%s.ogg", musicDir, musNames[t]);
    mf = fopen(path, "rb");
    if (!mf) return 0;
    setvbuf(mf, NULL, _IOFBF, 32768);
    ov_callbacks cb = {f_read, f_seek, f_close, f_tell};
    if (ov_open_callbacks(mf, &vf, NULL, 0, cb) < 0) { fclose(mf); mf = NULL; return 0; }
    vorbis_info *vi = ov_info(&vf, -1);
    if (!vi || vi->channels < 1 || vi->channels > 2) { ov_clear(&vf); mf = NULL; return 0; }
    rate = (int)vi->rate;
    ch = vi->channels;
    vfOpen = 1;
    return 1;
}

static void *worker(void *arg) {
    (void)arg;
    for (;;) {
        if (restart) {
            close_track();
            for (int i = 0; i < NBUF; i++) st[i] = 0;
            fillIdx = 0;
            track = reqTrack;
            ended = 0;
            restart = 0;
        }
        if (!want || ended) { usleep(10000); continue; }
        if (!vfOpen && !open_track(track)) { ended = 1; continue; }
        int s = fillIdx;
        if (st[s]) { usleep(4000); continue; }
        int bytes = FRAMES * ch * 2, got = 0;
        while (got < bytes && want && !restart) {
            int sec;
            long r = ov_read(&vf, (char *)buf[s] + got, bytes - got, &sec);
            if (r <= 0) {
                if (!loopTrack || ov_raw_seek(&vf, 0) < 0) { ended = 1; break; }
            } else {
                got += r;
            }
        }
        if (restart) continue;
        if (got == 0) continue;
        if (got < bytes) memset((char *)buf[s] + got, 0, bytes - got);
        blen[s] = bytes;
        DCFlushRange(buf[s], bytes);
        st[s] = 1;
        fillIdx = (s + 1) % NBUF;
    }
    return NULL;
}

static volatile int starved;

static void voice_cb(s32 voice) {
    if (!playing) return;
    if (nfifo > 0) {
        int done = fifo[0];
        fifo[0] = fifo[1];
        fifo[1] = fifo[2];
        nfifo--;
        if (done >= 0) st[done] = 0;
    }
    int n = cbIdx;
    if (st[n] == 1) {
        ASND_AddVoice(voice, buf[n], blen[n]);
        st[n] = 2;
        fifo[nfifo++] = n;
        cbIdx = (n + 1) % NBUF;
        starved = 0;
    } else {
        ASND_AddVoice(voice, silence, SILENCE_BYTES);
        fifo[nfifo++] = -1;
        starved++;
    }
}

static void stop_voice(void) {
    playing = 0;
    ASND_StopVoice(MUSIC_VOICE);
    nfifo = 0;
}

void mus_play(int t, int loop) {
    if (t < 0 || t >= MUS_COUNT) return;
    stop_voice();
    want = 0;
    reqTrack = t;
    loopTrack = loop;
    restart = 1;
    while (restart) usleep(1000);
    cbIdx = 0;
    lastVol = -1;
    starved = 0;
    want = 1;
}

void mus_stop(void) {
    /* keeps the decoder where it is; the decoded buffers that were queued are dropped */
    if (!want) return;
    stop_voice();
    want = 0;
}

void mus_resume(void) {
    if (track < 0 || ended) return;
    /* buffers still decoded (state 1) play first; queued ones were lost with the voice */
    for (int i = 0; i < NBUF; i++) if (st[i] == 2) st[i] = 0;
    for (int i = 0; i < NBUF; i++) {
        int k = (cbIdx + i) % NBUF;
        if (st[k] == 1) { cbIdx = k; break; }
    }
    want = 1;
}

int mus_is_playing(void) {
    if (!want) return 0;
    if (ended && !playing) return 0;
    if (ended && playing) {
        /* the decoder is done; playing until the queued buffers have run out */
        for (int i = 0; i < NBUF; i++) if (st[i]) return 1;
        return 0;
    }
    return 1;
}

int mus_current(void) { return track; }

void mus_set_volume(float v) { musVolume = v; }

void audio_update(void) {
    if (!want) return;
    int v = level(musVolume);
    if (!playing) {
        int ready = 0;
        for (int i = 0; i < NBUF; i++) if (st[(cbIdx + i) % NBUF] == 1) ready++; else break;
        if (ready >= 2 || (ended && ready >= 1)) {
            int a = cbIdx, b = (cbIdx + 1) % NBUF;
            nfifo = 0;
            playing = 1;
            ASND_SetVoice(MUSIC_VOICE, ch == 2 ? VOICE_STEREO_16BIT : VOICE_MONO_16BIT, rate, 0,
                          buf[a], blen[a], v, v, voice_cb);
            st[a] = 2; fifo[nfifo++] = a;
            cbIdx = b;
            if (st[b] == 1) {
                ASND_AddVoice(MUSIC_VOICE, buf[b], blen[b]);
                st[b] = 2; fifo[nfifo++] = b;
                cbIdx = (b + 1) % NBUF;
            }
            lastVol = v;
        }
        return;
    }
    if (ended) {
        int any = 0;
        for (int i = 0; i < NBUF; i++) if (st[i]) any = 1;
        if (!any) { stop_voice(); want = 0; return; }
    }
    if (v != lastVol) {
        ASND_ChangeVolumeVoice(MUSIC_VOICE, v, v);
        lastVol = v;
    }
}

void audio_init(const char *dir) {
    strncpy(musicDir, dir, sizeof musicDir - 1);
    ASND_Init();
    ASND_Pause(0);
    for (int i = 0; i < 16; i++) voiceSnd[i] = -1;
    for (int i = 0; i < SND_COUNT; i++) {
        const PackEntry *e = pack_find(sndNames[i]);
        if (!e) continue;
        samples[i].data = pack_data(e);
        samples[i].size = e->size & ~31u;
        samples[i].rate = e->rate;
        DCFlushRange((void *)samples[i].data, e->size);
    }
    for (int i = 0; i < NBUF; i++) {
        buf[i] = memalign(32, BUFBYTES);
        memset(buf[i], 0, BUFBYTES);
    }
    silence = memalign(32, SILENCE_BYTES);
    memset(silence, 0, SILENCE_BYTES);
    DCFlushRange(silence, SILENCE_BYTES);
    (void)queue; (void)lock;
    void *stack = memalign(32, 65536);
    LWP_CreateThread(&thread, worker, NULL, stack, 65536, 70);
}
