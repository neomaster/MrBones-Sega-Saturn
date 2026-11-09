#include "duck_avi.h"
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifdef MB_USE_FFMPEG
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
const AVCodec *mb_saturn_truemotion_decoder(void);
typedef struct {
    AVCodecContext *codec;
    AVFrame *frame;
    AVCodecContext *saturn_codec;
    AVFrame *saturn_frame;
    struct SwsContext *scale;
    uint8_t *scratch;
    size_t scratch_size;
} video_decoder;

static void video_free(video_decoder *v) {
    if (!v) return;
    sws_freeContext(v->scale);
    av_frame_free(&v->frame);
    avcodec_free_context(&v->codec);
    av_frame_free(&v->saturn_frame);
    avcodec_free_context(&v->saturn_codec);
    free(v->scratch);
    free(v);
}

static video_decoder *video_open(void) {
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_TRUEMOTION1);
    const AVCodec *saturn_codec = mb_saturn_truemotion_decoder();
    video_decoder *v = calloc(1, sizeof *v);
    if (!v) return NULL;
    v->codec = codec ? avcodec_alloc_context3(codec) : NULL;
    v->frame = av_frame_alloc();
    v->saturn_codec = avcodec_alloc_context3(saturn_codec);
    v->saturn_frame = av_frame_alloc();
    if (v->codec) { v->codec->width = 320; v->codec->height = 208; }
    if (v->saturn_codec) { v->saturn_codec->width = 320; v->saturn_codec->height = 208; }
    if (!v->codec || !v->frame || !v->saturn_codec || !v->saturn_frame ||
        avcodec_open2(v->codec, codec, NULL) < 0 ||
        avcodec_open2(v->saturn_codec, saturn_codec, NULL) < 0) {
        video_free(v);
        return NULL;
    }
    return v;
}

static int video_step(video_decoder *v, const uint8_t *data, unsigned size, uint8_t *rgba) {
    AVPacket *packet = av_packet_alloc();
    int ret, w, h, x, y, pitch;
    uint8_t *dst[4];
    int stride[4] = {0, 0, 0, 0};
    if (!packet || size > INT_MAX || av_new_packet(packet, (int)size) < 0) {
        av_packet_free(&packet);
        return -1;
    }
    memcpy(packet->data, data, size);
    ret = avcodec_send_packet(v->codec, packet);
    if (ret >= 0) ret = avcodec_send_packet(v->saturn_codec, packet);
    av_packet_free(&packet);
    if (ret < 0 || avcodec_receive_frame(v->codec, v->frame) < 0 ||
        avcodec_receive_frame(v->saturn_codec, v->saturn_frame) < 0) return -1;
    w = v->frame->width;
    h = v->frame->height;
    if (w < 1 || h < 1 || w > 320 || h > 224) return -1;
    if (v->codec->sample_aspect_ratio.num > 0 && v->codec->sample_aspect_ratio.den > 0) {
        int64_t display_w = (int64_t)w * v->codec->sample_aspect_ratio.num /
                            v->codec->sample_aspect_ratio.den;
        if (display_w > 0 && display_w <= 320) w = (int)display_w;
    }
    x = (320 - w) / 2;
    y = (224 - h) / 2;
    v->scale = sws_getCachedContext(v->scale, v->frame->width, v->frame->height,
                                  v->frame->format, w, h, AV_PIX_FMT_RGBA,
                                  SWS_POINT, NULL, NULL, NULL);
    if (!v->scale) return -1;
    pitch = (w * 4 + 63) & ~31;
    if ((size_t)pitch * h > v->scratch_size) {
        uint8_t *p = realloc(v->scratch, (size_t)pitch * h);
        if (!p) return -1;
        v->scratch = p;
        v->scratch_size = (size_t)pitch * h;
    }
    dst[0] = v->scratch;
    stride[0] = pitch;
    dst[1] = dst[2] = dst[3] = NULL;
    if (sws_scale(v->scale, (const uint8_t *const *)v->frame->data,
                  v->frame->linesize, 0, v->frame->height, dst, stride) != h) return -1;
    for (int i = 0; i < 320 * 224; i++) {
        rgba[i * 4] = 0; rgba[i * 4 + 1] = 0; rgba[i * 4 + 2] = 0; rgba[i * 4 + 3] = 255;
    }
    if (v->frame->format == AV_PIX_FMT_BGR0 && w == v->frame->width * 2 &&
        v->saturn_frame->format == v->frame->format &&
        v->saturn_frame->width == v->frame->width && v->saturn_frame->height == h) {
        for (int row = 0; row < h; row++) {
            const uint8_t *src = v->frame->data[0] + row * v->frame->linesize[0];
            const uint8_t *aux = v->saturn_frame->data[0] + row * v->saturn_frame->linesize[0];
            uint8_t *out = rgba + ((y + row) * 320 + x) * 4;
            for (int col = 0; col < v->frame->width; col++) {
                uint8_t diff = (uint8_t)(aux[4 * col + 3] - src[4 * col + 3]);
                out[8 * col] = (uint8_t)(src[4 * col + 2] + diff);
                out[8 * col + 1] = (uint8_t)(src[4 * col + 1] + diff);
                out[8 * col + 2] = src[4 * col];
                out[8 * col + 3] = 255;
                out[8 * col + 4] = src[4 * col + 2];
                out[8 * col + 5] = src[4 * col + 1];
                out[8 * col + 6] = (uint8_t)(src[4 * col] - diff);
                out[8 * col + 7] = 255;
            }
        }
        return 0;
    }
    for (int row = 0; row < h; row++)
        memcpy(rgba + ((y + row) * 320 + x) * 4, v->scratch + (size_t)row * pitch, (size_t)w * 4);
    return 0;
}
#endif

