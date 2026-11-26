#include "host_audio.h"
#include "core/cdda.h"
#include "core/menu_options.h"
#include <ctype.h>
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
static volatile LONG g_muted = -1;

static unsigned current_volume_level(void) {
    LONG v = InterlockedCompareExchange(&g_volume_level, 0, 0);
    if (v <= 0) return 0u;
    if ((unsigned)v > MENU_VOLUME_MAX) return MENU_VOLUME_MAX;
    return (unsigned)v;
}

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static int is_muted(void) {
    LONG m = InterlockedCompareExchange(&g_muted, 0, 0);
    if (m < 0) {
        const char *env = getenv("MB_MUTE");
        int val = (env && (env[0] == '1' || env[0] == 'y' || env[0] == 'Y' ||
                           env[0] == 't' || env[0] == 'T' ||
                           ieq(env, "on") || ieq(env, "true") || ieq(env, "yes"))) ? 1 : 0;
        InterlockedCompareExchange(&g_muted, val ? 1 : 0, -1);
        return val;
    }
    return m != 0;
}

void host_audio_set_muted(int muted) {
    InterlockedExchange(&g_muted, muted ? 1 : 0);
}

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

struct host_audio {
    cdimage *cd;
    int track;
    int menu_mix;
    uint32_t frame;
    volatile LONG stop;
    volatile LONG fade_active;
    ULONGLONG fade_start_ms;
    unsigned fade_duration_ms;
    HANDLE thread;
    HWAVEOUT wave;
    int16_t *movie_pcm;
    size_t movie_samples;
    unsigned movie_rate, movie_channels;
    volatile LONG movie_state;
    volatile LONG movie_position_ms;
    volatile LONG movie_paused;
};

static uint32_t level_to_gain_q16(unsigned level) {
    unsigned word = host_audio_volume_word(level);
    return word ? (uint32_t)word + 1u : 0u;
}

static uint32_t stream_gain_q16(const host_audio *a) {
    uint32_t g = level_to_gain_q16(current_volume_level());
    if (g && a && InterlockedCompareExchange((volatile LONG *)&a->fade_active, 0, 0)) {
        ULONGLONG now = GetTickCount64();
        ULONGLONG elapsed = now > a->fade_start_ms ? (now - a->fade_start_ms) : 0;
        if (!a->fade_duration_ms || elapsed >= a->fade_duration_ms) return 0u;
        g = (uint32_t)((uint64_t)g * (a->fade_duration_ms - elapsed) / a->fade_duration_ms);
    }
    return g;
}

static void apply_software_gain(int16_t *pcm, size_t samples, uint32_t gain_q16) {
    if (!gain_q16) {
        memset(pcm, 0, samples * sizeof(int16_t));
        return;
    }
    if (gain_q16 >= 65536u) return;
    for (size_t i = 0; i < samples; ++i)
        pcm[i] = (int16_t)(((int32_t)pcm[i] * (int32_t)gain_q16) >> 16);
}

#define BUF_POOL 4
#define BUF_FRAMES 2
#define BUF_SAMPLES (CDDA_SAMPLES_PER_SECTOR * BUF_FRAMES)

