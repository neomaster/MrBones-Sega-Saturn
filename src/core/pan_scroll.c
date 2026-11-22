#include "pan_scroll.h"
#include <stdlib.h>
#include <string.h>

static void planes_to_rgba(const uint8_t *planes, const uint8_t *bank,
                           uint8_t *rgba) {
    size_t i;
    for (i = 0; i < PAN_PLANE_SIZE; i++) {
        uint8_t idx = planes[i];
        size_t c = (size_t)idx * 4u;
        if (idx == 0u) {
            rgba[4 * i + 0] = 0;
            rgba[4 * i + 1] = 0;
            rgba[4 * i + 2] = 0;
            rgba[4 * i + 3] = 255;
        } else {
            rgba[4 * i + 0] = bank[c + 1];
            rgba[4 * i + 1] = bank[c + 2];
            rgba[4 * i + 2] = bank[c + 3];
            rgba[4 * i + 3] = 255;
        }
    }
}

static void save_keyframe(pan_scroll *ps, unsigned frame_idx) {
    unsigned kf;
    if (!ps || (frame_idx % PAN_KEYFRAME_STRIDE) != 0u) return;
    kf = frame_idx / PAN_KEYFRAME_STRIDE;
    if (kf >= ps->kf_count || !ps->kf_valid || !ps->kf_planes || !ps->kf_coll)
        return;
    if (ps->kf_valid[kf]) return;
    if (!ps->kf_planes[kf]) {
        ps->kf_planes[kf] = (uint8_t *)malloc(PAN_BUFFER_SIZE);
        if (!ps->kf_planes[kf]) return;
    }
    memcpy(ps->kf_planes[kf], ps->planes, PAN_BUFFER_SIZE);
    ps->kf_coll[kf] = ps->collision;
    ps->kf_valid[kf] = 1u;
}

int pan_scroll_init(pan_scroll *ps, const uint8_t *blob, size_t blob_size,
                    const uint8_t *palette_bank4) {
    size_t off = 0;
    uint32_t sz;
    size_t consumed = 0, csz = 0;
    uint8_t coll_raw[PAN_COLL_SIZE + 480];
    unsigned i;
    if (!ps || !blob || !palette_bank4 || blob_size < 6) return -1;
    memset(ps, 0, sizeof *ps);
    if (pan_stream_count(blob, blob_size, &ps->count) || ps->count == 0) return -1;
    ps->blob = blob;
    ps->blob_size = blob_size;
    memcpy(ps->palette4, palette_bank4, sizeof ps->palette4);
    ps->planes = calloc(1, PAN_BUFFER_SIZE);
    ps->bg_rgba = malloc(PAN_PLANE_SIZE * 4);
    ps->frame_offs = (size_t *)calloc(ps->count, sizeof *ps->frame_offs);
    ps->frame_sizes = (size_t *)calloc(ps->count, sizeof *ps->frame_sizes);
    ps->kf_count = (ps->count + PAN_KEYFRAME_STRIDE - 1u) / PAN_KEYFRAME_STRIDE;
    ps->kf_planes = (uint8_t **)calloc(ps->kf_count, sizeof *ps->kf_planes);
    ps->kf_coll = (pan_collision_map *)calloc(ps->kf_count, sizeof *ps->kf_coll);
    ps->kf_valid = (uint8_t *)calloc(ps->kf_count, sizeof *ps->kf_valid);
    if (!ps->planes || !ps->bg_rgba || !ps->frame_offs || !ps->frame_sizes ||
        !ps->kf_planes || !ps->kf_coll || !ps->kf_valid) {
        pan_scroll_free(ps);
        return -1;
    }
    off = 0;
    for (i = 0; i < ps->count; i++) {
        uint32_t rec_sz = pan_frame_byte_size(blob + off, blob_size - off);
        if (!rec_sz) {
            pan_scroll_free(ps);
            return -1;
        }
        ps->frame_offs[i] = off;
        ps->frame_sizes[i] = (size_t)rec_sz;
        off += (size_t)rec_sz;
    }
    /* Decode record 0 into planes + collision. */
    sz = (uint32_t)ps->frame_sizes[0];
    if (!sz ||
        pan_frame_decode(blob + ps->frame_offs[0], sz, ps->planes, PAN_BUFFER_SIZE,
                         0, coll_raw, sizeof coll_raw, &csz, &consumed) ||
        csz < PAN_COLL_SIZE ||
        pan_collision_map_load(&ps->collision, coll_raw, PAN_COLL_SIZE)) {
        pan_scroll_free(ps);
        return -1;
    }
    ps->index = 0;
    ps->collision0 = ps->collision;
    save_keyframe(ps, 0u);
    planes_to_rgba(ps->planes, ps->palette4, ps->bg_rgba);
    return 0;
}