#define SCR_W 320u
#define SCR_H 224u

static unsigned le16(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}
static unsigned le32(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
           ((unsigned)p[3] << 24);
}

static int validate_avi_streams(duck_avi *avi, const uint8_t *buf, size_t size,
                                uint64_t *audio_bytes_expected, unsigned *video_frames_expected) {
    size_t pos = 12;
    unsigned streams = 0;
    int video = 0;
    while (pos + 12 <= size) {
        unsigned len = le32(buf + pos + 4);
        size_t padded = (size_t)len + (len & 1u);
        if (padded > size - pos - 8) return -1;
        if (!memcmp(buf + pos, "LIST", 4) && len >= 4 &&
            !memcmp(buf + pos + 8, "hdrl", 4)) {
            size_t end = pos + 8 + len, sub = pos + 12;
            while (sub + 8 <= end) {
                unsigned slen = le32(buf + sub + 4);
                size_t spad = (size_t)slen + (slen & 1u);
                if (spad > end - sub - 8) return -1;
                if (!memcmp(buf + sub, "LIST", 4) && slen >= 4 &&
                    !memcmp(buf + sub + 8, "strl", 4)) {
                    const uint8_t *hdr = NULL, *fmt = NULL;
                    unsigned hlen = 0, flen = 0;
                    size_t limit = sub + 8 + slen, cur = sub + 12;
                    while (cur + 8 <= limit) {
                        unsigned n = le32(buf + cur + 4);
                        size_t pad = (size_t)n + (n & 1u);
                        if (pad > limit - cur - 8) return -1;
                        if (!memcmp(buf + cur, "strh", 4)) { hdr = buf + cur + 8; hlen = n; }
                        if (!memcmp(buf + cur, "strf", 4)) { fmt = buf + cur + 8; flen = n; }
                        cur += 8 + pad;
                    }
                    if (cur != limit || !hdr || hlen < 8 || !fmt) return -1;
                    if (streams == 0 && !memcmp(hdr, "vidsDUCK", 8) &&
                        flen >= 40 && !memcmp(fmt + 16, "DUCK", 4)) {
                        video = 1;
                        if (hlen >= 48 && !le32(hdr + 44))
                            *video_frames_expected = le32(hdr + 32);
                        if (hlen >= 28 && le32(hdr + 20) && le32(hdr + 24) &&
                            (uint64_t)le32(hdr + 20) * 1000000u >= le32(hdr + 24)) {
                            avi->frame_scale = le32(hdr + 20);
                            avi->frame_rate = le32(hdr + 24);
                        }
                    } else if (streams == 1 && !memcmp(hdr, "auds", 4) && flen >= 16 &&
                               (le16(fmt) == 0x62 || (le16(fmt) == 1 && le16(fmt + 14) == 16)) &&
                               (le16(fmt + 2) == 1 || le16(fmt + 2) == 2) &&
                               le32(fmt + 4) >= 4000 && le32(fmt + 4) <= 192000 &&
                               le16(fmt + 12) != 0 &&
                               (le16(fmt) != 0x62 || (le16(fmt + 2) == 2 && le16(fmt + 12) >= 16)) &&
                               (le16(fmt) != 1 || le16(fmt + 12) == le16(fmt + 2) * 2)) {
                        avi->audio_dk3_unsupported = le16(fmt) == 0x62;
                        avi->pcm_channels = le16(fmt + 2);
                        avi->pcm_rate = le32(fmt + 4);
                        avi->audio_block_align = le16(fmt + 12);
                        if (hlen >= 48)
                            *audio_bytes_expected = (uint64_t)le32(hdr + 32) * le32(hdr + 44);
                    } else return -1;
                    streams++;
                }
                sub += 8 + spad;
            }
            return sub == end && video ? 0 : -1;
        }
        pos += 8 + padded;
    }
    return -1;
}

