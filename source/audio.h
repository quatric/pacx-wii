#ifndef AUDIO_H
#define AUDIO_H

/* the game's sound ids, in the order of MGSound.add: in applicationDidFinishLaunching */
enum {
    SND_EAT_GHOST, SND_EAT_PELLET_B, SND_EAT_PELLET_B2, SND_EAT_PELLET_F, SND_EAT_PELLET_F2,
    SND_EAT_SUPERPELLET, SND_EXTRA_APPEAR, SND_EXTRA_EATEN, SND_EXTRA_LIFE, SND_GAMEOVER,
    SND_GHOST_STARTS, SND_LEVEL_CLEAR, SND_PLAYER_DIE, SND_POP, SND_RUNNING_HOME, SND_SCARED,
    SND_COUNT
};

/* music tracks: the four game tracks (musicArray), the title and the ending */
enum { MUS_PACLAND, MUS_PACMANIAC, MUS_PACDREAMER, MUS_LOOPBACK, MUS_TITLE, MUS_ENDING, MUS_COUNT };

void audio_init(const char *musicDir);
void audio_update(void);                /* once a frame */

void snd_play(int id);
void snd_play_loop(int id);
void snd_stop(int id);
void snd_stop_all(void);
int  snd_is_playing(int id);
void snd_set_volume(float v);

void mus_play(int track, int loop);
void mus_stop(void);                    /* MGMusic.stop: remembers where it was */
void mus_resume(void);
int  mus_is_playing(void);
int  mus_current(void);                 /* currentTrackId, -1 if none */
void mus_set_volume(float v);

#endif