void pan_scroll_free(pan_scroll *ps) {
    unsigned i;
    if (!ps) return;
    if (ps->kf_planes) {
        for (i = 0; i < ps->kf_count; i++)
            free(ps->kf_planes[i]);
    }
    free(ps->kf_planes);
    free(ps->kf_coll);
    free(ps->kf_valid);
    free(ps->frame_offs);
    free(ps->frame_sizes);
    free(ps->planes);
    free(ps->bg_rgba);
    memset(ps, 0, sizeof *ps);
}

unsigned pan_scroll_index_for_x(const pan_scroll *ps, unsigned pixel_x,
                                unsigned start_px, unsigned finish_px) {
    unsigned span, rel;
    if (!ps || ps->count == 0) return 0;
    if (finish_px <= start_px) return 0;
    if (pixel_x <= start_px) return 0;
    if (pixel_x >= finish_px) return ps->count - 1u;
    span = finish_px - start_px;
    rel = pixel_x - start_px;
    return (unsigned)(((uint64_t)rel * (ps->count - 1u)) / span);
}

int pan_scroll_seek(pan_scroll *ps, unsigned target) {
    uint8_t coll_raw[PAN_COLL_SIZE + 480];
    size_t consumed = 0, csz = 0;
    if (!ps || !ps->blob || ps->count == 0) return -1;
    if (target >= ps->count) target = ps->count - 1u;

    if (target < ps->index) {
        unsigned kf = target / PAN_KEYFRAME_STRIDE;
        int restored = 0;
        if (ps->kf_valid && ps->kf_planes && ps->kf_coll) {
            for (;;) {
                if (kf < ps->kf_count && ps->kf_valid[kf] && ps->kf_planes[kf]) {
                    memcpy(ps->planes, ps->kf_planes[kf], PAN_BUFFER_SIZE);
                    ps->collision = ps->kf_coll[kf];
                    ps->index = kf * PAN_KEYFRAME_STRIDE;
                    restored = 1;
                    break;
                }
                if (kf == 0u) break;
                kf--;
            }
        }
        if (!restored) {
            size_t off0 = ps->frame_offs ? ps->frame_offs[0] : 0u;
            uint32_t sz0 = ps->frame_sizes ? (uint32_t)ps->frame_sizes[0] : 0u;
            memset(ps->planes, 0, PAN_BUFFER_SIZE);
            if (!sz0 && pan_stream_record(ps->blob, ps->blob_size, 0, &off0, &sz0))
                return -1;
            if (pan_frame_decode(ps->blob + off0, sz0, ps->planes, PAN_BUFFER_SIZE, 0,
                                 coll_raw, sizeof coll_raw, &csz, &consumed) ||
                csz < PAN_COLL_SIZE ||
                pan_collision_map_load(&ps->collision, coll_raw, PAN_COLL_SIZE))
                return -1;
            ps->index = 0;
            save_keyframe(ps, 0u);
        }
    }

    while (ps->index < target) {
        size_t off = 0;
        uint32_t sz = 0;
        unsigned next = ps->index + 1u;
        unsigned cur_p = next & 3u;
        if (ps->frame_offs && ps->frame_sizes && next < ps->count) {
            off = ps->frame_offs[next];
            sz = (uint32_t)ps->frame_sizes[next];
        } else if (pan_stream_record(ps->blob, ps->blob_size, next, &off, &sz)) {
            return -1;
        }
        if (pan_frame_decode(ps->blob + off, sz, ps->planes, PAN_BUFFER_SIZE,
                             cur_p, coll_raw, sizeof coll_raw, &csz,
                             &consumed) ||
            csz < PAN_COLL_SIZE ||
            pan_collision_map_load(&ps->collision, coll_raw, PAN_COLL_SIZE))
            return -1;
        ps->index = next;
        save_keyframe(ps, next);
    }
    planes_to_rgba(ps->planes + (size_t)(ps->index & 3u) * PAN_PLANE_SIZE,
                   ps->palette4, ps->bg_rgba);
    return 0;
}

int pan_scroll_update(pan_scroll *ps, unsigned camera_px, unsigned bones_px,
                      unsigned start_px, unsigned finish_px) {
    unsigned idx;
    if (!ps) return -1;
    /* CPanScroll: scroll shorts track camera; PlayPanAnim advances with motion. */
    ps->scroll_x = (int)camera_px;
    ps->scroll_y = 0;
    (void)bones_px;
    idx = pan_scroll_index_for_x(ps, camera_px, start_px, finish_px);
    if (idx == ps->index) return 0;
    return pan_scroll_seek(ps, idx);
}

int pan_scroll_update_ratio(pan_scroll *ps, unsigned camera_px, unsigned bones_px,
                            unsigned px_num, unsigned px_den) {
    unsigned idx = 0u;
    if (!ps || ps->count == 0u) return -1;
    ps->scroll_x = (int)camera_px;
    ps->scroll_y = 0;
    (void)bones_px;
    if (px_den > 0u)
        idx = (unsigned)(((uint64_t)camera_px * (uint64_t)px_num) / (uint64_t)px_den);
    if (idx >= ps->count) idx = ps->count - 1u;
    if (idx == ps->index) return 0;
    return pan_scroll_seek(ps, idx);
}