static void read_avi_header(duck_avi *avi, const uint8_t *p, size_t size) {
    size_t pos = 0;
    while (size - pos >= 8) {
        unsigned len = le32(p + pos + 4);
        size_t padded = (size_t)len + (len & 1u);
        if (padded > size - pos - 8) break;
        if (!memcmp(p + pos, "avih", 4) && len >= 40) {
            unsigned interval = le32(p + pos + 8);
            if (interval) avi->us_per_frame = interval;
        } else if (!memcmp(p + pos, "LIST", 4) && len >= 4 &&
                   !memcmp(p + pos + 8, "hdrl", 4)) {
            read_avi_header(avi, p + pos + 12, len - 4);
        }
        pos += 8 + padded;
    }
}

static void fill_bg(uint8_t *rgba, uint8_t r, uint8_t g, uint8_t b) {
    unsigned i;
    for (i = 0; i < SCR_W * SCR_H; i++) {
        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = 255;
    }
}

static int append_pcm(duck_avi *avi, const uint8_t *pcm, unsigned size) {
    size_t n, old;
    int16_t *nbuf;
    if (!size) return 0;
    if (!pcm || size < 2) return -1;
#ifdef MB_USE_FFMPEG
    if (avi->audio_decoder) {
        AVCodecContext *codec = avi->audio_decoder;
        AVPacket *packet = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        unsigned pos = 0;
        int result = 0;
        if (!packet || !frame) { av_packet_free(&packet); av_frame_free(&frame); return -1; }
        if (!avi->audio_block_align || size % avi->audio_block_align) {
            av_packet_free(&packet);
            av_frame_free(&frame);
            return -1;
        }
        while (pos < size) {
            unsigned block = avi->audio_block_align;
            int ret;
            if (!block || block > size - pos) block = size - pos;
            if (block > INT_MAX || av_new_packet(packet, (int)block) < 0) { result = -1; break; }
            memcpy(packet->data, pcm + pos, block);
            ret = avcodec_send_packet(codec, packet);
            av_packet_unref(packet);
            if (ret < 0) { result = -1; break; }
            while ((ret = avcodec_receive_frame(codec, frame)) == 0) {
                n = (size_t)frame->nb_samples * (size_t)frame->ch_layout.nb_channels;
                old = avi->pcm_samples;
                if (frame->format != AV_SAMPLE_FMT_S16 || n > (SIZE_MAX / sizeof(int16_t)) - old) { result = -1; break; }
                if (old + n > avi->pcm_capacity) {
                    size_t cap = avi->pcm_capacity ? avi->pcm_capacity : 8192;
                    while (cap < old + n && cap <= SIZE_MAX / 2) cap *= 2;
                    if (cap < old + n || cap > SIZE_MAX / sizeof(int16_t)) { result = -1; break; }
                    nbuf = realloc(avi->pcm, cap * sizeof(int16_t));
                    if (!nbuf) { result = -1; break; }
                    avi->pcm = nbuf;
                    avi->pcm_capacity = cap;
                }
                memcpy(avi->pcm + old, frame->data[0], n * sizeof(int16_t));
                avi->pcm_samples += n;
                av_frame_unref(frame);
            }
            if (!result && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) result = -1;
            if (result) break;
            pos += block;
        }
        av_packet_free(&packet);
        av_frame_free(&frame);
        return result;
    }
#endif
    if (avi->audio_dk3_unsupported) return -1;
    if (!avi->audio_block_align || size % avi->audio_block_align) return -1;
    n = size / 2u;
    old = avi->pcm_samples;
    if (n > SIZE_MAX / sizeof(int16_t) - old) return -1;
    nbuf = (int16_t *)realloc(avi->pcm, (old + n) * sizeof(int16_t));
    if (!nbuf) return -1;
    avi->pcm = nbuf;
    memcpy(avi->pcm + old, pcm, n * sizeof(int16_t));
    avi->pcm_samples = old + n;
    return 0;
}

