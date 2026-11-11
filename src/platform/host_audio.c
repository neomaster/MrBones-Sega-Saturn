#include "host_audio.h"
#include "core/cdda.h"
#include "core/menu_options.h"
#include <stdlib.h>
#include <string.h>

unsigned host_audio_volume_word(unsigned level) {
    if (level > MENU_VOLUME_MAX) level = MENU_VOLUME_MAX;
    return level * 0xffffu / MENU_VOLUME_MAX;
}

void host_audio_mono_downmix(int16_t *pcm, size_t frames) {
    for (size_t i = 0; i < frames; i++) {
        int16_t m = (int16_t)(((int)pcm[i * 2] + (int)pcm[i * 2 + 1]) / 2);
        pcm[i * 2] = m;
        pcm[i * 2 + 1] = m;
    }
}

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
static volatile LONG g_volume_level = MENU_VOLUME_MAX;
static volatile LONG g_stereo;

void host_audio_set_stereo(int on) {
    InterlockedExchange(&g_stereo, on != 0);
}

static int stereo_on(void) {
    return InterlockedCompareExchange(&g_stereo, 0, 0) != 0;
}

void host_audio_set_volume(unsigned level) {
    if (level > MENU_VOLUME_MAX) level = MENU_VOLUME_MAX;
    InterlockedExchange(&g_volume_level, (LONG)level);
}

static DWORD volume_pair(unsigned level) {
    DWORD word = host_audio_volume_word(level);
    return word | (word << 16);
}

static void apply_volume(HWAVEOUT wave) {
    if (wave) waveOutSetVolume(wave, volume_pair((unsigned)InterlockedCompareExchange(&g_volume_level, 0, 0)));
}

struct host_audio {
    cdimage *cd;
    int track;
    int menu_mix;
    uint32_t frame;
    volatile LONG stop;
    volatile LONG fading;
    HANDLE thread;
    HWAVEOUT wave;
    int16_t *movie_pcm;
    size_t movie_samples;
    unsigned movie_rate, movie_channels;
    volatile LONG movie_state;
    volatile LONG movie_position_ms;
    volatile LONG movie_paused;
};

#define BUF_FRAMES 8
#define BUF_SAMPLES (CDDA_SAMPLES_PER_SECTOR * BUF_FRAMES)