static DWORD WINAPI audio_thread(void *param) {
    host_audio *a = (host_audio *)param;
    if (is_muted()) {
        while (!a->stop) Sleep(5);
        return 0;
    }
    WAVEFORMATEX fmt = {0};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = 44100;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = 44100 * 4;
    if (waveOutOpen(&a->wave, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        return 1;

    int16_t *pcm[BUF_POOL] = {0};
    WAVEHDR hdr[BUF_POOL] = {0};
    int prepared[BUF_POOL] = {0};
    int eof = 0;
    for (int i = 0; i < BUF_POOL; i++) {
        pcm[i] = malloc(BUF_SAMPLES * sizeof(int16_t));
        if (!pcm[i]) { eof = 1; break; }
        hdr[i].lpData = (LPSTR)pcm[i];
    }
    while (!a->stop && !eof) {
        int active = 0;
        for (int i = 0; i < BUF_POOL && !a->stop; i++) {
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
            size_t n_samples = (size_t)count * CDDA_SAMPLES_PER_SECTOR;
            uint32_t gain = stream_gain_q16(a);
            if (!gain) {
                memset(pcm[i], 0, n_samples * sizeof(int16_t));
            } else {
                if (a->menu_mix) cdda_mix_menu(pcm[i], count * (CDDA_SAMPLES_PER_SECTOR / 2));
                if (!stereo_on()) host_audio_mono_downmix(pcm[i], count * (CDDA_SAMPLES_PER_SECTOR / 2));
                apply_software_gain(pcm[i], n_samples, gain);
            }
            hdr[i].dwBufferLength = (DWORD)(n_samples * sizeof(int16_t));
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
        for (int i = 0; i < BUF_POOL; i++)
            while (prepared[i] && !(hdr[i].dwFlags & WHDR_DONE) && !a->stop) Sleep(5);
    }
    waveOutReset(a->wave);
    for (int i = 0; i < BUF_POOL; i++) {
        if (prepared[i]) waveOutUnprepareHeader(a->wave, &hdr[i], sizeof hdr[i]);
        free(pcm[i]);
    }
    waveOutClose(a->wave);
    a->wave = NULL;
    return 0;
}

#define MOVIE_BUF_POOL 4
#define MOVIE_CHUNK_FRAMES 1024u

static DWORD WINAPI movie_thread(void *param) {
    host_audio *a = param;
    uint64_t total_ms = (uint64_t)a->movie_samples * 1000u / a->movie_channels / a->movie_rate;
    if (is_muted()) {
        ULONGLONG last = GetTickCount64();
        uint64_t pos_ms = 0;
        InterlockedExchange(&a->movie_state, 1);
        while (!a->stop && pos_ms < total_ms) {
            ULONGLONG now = GetTickCount64();
            if (InterlockedCompareExchange(&a->movie_paused, 0, 0) == 0 && now > last) {
                pos_ms += (uint64_t)(now - last);
                if (pos_ms > total_ms) pos_ms = total_ms;
                InterlockedExchange(&a->movie_position_ms, (LONG)pos_ms);
            }
            last = now;
            Sleep(5);
        }
        if (!a->stop) {
            InterlockedExchange(&a->movie_position_ms, (LONG)total_ms);
            InterlockedExchange(&a->movie_state, 2);
        }
        return 0;
    }
    WAVEFORMATEX fmt = {0};
    int16_t *pcm[MOVIE_BUF_POOL] = {0};
    WAVEHDR hdr[MOVIE_BUF_POOL] = {0};
    int prepared[MOVIE_BUF_POOL] = {0};
    size_t chunk_samples = (size_t)MOVIE_CHUNK_FRAMES * a->movie_channels;
    size_t cursor = 0;
    int paused = 0;
    int completed = 0;
    int started = 0;
    int alloc_fail = 0;
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
    for (int i = 0; i < MOVIE_BUF_POOL; i++) {
        pcm[i] = malloc(chunk_samples * sizeof(int16_t));
        if (!pcm[i]) { alloc_fail = 1; break; }
        hdr[i].lpData = (LPSTR)pcm[i];
    }
    if (alloc_fail) {
        InterlockedExchange(&a->movie_state, -1);
        for (int i = 0; i < MOVIE_BUF_POOL; i++) free(pcm[i]);
        waveOutClose(a->wave);
        a->wave = NULL;
        return 1;
    }
    while (!a->stop) {
        int want_pause = InterlockedCompareExchange(&a->movie_paused, 0, 0) != 0;
        if (want_pause != paused) {
            if (want_pause) waveOutPause(a->wave); else waveOutRestart(a->wave);
            paused = want_pause;
        }
        int active = 0;
        int write_err = 0;
        for (int i = 0; i < MOVIE_BUF_POOL && !a->stop; i++) {
            if (prepared[i]) {
                if (!(hdr[i].dwFlags & WHDR_DONE)) { active = 1; continue; }
                waveOutUnprepareHeader(a->wave, &hdr[i], sizeof hdr[i]);
                prepared[i] = 0;
            }
            if (paused || cursor >= a->movie_samples) continue;
            size_t take = a->movie_samples - cursor;
            if (take > chunk_samples) take = chunk_samples;
            memcpy(pcm[i], a->movie_pcm + cursor, take * sizeof(int16_t));
            cursor += take;
            uint32_t gain = stream_gain_q16(a);
            if (!gain) {
                memset(pcm[i], 0, take * sizeof(int16_t));
            } else {
                if (a->movie_channels == 2u && !stereo_on())
                    host_audio_mono_downmix(pcm[i], take / 2u);
                apply_software_gain(pcm[i], take, gain);
            }
            hdr[i].dwBufferLength = (DWORD)(take * sizeof(int16_t));
            hdr[i].dwFlags = 0;
            if (waveOutPrepareHeader(a->wave, &hdr[i], sizeof hdr[i]) != MMSYSERR_NOERROR) {
                write_err = 1;
                break;
            }
            prepared[i] = 1;
            if (waveOutWrite(a->wave, &hdr[i], sizeof hdr[i]) != MMSYSERR_NOERROR) {
                waveOutUnprepareHeader(a->wave, &hdr[i], sizeof hdr[i]);
                prepared[i] = 0;
                write_err = 1;
                break;
            }
            active = 1;
            if (!started) {
                InterlockedExchange(&a->movie_state, 1);
                started = 1;
            }
        }
        if (write_err) {
            InterlockedExchange(&a->movie_state, -1);
            break;
        }
        if (started) {
            MMTIME pos = {0};
            pos.wType = TIME_MS;
            if (waveOutGetPosition(a->wave, &pos, sizeof pos) == MMSYSERR_NOERROR) {
                uint64_t ms;
                if (pos.wType == TIME_MS) ms = pos.u.ms;
                else if (pos.wType == TIME_SAMPLES) ms = (uint64_t)pos.u.sample * 1000u / fmt.nSamplesPerSec;
                else if (pos.wType == TIME_BYTES) ms = (uint64_t)pos.u.cb * 1000u / fmt.nAvgBytesPerSec;
                else { InterlockedExchange(&a->movie_state, -1); break; }
                if (ms > total_ms) ms = total_ms;
                InterlockedExchange(&a->movie_position_ms, (LONG)ms);
            } else {
                InterlockedExchange(&a->movie_state, -1);
                break;
            }
        }
        if (!active && !paused && cursor >= a->movie_samples) {
            if (!a->stop && InterlockedCompareExchange(&a->movie_state, 0, 0) == 1) {
                InterlockedExchange(&a->movie_position_ms, (LONG)total_ms);
                InterlockedExchange(&a->movie_state, 2);
                completed = 1;
            }
            break;
        }
        Sleep(5);
    }
    if (completed) Sleep(50);
    else waveOutReset(a->wave);
    for (int i = 0; i < MOVIE_BUF_POOL; i++) {
        if (prepared[i]) waveOutUnprepareHeader(a->wave, &hdr[i], sizeof hdr[i]);
        free(pcm[i]);
    }
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
    if (duration_ms && !is_muted() && audio->thread) {
        audio->fade_start_ms = GetTickCount64();
        audio->fade_duration_ms = duration_ms;
        InterlockedExchange(&audio->fade_active, 1);
        Sleep(duration_ms + 40u);
    }
    host_audio_stop(audio);
}

#define SFX_OUTPUT_RATE 44100u
#define SFX_BLOCK_FRAMES 128u
#define SFX_MAX_VOICES 32u

typedef struct {
    int16_t *mono;
    size_t length;
    uint64_t position;
    unsigned rate;
} sfx_voice;

static SRWLOCK sfx_lock = SRWLOCK_INIT;
static sfx_voice sfx_voices[SFX_MAX_VOICES];
static int sfx_running;

static void free_sfx_voice(sfx_voice *voice) {
    free(voice->mono);
    memset(voice, 0, sizeof *voice);
}

/* One stereo output stream mixes overlapping one-shots before handing them to WinMM. */
static int fill_sfx_block(int16_t *stereo) {
    int used = 0;
    uint32_t gain = is_muted() ? 0u : level_to_gain_q16(current_volume_level());
    AcquireSRWLockExclusive(&sfx_lock);
    for (unsigned i = 0; i < SFX_BLOCK_FRAMES; ++i) {
        int sample = 0;
        for (unsigned j = 0; j < SFX_MAX_VOICES; ++j) {
            sfx_voice *voice = &sfx_voices[j];
            if (!voice->mono) continue;
            used = 1;
            size_t index = (size_t)(voice->position / SFX_OUTPUT_RATE);
            unsigned fraction = (unsigned)(voice->position % SFX_OUTPUT_RATE);
            int a = voice->mono[index];
            int b = index + 1u < voice->length ? voice->mono[index + 1u] : a;
            sample += a + (b - a) * (int)fraction / (int)SFX_OUTPUT_RATE;
            voice->position += voice->rate;
            if (voice->position / SFX_OUTPUT_RATE >= voice->length)
                free_sfx_voice(voice);
        }
        sample = (int)(((int64_t)sample * (int64_t)gain) >> 16);
        if (sample < -32768) sample = -32768;
        if (sample > 32767) sample = 32767;
        stereo[2u*i] = stereo[2u*i + 1u] = (int16_t)sample;
    }
    ReleaseSRWLockExclusive(&sfx_lock);
    return used;
}

static int sfx_pending(void) {
    for (unsigned i = 0; i < SFX_MAX_VOICES; ++i)
        if (sfx_voices[i].mono) return 1;
    return 0;
}

static DWORD WINAPI pcm8_thread(void *unused) {
    (void)unused;
    if (is_muted()) {
        AcquireSRWLockExclusive(&sfx_lock);
        for (unsigned i = 0; i < SFX_MAX_VOICES; ++i) free_sfx_voice(&sfx_voices[i]);
        sfx_running = 0;
        ReleaseSRWLockExclusive(&sfx_lock);
        return 0;
    }
    WAVEFORMATEX fmt = {0};
    HWAVEOUT wave = NULL;
    HANDLE event = CreateEventW(NULL, FALSE, FALSE, NULL);
    int16_t pcm[2][SFX_BLOCK_FRAMES * 2u];
    WAVEHDR headers[2] = {0};
    unsigned prepared = 0;
    int failed = 1;
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = SFX_OUTPUT_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = fmt.nBlockAlign * SFX_OUTPUT_RATE;
    if (!event || waveOutOpen(&wave, WAVE_MAPPER, &fmt, (DWORD_PTR)event,
                              0, CALLBACK_EVENT) != MMSYSERR_NOERROR)
        goto done;
    for (; prepared < 2u; ++prepared) {
        headers[prepared].lpData = (LPSTR)pcm[prepared];
        headers[prepared].dwBufferLength = (DWORD)sizeof pcm[prepared];
        if (waveOutPrepareHeader(wave, &headers[prepared], sizeof headers[prepared]) != MMSYSERR_NOERROR)
            goto done;
    }

    for (;;) {
        if (is_muted()) goto done;
        unsigned queued = 0;
        for (unsigned i = 0; i < prepared; ++i) {
            if (headers[i].dwFlags & WHDR_INQUEUE) {
                ++queued;
            } else if (fill_sfx_block(pcm[i])) {
                if (waveOutWrite(wave, &headers[i], sizeof headers[i]) != MMSYSERR_NOERROR)
                    goto done;
                ++queued;
            }
        }
        if (!queued) {
            AcquireSRWLockExclusive(&sfx_lock);
            int pending = sfx_pending();
            if (!pending) sfx_running = 0;
            ReleaseSRWLockExclusive(&sfx_lock);
            if (pending) continue;
            failed = 0;
            break;
        }
        if (WaitForSingleObject(event, 250) != WAIT_OBJECT_0) goto done;
    }

done:
    if (wave) {
        waveOutReset(wave);
        for (unsigned i = 0; i < prepared; ++i)
            waveOutUnprepareHeader(wave, &headers[i], sizeof headers[i]);
        waveOutClose(wave);
    }
    if (event) CloseHandle(event);
    if (failed) {
        AcquireSRWLockExclusive(&sfx_lock);
        for (unsigned i = 0; i < SFX_MAX_VOICES; ++i) free_sfx_voice(&sfx_voices[i]);
        sfx_running = 0;
        ReleaseSRWLockExclusive(&sfx_lock);
    }
    return (DWORD)failed;
}

int host_audio_play_pcm8(const int8_t *pcm, size_t length, unsigned rate) {
    if (!pcm || !length || rate < 4000u || rate > 48000u) return -1;
    if (is_muted() || current_volume_level() == 0u) return 0;
    if (length > 132300u) length = 132300u;
    int16_t *mono = malloc(length * sizeof *mono);
    if (!mono) return -1;
    size_t fade = length / 8u;
    if (fade > 48u) fade = 48u;
    for (size_t i = 0; i < length; ++i) {
        int value = (int)pcm[i] * 72;
        if (fade > 1u) {
            if (i < fade) value = value * (int)i / (int)fade;
            else if (i >= length - fade) value = value * (int)(length - 1u - i) / (int)fade;
        }
        mono[i] = (int16_t)value;
    }
    int result = -1;
    AcquireSRWLockExclusive(&sfx_lock);
    for (unsigned i = 0; i < SFX_MAX_VOICES; ++i) {
        if (sfx_voices[i].mono) continue;
        sfx_voices[i] = (sfx_voice){mono, length, 0, rate};
        if (!sfx_running) {
            HANDLE thread = CreateThread(NULL, 0, pcm8_thread, NULL, 0, NULL);
            if (!thread) {
                memset(&sfx_voices[i], 0, sizeof sfx_voices[i]);
                break;
            }
            sfx_running = 1;
            CloseHandle(thread);
        }
        result = 0;
        break;
    }
    ReleaseSRWLockExclusive(&sfx_lock);
    if (result) free(mono);
    return result;
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
void host_audio_set_muted(int muted) { (void)muted; }
void host_audio_set_stereo(int on) { (void)on; }
int host_audio_play_pcm8(const int8_t *pcm, size_t length, unsigned rate) {
    (void)pcm; (void)length; (void)rate; return -1;
}
#endif
