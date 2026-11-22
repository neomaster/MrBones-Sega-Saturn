#ifndef MB_HOST_AUDIO_H
#define MB_HOST_AUDIO_H
#include "core/cdimage.h"
#include <stddef.h>
#include <stdint.h>

/* Minimal Win32 audio: CD-DA streamer + signed PCM8 one-shots. Not SCSP. */

typedef struct host_audio host_audio;

host_audio *host_audio_start_cdda(cdimage *cd, int track);
host_audio *host_audio_start_menu_cdda(cdimage *cd, int track);
host_audio *host_audio_start_pcm16(const int16_t *pcm, size_t samples, unsigned rate, unsigned channels);
int host_audio_movie_clock(host_audio *audio, unsigned *position_ms);
void host_audio_movie_pause(host_audio *audio, int paused);
void host_audio_stop(host_audio *audio);
void host_audio_fade_stop(host_audio *audio, unsigned duration_ms);
void host_audio_set_volume(unsigned level);
void host_audio_set_muted(int muted);
unsigned host_audio_volume_word(unsigned level);
/* STEREO option affects CD-DA; AVI PCM retains its recorded channels. */
void host_audio_set_stereo(int on);
/* In place: each interleaved L/R frame becomes (L+R)/2 on both channels. */
void host_audio_mono_downmix(int16_t *pcm, size_t frames);

/* Fire-and-forget mono signed PCM8 at `rate` Hz (stereo 16-bit waveOut).
 * Works with or without an active CD-DA stream. Returns 0 on queue. */
int host_audio_play_pcm8(const int8_t *pcm, size_t length, unsigned rate);

#endif