static int add_frame(duck_avi *avi, const uint8_t *data, unsigned size,
                     unsigned audio_off, unsigned audio_size) {
    duck_avi_frame *f;
    if (avi->frame_count == avi->frame_capacity) {
        size_t cap = avi->frame_capacity ? avi->frame_capacity * 2 : 256;
        if (cap < avi->frame_capacity || cap > SIZE_MAX / sizeof *f) return -1;
        f = realloc(avi->frames, cap * sizeof *f);
        if (!f) return -1;
        avi->frames = f;
        avi->frame_capacity = cap;
    }
    f = &avi->frames[avi->frame_count++];
    f->data = data; f->size = size; f->pcm_off = audio_off; f->pcm_size = audio_size;
    return 0;
}

static int ingest_rec_list(duck_avi *avi, const uint8_t *base, size_t end,
                           uint64_t *audio_bytes_seen) {
    size_t i = 0;
    unsigned first = avi->frame_count, pcm_off = 0, pcm_sz = 0;

    while (i <= end && end - i >= 8) {
        const uint8_t *p = base + i;
        unsigned szc = le32(p + 4);
        size_t padded = (size_t)szc + (szc & 1u);
        size_t next;
        if (padded > end - i - 8) return -1;
        next = i + 8 + padded;
        if (!memcmp(p, "00dc", 4) || !memcmp(p, "00db", 4)) {
            if (add_frame(avi, p + 8, szc, 0, 0)) return -1;
        } else if (!memcmp(p, "01wb", 4)) {
            pcm_off = (unsigned)(p + 8 - avi->blob);
            pcm_sz = szc;
            if (append_pcm(avi, p + 8, szc)) return -1;
            *audio_bytes_seen += szc;
        }
        i = next;
    }
    if (i != end) return -1;
    for (unsigned j = first; j < avi->frame_count; j++) {
        avi->frames[j].pcm_off = pcm_off;
        avi->frames[j].pcm_size = pcm_sz;
    }
    return avi->frame_count != first;
}

