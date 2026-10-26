#ifndef PORT_H
#define PORT_H
/*
 * What the Wii U, Switch and 3DS ports share. Each port supplies the audio output
 * (calling audio_mix from a thread, under the port's audio lock) and the screen,
 * input and start-up of its own system.
 */
#include "platform.h"

#define AUDIO_RATE 32000

/* ports/common/audio_soft.c: mixes the effects and the streamed music into stereo 16-bit */
void audio_mix(s16 *out, int frames);

/* supplied by the port */
void port_audio_lock(void);
void port_audio_unlock(void);
void port_audio_open(void);

/* where the pack and the music are (without a trailing slash), and where the settings,
   high scores and level files are kept (created by the port; 0 when there is no storage) */
const char *port_data_dir(void);
int port_storage_dir(char *out, int size);
const char *port_language(void);        /* "en", "de" or "fr" */
void port_copy_levels(const char *from, const char *to);   /* ports/common/util.c */

/* start-up (first thing in main), the end of the program, and a message when it cannot start */
void port_init(void);
void port_fatal(const char *message);

/* the system asked to quit (HOME menu, close button) */
int  port_quit_requested(void);
void port_shutdown(void);

#endif
