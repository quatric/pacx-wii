/*
 * Sound effects play from the pack (16-bit mono) on 15 voices, mixed in the callback of the
 * default output unit. The music is the original soundtrack, played with AVAudioPlayer from
 * the Music folder of the application.
 */
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#include <os/lock.h>
#include <string.h>
#include "audio.h"
#include "pack.h"

static const char *sndNames[SND_COUNT] = {
    "snd/eat_ghost", "snd/eat_pelletB", "snd/eat_pelletB2", "snd/eat_pelletF", "snd/eat_pelletF2",
    "snd/eat_superpellet", "snd/extra_appear", "snd/extra_eaten", "snd/extra_life", "snd/gameover",
    "snd/ghost_starts", "snd/level_clear", "snd/player_die", "snd/pop", "snd/running_home", "snd/scared",
};
static NSString *const musNames[MUS_COUNT] = {
    @"Pacland", @"PacManiac", @"PacDreamer", @"Loopback", @"PacTitle", @"Ending",
};

#define OUT_RATE 44100.0
#define NUM_VOICES 15

typedef struct { const s16 *data; u32 len; int rate; } Sample;
typedef struct { int id, loop, active; double pos, step; } Voice;

static Sample samples[SND_COUNT];
static Voice voices[NUM_VOICES];
static int nextVoice;
static float sndVolume = 0.7f;
static os_unfair_lock lock = OS_UNFAIR_LOCK_INIT;
static AudioComponentInstance unit;

/* ------------------------------------------------------------------ effects */

static OSStatus render(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus,
                       UInt32 frames, AudioBufferList *io) {
    (void)ref; (void)flags; (void)ts; (void)bus;
    float *out = io->mBuffers[0].mData;
    memset(out, 0, frames * 2 * sizeof(float));
    os_unfair_lock_lock(&lock);
    float vol = sndVolume / 32768.0f;
    for (int v = 0; v < NUM_VOICES; v++) {
        Voice *vc = &voices[v];
        if (!vc->active) continue;
        const Sample *s = &samples[vc->id];
        for (UInt32 i = 0; i < frames; i++) {
            u32 p = (u32)vc->pos;
            if (p + 1 >= s->len) {
                if (vc->loop) {
                    vc->pos -= (double)(s->len - 1);
                    p = (u32)vc->pos;
                } else {
                    vc->active = 0;
                    break;
                }
            }
            float f = (float)(vc->pos - p);
            float x = (s->data[p] * (1 - f) + s->data[p + 1] * f) * vol;
            out[i * 2] += x;
            out[i * 2 + 1] += x;
            vc->pos += vc->step;
        }
    }
    os_unfair_lock_unlock(&lock);
    return noErr;
}

static void play(int id, int loop) {
    if (id < 0 || id >= SND_COUNT || !samples[id].data) return;
    os_unfair_lock_lock(&lock);
    int v = -1;
    for (int k = 0; k < NUM_VOICES; k++) {
        int c = (nextVoice + k) % NUM_VOICES;
        if (!voices[c].active) { v = c; break; }
    }
    if (v < 0) v = nextVoice;
    nextVoice = (v + 1) % NUM_VOICES;
    voices[v] = (Voice){id, loop, 1, 0, samples[id].rate / OUT_RATE};
    os_unfair_lock_unlock(&lock);
}

void snd_play(int id) { play(id, 0); }
void snd_play_loop(int id) { play(id, 1); }

void snd_stop(int id) {
    os_unfair_lock_lock(&lock);
    for (int v = 0; v < NUM_VOICES; v++)
        if (voices[v].id == id) voices[v].active = 0;
    os_unfair_lock_unlock(&lock);
}

void snd_stop_all(void) {
    os_unfair_lock_lock(&lock);
    for (int v = 0; v < NUM_VOICES; v++) voices[v].active = 0;
    os_unfair_lock_unlock(&lock);
}

int snd_is_playing(int id) {
    int r = 0;
    os_unfair_lock_lock(&lock);
    for (int v = 0; v < NUM_VOICES; v++)
        if (voices[v].active && voices[v].id == id) r = 1;
    os_unfair_lock_unlock(&lock);
    return r;
}

void snd_set_volume(float v) {
    os_unfair_lock_lock(&lock);
    sndVolume = v;
    os_unfair_lock_unlock(&lock);
}

/* ------------------------------------------------------------------ music */

@interface MusicDelegate : NSObject <AVAudioPlayerDelegate>
@end

static NSString *musicDir;
static AVAudioPlayer *player;
static MusicDelegate *musicDelegate;
static int track = -1, ended, wanted;
static float musVolume = 0.3f;

@implementation MusicDelegate
- (void)audioPlayerDidFinishPlaying:(AVAudioPlayer *)p successfully:(BOOL)ok {
    if (p == player) ended = 1;
}
@end

static NSURL *track_url(int t) {
    for (NSString *ext in @[@"mp3", @"m4a", @"mov"]) {
        NSString *p = [musicDir stringByAppendingPathComponent:[musNames[t] stringByAppendingPathExtension:ext]];
        if ([[NSFileManager defaultManager] fileExistsAtPath:p]) return [NSURL fileURLWithPath:p];
    }
    return nil;
}

void mus_play(int t, int loop) {
    if (t < 0 || t >= MUS_COUNT) return;
    [player stop];
    player = nil;
    track = t;
    ended = 0;
    wanted = 1;
    NSURL *url = track_url(t);
    if (!url) { ended = 1; return; }
    player = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:nil];
    if (!player) { ended = 1; return; }
    player.delegate = musicDelegate;
    player.numberOfLoops = loop ? -1 : 0;
    player.volume = musVolume;
    [player play];
}

void mus_stop(void) {
    /* MGMusic.stop: the track goes on from here with mus_resume */
    wanted = 0;
    [player pause];
}

void mus_resume(void) {
    if (track < 0 || ended || !player) return;
    wanted = 1;
    [player play];
}

int mus_is_playing(void) { return wanted && !ended && player != nil; }
int mus_current(void) { return track; }

void mus_set_volume(float v) {
    musVolume = v;
    player.volume = v;
}

void audio_update(void) {}

/* ------------------------------------------------------------------ start-up */

void audio_init(const char *dir) {
    musicDir = @(dir);
    musicDelegate = [MusicDelegate new];
    for (int i = 0; i < SND_COUNT; i++) {
        const PackEntry *e = pack_find(sndNames[i]);
        if (!e) continue;
        samples[i].data = pack_data(e);
        samples[i].len = e->size / 2;
        samples[i].rate = e->rate;
    }

    AudioComponentDescription d = {kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput,
                                   kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent c = AudioComponentFindNext(NULL, &d);
    if (!c || AudioComponentInstanceNew(c, &unit) != noErr) return;
    AudioStreamBasicDescription f = {0};
    f.mSampleRate = OUT_RATE;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    f.mChannelsPerFrame = 2;
    f.mBitsPerChannel = 32;
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = 8;
    f.mBytesPerPacket = 8;
    AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
    AURenderCallbackStruct cb = {render, NULL};
    AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
    if (AudioUnitInitialize(unit) == noErr) AudioOutputUnitStart(unit);
}