int duck_avi_open_with_duck(duck_avi *avi, const void *blob, size_t size,
                            const void *duck19, size_t duck_size) {
    const uint8_t *buf;
    size_t i;
    size_t movi = 0, movi_end = 0, riff_end;
    uint64_t audio_bytes_expected = 0, audio_bytes_seen = 0;
    unsigned video_frames_expected = 0;

    if (!avi) return -1;
    memset(avi, 0, sizeof *avi);
    if (!blob || size < 16) {
        snprintf(avi->status, sizeof avi->status, "duck_avi: truncated header");
        return -1;
    }
    buf = (const uint8_t *)blob;
    if (memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "AVI ", 4)) {
        snprintf(avi->status, sizeof avi->status, "duck_avi: not RIFF/AVI");
        return -1;
    }
    riff_end = (size_t)le32(buf + 4) + 8u;
    if (riff_end < 12 || riff_end > size) {
        snprintf(avi->status, sizeof avi->status, "duck_avi: truncated RIFF");
        return -1;
    }
    size = riff_end;
    if (validate_avi_streams(avi, buf, size, &audio_bytes_expected, &video_frames_expected)) {
        snprintf(avi->status, sizeof avi->status, "duck_avi: unsupported or invalid stream header");
        return -1;
    }
    avi->blob = buf;
    avi->blob_size = size;
    avi->width = 320;
    avi->height = 208;
    avi->us_per_frame = 66666; /* ~15 fps default */
    read_avi_header(avi, buf + 12, size - 12);

#ifdef MB_USE_FFMPEG
    (void)duck19;
    (void)duck_size;
    avi->video_decoder = video_open();
    if (!avi->video_decoder) {
        snprintf(avi->status, sizeof avi->status, "duck_avi: TrueMotion1 decoder unavailable");
        return -1;
    }
    if (avi->audio_dk3_unsupported) {
        const AVCodec *decoder = avcodec_find_decoder(AV_CODEC_ID_ADPCM_IMA_DK3);
        AVCodecContext *audio = decoder ? avcodec_alloc_context3(decoder) : NULL;
        if (audio) {
            audio->sample_rate = avi->pcm_rate;
            audio->block_align = avi->audio_block_align;
            av_channel_layout_default(&audio->ch_layout, avi->pcm_channels);
            if (avcodec_open2(audio, decoder, NULL) == 0) {
                avi->audio_decoder = audio;
                avi->audio_dk3_unsupported = 0;
            } else avcodec_free_context(&audio);
        }
    }
    if (avi->audio_dk3_unsupported) {
        duck_avi_free(avi);
        snprintf(avi->status, sizeof avi->status, "duck_avi: DK3 decoder unavailable");
        return -1;
    }
#else
    (void)duck19;
    (void)duck_size;
    snprintf(avi->status, sizeof avi->status, "duck_avi: TrueMotion1 decoder unavailable");
    return -1;