static DWORD WINAPI audio_thread(void *param) {
    host_audio *a = (host_audio *)param;
    LONG applied_volume = -1;
    WAVEFORMATEX fmt = {0};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = 44100;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = 44100 * 4;
    if (waveOutOpen(&a->wave, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        return 1;

    int16_t *pcm[2] = {NULL, NULL};
    WAVEHDR hdr[2] = {{0}, {0}};
    int prepared[2] = {0, 0};
    int eof = 0;
    for (int i = 0; i < 2; i++) {
        pcm[i] = malloc(BUF_SAMPLES * sizeof(int16_t));
        if (!pcm[i]) { eof = 1; break; }
        hdr[i].lpData = (LPSTR)pcm[i];
    }
    while (!a->stop && !eof) {
        LONG level = InterlockedCompareExchange(&g_volume_level, 0, 0);
        if (!a->fading && level != applied_volume) {
            waveOutSetVolume(a->wave, volume_pair((unsigned)level));
            applied_volume = level;
        }
        int active = 0;
        for (int i = 0; i < 2 && !a->stop; i++) {
            int count = 0;
            if (prepared[i]) {
                if (!(hdr[i].dwFlags & WHDR_DONE)) { active = 1; continue; }
                waveOutUnprepareHeader(a->wave, &hdr[i], sizeof hdr[i]);
                prepared[i] = 0;
            }
            if (eof) continue;
            while (count < BUF_FRAMES &&
                   cdda_read_sector(a->cd, a->track, a->frame,
                                    pcm[i] + count * CDDA_SAMPLES_PER_SECTOR) == 0) {
                a->frame++;
                count++;
            }
            if (count < BUF_FRAMES) eof = 1;
            if (!count) continue;
            if (a->menu_mix) cdda_mix_menu(pcm[i], count * (CDDA_SAMPLES_PER_SECTOR / 2));
            if (!stereo_on()) host_audio_mono_downmix(pcm[i], count * (CDDA_SAMPLES_PER_SECTOR / 2));
            hdr[i].dwBufferLength = (DWORD)(count * CDDA_SAMPLES_PER_SECTOR * sizeof(int16_t));
            hdr[i].dwFlags = 0;
            if (waveOutPrepareHeader(a->wave, &hdr[i], sizeof hdr[i]) != MMSYSERR_NOERROR) { eof = 1; break; }
            prepared[i] = 1;
            if (waveOutWrite(a->wave, &hdr[i], sizeof hdr[i]) != MMSYSERR_NOERROR) {
                waveOutUnprepareHeader(a->wave, &hdr[i], sizeof hdr[i]);
                prepared[i] = 0;
                eof = 1;
                break;
            }
            active = 1;
        }
        if (!active) break;
        Sleep(5);
    }
    if (eof && !a->stop) {
        for (int i = 0; i < 2; i++)
            while (prepared[i] && !(hdr[i].dwFlags & WHDR_DONE) && !a->stop) Sleep(5);
    }
    waveOutReset(a->wave);
    for (int i = 0; i < 2; i++) {
        if (prepared[i]) waveOutUnprepareHeader(a->wave, &hdr[i], sizeof hdr[i]);
        free(pcm[i]);
    }
    waveOutClose(a->wave);
    a->wave = NULL;
    return 0;
}

static DWORD WINAPI movie_thread(void *param) {
    host_audio *a = param;
    LONG applied_volume = -1;
    WAVEFORMATEX fmt = {0};
    WAVEHDR hdr = {0};
    int paused = 0;
    int completed = 0;
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = (WORD)a->movie_channels;
    fmt.nSamplesPerSec = a->movie_rate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = (WORD)(fmt.nChannels * 2);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    if (waveOutOpen(&a->wave, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        InterlockedExchange(&a->movie_state, -1);
        return 1;
    }
    apply_volume(a->wave);
    applied_volume = InterlockedCompareExchange(&g_volume_level, 0, 0);
    hdr.lpData = (LPSTR)a->movie_pcm;
    hdr.dwBufferLength = (DWORD)(a->movie_samples * sizeof(int16_t));
    if (waveOutPrepareHeader(a->wave, &hdr, sizeof hdr) == MMSYSERR_NOERROR) {
        if (waveOutWrite(a->wave, &hdr, sizeof hdr) == MMSYSERR_NOERROR) {
            InterlockedExchange(&a->movie_state, 1);
            while (!a->stop && !(hdr.dwFlags & WHDR_DONE)) {
                LONG level = InterlockedCompareExchange(&g_volume_level, 0, 0);
                if (!a->fading && level != applied_volume) {
                    waveOutSetVolume(a->wave, volume_pair((unsigned)level));
                    applied_volume = level;
                }
                MMTIME pos = {0};
                int want_pause = InterlockedCompareExchange(&a->movie_paused, 0, 0) != 0;
                if (want_pause != paused) {
                    if (want_pause) waveOutPause(a->wave); else waveOutRestart(a->wave);
                    paused = want_pause;
                }
                pos.wType = TIME_MS;
                if (waveOutGetPosition(a->wave, &pos, sizeof pos) == MMSYSERR_NOERROR) {
                    uint64_t ms;
                    if (pos.wType == TIME_MS) ms = pos.u.ms;
                    else if (pos.wType == TIME_SAMPLES) ms = (uint64_t)pos.u.sample * 1000u / fmt.nSamplesPerSec;
                    else if (pos.wType == TIME_BYTES) ms = (uint64_t)pos.u.cb * 1000u / fmt.nAvgBytesPerSec;
                    else { InterlockedExchange(&a->movie_state, -1); Sleep(5); continue; }
                    InterlockedExchange(&a->movie_position_ms, (LONG)ms);
                } else InterlockedExchange(&a->movie_state, -1);
                Sleep(5);
            }
            if (!a->stop && InterlockedCompareExchange(&a->movie_state, 0, 0) == 1) {
                InterlockedExchange(&a->movie_position_ms,
                    (LONG)((uint64_t)a->movie_samples * 1000u / a->movie_channels / a->movie_rate));
                InterlockedExchange(&a->movie_state, 2);
                completed = 1;
            }
        } else InterlockedExchange(&a->movie_state, -1);
        /* shortcut: 50 ms covers measured buffering; query latency for other devices. */
        if (completed) Sleep(50);
        else waveOutReset(a->wave);
        waveOutUnprepareHeader(a->wave, &hdr, sizeof hdr);
    } else InterlockedExchange(&a->movie_state, -1);
    waveOutClose(a->wave);
    a->wave = NULL;
    return 0;
}

int host_audio_movie_clock(host_audio *audio, unsigned *position_ms) {
    LONG state;
    if (!audio || !audio->movie_pcm || !position_ms) return -1;
    state = InterlockedCompareExchange(&audio->movie_state, 0, 0);
    *position_ms = (unsigned)InterlockedCompareExchange(&audio->movie_position_ms, 0, 0);
    return (int)state;
}

void host_audio_movie_pause(host_audio *audio, int paused) {
    if (audio && audio->movie_pcm) InterlockedExchange(&audio->movie_paused, paused != 0);
}

host_audio *host_audio_start_pcm16(const int16_t *pcm, size_t samples, unsigned rate, unsigned channels) {
    host_audio *a;
    if (!pcm || !samples || samples > 0xffffffffu / sizeof(int16_t) ||
        rate < 4000 || rate > 192000 || (channels != 1 && channels != 2) || samples % channels) return NULL;
    a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->movie_pcm = malloc(samples * sizeof(int16_t));
    if (!a->movie_pcm) { free(a); return NULL; }
    memcpy(a->movie_pcm, pcm, samples * sizeof(int16_t));
    a->movie_samples = samples;
    a->movie_rate = rate;
    a->movie_channels = channels;
    a->thread = CreateThread(NULL, 0, movie_thread, a, 0, NULL);
    if (!a->thread) { free(a->movie_pcm); free(a); return NULL; }
    return a;
}

static host_audio *start_cdda(cdimage *cd, int track, int menu_mix) {
    if (!cd || track < 2) return NULL;
    host_audio *a = (host_audio *)calloc(1, sizeof *a);
    if (!a) return NULL;
    a->cd = cd;
    a->track = track;
    a->menu_mix = menu_mix;
    a->thread = CreateThread(NULL, 0, audio_thread, a, 0, NULL);
    if (!a->thread) { free(a); return NULL; }
    return a;
}

host_audio *host_audio_start_cdda(cdimage *cd, int track) {
    return start_cdda(cd, track, 0);
}

host_audio *host_audio_start_menu_cdda(cdimage *cd, int track) {
    return start_cdda(cd, track, 1);
}

void host_audio_stop(host_audio *audio) {
    if (!audio) return;
    InterlockedExchange(&audio->stop, 1);
    if (audio->thread) {
        WaitForSingleObject(audio->thread, INFINITE);
        CloseHandle(audio->thread);
    }
    free(audio->movie_pcm);
    free(audio);
}

void host_audio_fade_stop(host_audio *audio, unsigned duration_ms) {
    if (!audio) return;
    if (duration_ms && audio->wave) {
        InterlockedExchange(&audio->fading, 1);
        unsigned start = (unsigned)InterlockedCompareExchange(&g_volume_level, 0, 0);
        const unsigned steps = 20;
        for (unsigned i = 1; i <= steps; i++) {
            unsigned level = (start * (steps - i) * 0xffffu) / (MENU_VOLUME_MAX * steps);
            waveOutSetVolume(audio->wave, level | (level << 16));
            Sleep(duration_ms / steps);
        }
        Sleep(50);
    }
    host_audio_stop(audio);
}

typedef struct {
    int16_t *stereo;
    size_t samples; /* stereo frames */
    unsigned rate;
} pcm8_job;

static DWORD WINAPI pcm8_thread(void *param) {
    pcm8_job *job = (pcm8_job *)param;
    WAVEFORMATEX fmt = {0};
    HWAVEOUT wave = NULL;
    WAVEHDR hdr = {0};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = job->rate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = job->rate * 4;
    if (waveOutOpen(&wave, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        free(job->stereo);
        free(job);
        return 1;
    }
    apply_volume(wave);
    hdr.lpData = (LPSTR)job->stereo;
    hdr.dwBufferLength = (DWORD)(job->samples * sizeof(int16_t));
    if (waveOutPrepareHeader(wave, &hdr, sizeof hdr) == MMSYSERR_NOERROR) {
        if (waveOutWrite(wave, &hdr, sizeof hdr) == MMSYSERR_NOERROR) {
            while (!(hdr.dwFlags & WHDR_DONE)) Sleep(5);
        }
        waveOutUnprepareHeader(wave, &hdr, sizeof hdr);
    }
    waveOutReset(wave);
    waveOutClose(wave);
    free(job->stereo);
    free(job);
    return 0;
}

int host_audio_play_pcm8(const int8_t *pcm, size_t length, unsigned rate) {
    if (!pcm || !length || rate < 4000 || rate > 48000) return -1;
    /* Cap one-shot length (~2 s at 22 kHz) to avoid huge buffers. */
    if (length > 48000u) length = 48000u;
    pcm8_job *job = (pcm8_job *)calloc(1, sizeof *job);
    if (!job) return -1;
    job->rate = rate;
    job->samples = length * 2; /* L+R */
    job->stereo = (int16_t *)malloc(job->samples * sizeof(int16_t));
    if (!job->stereo) { free(job); return -1; }
    for (size_t i = 0; i < length; i++) {
        int16_t s = (int16_t)((int)pcm[i] << 8);
        job->stereo[i * 2] = s;
        job->stereo[i * 2 + 1] = s;
    }
    HANDLE th = CreateThread(NULL, 0, pcm8_thread, job, 0, NULL);
    if (!th) {
        free(job->stereo);
        free(job);
        return -1;
    }
    CloseHandle(th); /* detach; thread frees job */
    return 0;
}
#else
struct host_audio { int dummy; };
host_audio *host_audio_start_cdda(cdimage *cd, int track) { (void)cd;(void)track; return NULL; }
host_audio *host_audio_start_menu_cdda(cdimage *cd, int track) { (void)cd;(void)track; return NULL; }
host_audio *host_audio_start_pcm16(const int16_t *pcm, size_t samples, unsigned rate, unsigned channels) {
    (void)pcm; (void)samples; (void)rate; (void)channels; return NULL;
}
int host_audio_movie_clock(host_audio *audio, unsigned *position_ms) { (void)audio; (void)position_ms; return -1; }
void host_audio_movie_pause(host_audio *audio, int paused) { (void)audio; (void)paused; }
void host_audio_stop(host_audio *audio) { free(audio); }
void host_audio_fade_stop(host_audio *audio, unsigned duration_ms) { (void)duration_ms; host_audio_stop(audio); }
void host_audio_set_volume(unsigned level) { (void)level; }
void host_audio_set_stereo(int on) { (void)on; }
int host_audio_play_pcm8(const int8_t *pcm, size_t length, unsigned rate) {
    (void)pcm; (void)length; (void)rate; return -1;
}
#endif