#endif

    for (i = 12; i < size;) {
        const uint8_t *p = buf + i;
        if (size - i < 8) goto fail;
        unsigned szc = le32(p + 4);
        size_t padded = (size_t)szc + (szc & 1u);
        size_t next;
        if (padded > size - i - 8) goto fail;
        next = i + 8 + padded;

        if (!memcmp(p, "avih", 4) && szc >= 40) {
            avi->us_per_frame = le32(p + 8 + 0);
            if (!avi->us_per_frame) avi->us_per_frame = 66666;
            avi->width = le32(p + 8 + 32);
            avi->height = le32(p + 8 + 36);
            if (!avi->width) avi->width = 320;
            if (!avi->height) avi->height = 208;
        }
        if (!memcmp(p, "strf", 4) && szc >= 16) {
            unsigned w = le32(p + 8 + 4), h = le32(p + 8 + 8);
            if (w >= 16 && w <= 512 && h >= 16 && h <= 512) {
                avi->width = w;
                avi->height = h;
            } else if (szc >= 16) {
                unsigned rate = le32(p + 8 + 4);
                unsigned ch = le16(p + 8 + 0);
                if (rate >= 4000 && rate <= 48000) avi->pcm_rate = rate;
                if (ch == 1 || ch == 2) avi->pcm_channels = ch;
            }
        }
        if (!memcmp(p, "LIST", 4) && i + 12 <= size && !memcmp(p + 8, "movi", 4)) {
            movi = i + 12;
            movi_end = i + 8 + szc;
        }
        i = next;
    }

    if (!movi || movi_end <= movi) {
        goto fail;
    }

    for (i = movi; i < movi_end;) {
        const uint8_t *p = buf + i;
        if (movi_end - i < 8) goto fail;
        unsigned szc = le32(p + 4);
        size_t padded = (size_t)szc + (szc & 1u);
        size_t next;
        if (padded > movi_end - i - 8) goto fail;
        next = i + 8 + padded;
        if (!memcmp(p, "LIST", 4)) {
            if (szc < 4 || ingest_rec_list(avi, p + 12, (size_t)szc - 4,
                                           &audio_bytes_seen) < 0) goto fail;
        } else if (!memcmp(p, "00dc", 4) || !memcmp(p, "00db", 4)) {
            if (add_frame(avi, p + 8, szc, 0, 0)) goto fail;
        } else if (!memcmp(p, "01wb", 4)) {
            if (append_pcm(avi, p + 8, szc)) goto fail;
            audio_bytes_seen += szc;
        }
        i = next;
    }

    if (avi->pcm_channels && !avi->pcm_samples) {
        duck_avi_free(avi);
        snprintf(avi->status, sizeof avi->status, "duck_avi: audio stream has no samples");
        return -1;
    }
    if (audio_bytes_expected && audio_bytes_seen < audio_bytes_expected) {
        duck_avi_free(avi);
        snprintf(avi->status, sizeof avi->status, "duck_avi: incomplete audio stream");
        return -1;
    }
    if (video_frames_expected && avi->frame_count < video_frames_expected) {
        duck_avi_free(avi);
        snprintf(avi->status, sizeof avi->status, "duck_avi: incomplete video stream");
        return -1;
    }

    avi->rgba = (uint8_t *)calloc((size_t)SCR_W * SCR_H * 4u, 1);
    if (!avi->rgba) goto fail;
    fill_bg(avi->rgba, 0, 0, 0);
    snprintf(avi->status, sizeof avi->status,
             "duck_avi %ux%u frames=%u pcm=%zu @%uHz decoder=%s%s", avi->width,
             avi->height, avi->frame_count, avi->pcm_samples, avi->pcm_rate,
             avi->video_decoder ? "ffmpeg" : "approx",
             avi->audio_dk3_unsupported ? " DK3 unsupported" : "");
    if (avi->frame_count > 0) return 0;
fail:
    duck_avi_free(avi);
    snprintf(avi->status, sizeof avi->status, "duck_avi: invalid or truncated AVI");
    return -1;
}

int duck_avi_open(duck_avi *avi, const void *blob, size_t size) {
    return duck_avi_open_with_duck(avi, blob, size, NULL, 0);
}

void duck_avi_free(duck_avi *avi) {
    if (!avi) return;
#ifdef MB_USE_FFMPEG
    video_free((video_decoder *)avi->video_decoder);
    if (avi->audio_decoder) {
        AVCodecContext *audio = avi->audio_decoder;
        avcodec_free_context(&audio);
    }
#endif
    duck_tm1_free(&avi->tm1);
    free(avi->rgba);
    free(avi->pcm);
    free(avi->frames);
    memset(avi, 0, sizeof *avi);
}

int duck_avi_step(duck_avi *avi) {
    const duck_avi_frame *f;
    if (!avi || !avi->rgba) return 0;
    if (avi->cur >= avi->frame_count) return 0;
    f = &avi->frames[avi->cur++];
    if (f->size == 0) {
        avi->frames_drawn++;
        return 1;
    }
#ifdef MB_USE_FFMPEG
    /* Compression zero is an interframe repeat, not a pixel-format change. */
    if (f->size >= 15 && f->data[0] >= 0x10) {
        unsigned header_size = ((f->data[0] >> 5) | (f->data[0] << 3)) & 0x7f;
        unsigned version = f->data[10] ^ f->data[11];
        unsigned type = f->data[11] ^ f->data[12];
        unsigned flags = f->data[12] ^ f->data[13];
        if (header_size >= 14 && header_size + 1 <= f->size &&
            !(f->data[1] ^ f->data[2]) && version >= 2 &&
            (type == 2 || type == 3) && (flags & 8u) && !(flags & 32u)) {
            video_decoder *v = avi->video_decoder;
            unsigned height = (f->data[4] ^ f->data[5]) | ((f->data[5] ^ f->data[6]) << 8);
            unsigned width = (f->data[6] ^ f->data[7]) | ((f->data[7] ^ f->data[8]) << 8);
            if (!avi->tm1_frames || !v || v->frame->height != (int)height ||
                v->codec->sample_aspect_ratio.num <= 0 || v->codec->sample_aspect_ratio.den <= 0 ||
                (uint64_t)width * v->codec->sample_aspect_ratio.den !=
                    (uint64_t)v->frame->width * v->codec->sample_aspect_ratio.num) {
                snprintf(avi->status, sizeof avi->status, "duck_avi: invalid repeat frame %u", avi->cur);
                avi->decode_error = 1;
                avi->playback_done = 1;
                return 0;
            }
            avi->frames_drawn++;
            return 1;
        }
    }
    if (video_step((video_decoder *)avi->video_decoder, f->data, f->size, avi->rgba) < 0) {
        snprintf(avi->status, sizeof avi->status, "duck_avi: invalid video frame %u", avi->cur);
        avi->decode_error = 1;
        avi->playback_done = 1;
        return 0;
    }
    avi->tm1_frames++;
#else
    snprintf(avi->status, sizeof avi->status, "duck_avi: TrueMotion1 decoder unavailable");
    avi->decode_error = 1;
    avi->playback_done = 1;
    return 0;
#endif
    avi->frames_drawn++;
    snprintf(avi->status, sizeof avi->status, "duck_avi %u/%u %ux%u tm1=%u soft=%u",
             avi->cur, avi->frame_count, avi->width, avi->height, avi->tm1_frames,
             avi->soft_frames);
    return 1;
}

int duck_avi_done(const duck_avi *avi) {
    return avi && avi->cur >= avi->frame_count;
}

int duck_avi_playback_done(const duck_avi *avi) {
    return avi && avi->playback_done;
}

int duck_avi_tick(duck_avi *avi, unsigned elapsed_us) {
    int advanced = 0;
    if (!avi || !avi->us_per_frame || avi->playback_done) return 0;
    avi->elapsed_us += elapsed_us;
    while (!avi->playback_done) {
        uint64_t period = avi->frame_rate ? (uint64_t)avi->frame_scale * 1000000u : 0;
        uint64_t interval = period ? (period - avi->frame_fraction + avi->frame_rate - 1u) /
                                     avi->frame_rate : avi->us_per_frame;
        if (avi->elapsed_us < interval) break;
        if (duck_avi_done(avi)) {
            uint64_t required = interval;
            if (avi->pcm_rate && avi->pcm_channels) {
                uint64_t samples = avi->pcm_samples / avi->pcm_channels;
                uint64_t audio_us = (samples / avi->pcm_rate) * 1000000u +
                    ((samples % avi->pcm_rate) * 1000000u + avi->pcm_rate - 1u) / avi->pcm_rate;
                uint64_t video_us = (uint64_t)avi->frame_count * avi->us_per_frame;
                if (period) {
                    uint64_t whole = period / avi->frame_rate;
                    uint64_t frac = (uint64_t)avi->frame_count * (period % avi->frame_rate);
                    if (avi->frame_count && whole > UINT64_MAX / avi->frame_count)
                        video_us = UINT64_MAX;
                    else {
                        video_us = whole * avi->frame_count;
                        frac = frac / avi->frame_rate + (frac % avi->frame_rate != 0);
                        video_us = frac > UINT64_MAX - video_us ? UINT64_MAX : video_us + frac;
                    }
                }
                if (audio_us > video_us) required += audio_us - video_us;
            }
            if (avi->elapsed_us < required) break;
            avi->elapsed_us -= required;
            avi->playback_done = 1;
            break;
        }
        avi->elapsed_us -= interval;
        if (period) avi->frame_fraction = (unsigned)(avi->frame_fraction +
                                         interval * avi->frame_rate - period);
        if (!duck_avi_step(avi)) return -1;
        advanced++;
    }
    return advanced;
}

const uint8_t *duck_avi_frame_rgba(const duck_avi *avi) {
    return avi ? avi->rgba : NULL;
}
