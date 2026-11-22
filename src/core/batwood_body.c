#include "sidescrl_phase.h"
#include "stxtr.h"
#include <math.h>
#include <stdlib.h>

/* BonesBuildHirearchy, tagBODYPIECE and the VDP1 distorted quads in BATWOOD2/3. */
typedef struct {
    double x, y, z;
} vec;
typedef struct {
    vec x, y, z, origin;
} transform;
typedef struct {
    vec corner[4];
    stxtr_record texture;
    double depth;
} face;
typedef struct {
    face faces[144];
    unsigned count;
} mesh;
typedef struct {
    transform joint[21];
    unsigned active;
} skeleton;

static uint32_t u32(const uint8_t *s) {
    return (uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 | s[3];
}
static unsigned u16(const uint8_t *s) { return s[0] * 256u + s[1]; }
static const uint8_t *record(const sidescrl_phase *p, uint32_t va, size_t bytes) {
    if (va < 0x232000u || bytes > p->code_size || va - 0x232000u > p->code_size - bytes) return NULL;
    return (const uint8_t *)p->code + (va - 0x232000u);
}
static vec xyz(const uint8_t *s) {
    vec v = {(int32_t)u32(s) / 65536., (int32_t)u32(s + 4) / 65536., (int32_t)u32(s + 8) / 65536.};
    return v;
}
static vec rotate(const transform *m, vec v) {
    vec b = {m->x.x * v.x + m->y.x * v.y + m->z.x * v.z, m->x.y * v.x + m->y.y * v.y + m->z.y * v.z,
             m->x.z * v.x + m->y.z * v.y + m->z.z * v.z};
    return b;
}
static vec point(const transform *m, vec v) {
    vec b = rotate(m, v);
    b.x += m->origin.x;
    b.y += m->origin.y;
    b.z += m->origin.z;
    return b;
}
static transform angles(unsigned x, unsigned y, unsigned z) {
    const double rad = 6.2831853071795864769 / 1024.;
    double sx = sin(x * rad), cx = cos(x * rad), sy = sin(y * rad), cy = cos(y * rad),
           sz = sin(z * rad), cz = cos(z * rad);
    /* Loader /02 at 0x204d6c builds Rz * Ry * Rx. */
    transform m = {{cy * cz, cy * sz, -sy},
                   {sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, sx * cy},
                   {cx * sy * cz + sx * sz, cx * sy * sz - sx * cz, cx * cy},
                   {0, 0, 0}};
    return m;
}
static transform child(const transform *parent, transform local, vec offset) {
    transform joint = {rotate(parent, local.x), rotate(parent, local.y), rotate(parent, local.z),
                       point(parent, offset)};
    return joint;
}
static uint32_t master(const sidescrl_phase *p) {
    const uint8_t *row = record(p, p->config_table, 4);
    const uint8_t *cfg = row ? record(p, u32(row), 128) : NULL;
    const uint8_t *links = cfg ? record(p, u32(cfg + 124), 16) : NULL;
    return links && u32(links + 12) >= 168 ? u32(links + 12) - 168 : 0;
}
static unsigned motion_frame(const sidescrl_phase *p, const uint8_t *cfg, const uint8_t **tracks) {
    unsigned slot = 1, field = 16;
    if (p->latch != BAT_FREE) {
        slot = 3;
        field = p->latch_type == 2 ? 20 : 36;
        if (p->latch == BAT_PULL) field += 4;
    } else if (!p->actor.on_ground) {
        slot = 5;
        field = 16;
    }
    const uint8_t *state = record(p, u32(cfg + 4 * slot), field + 4);
    const uint8_t *motion = state ? record(p, u32(state + field), 8) : NULL;
    if (!motion) return 0;
    unsigned last = u32(motion + 4);
    if (last > 255) return 0;
    *tracks = record(p, u32(motion), 22 * 4);
    if (p->latch != BAT_FREE) {
        unsigned frame = (p->frames - p->latch_frame) / 4;
        return frame < last ? frame : last;
    }
    if (p->actor.on_ground && !p->actor.vx) return 0;
    return (p->frames / 4) % (last + 1);
}
static void pose(const sidescrl_phase *p, skeleton *bones) {
    const uint8_t *cfg = record(p, p->body_config, 208), *tracks = NULL;
    if (!cfg) return;
    const uint32_t first = master(p);
    unsigned frame = motion_frame(p, cfg, &tracks);
    if (!first || !tracks) return;
    transform parent[21] = {0};
    parent[0] = angles(0,
                       p->latch != BAT_FREE && p->latch_type == 1 ? 0
                       : p->actor.facing_left                     ? 256
                                                                  : 768,
                       0);
    const uint8_t *root = record(p, u32(tracks), 12 * (frame + 1));
    if (!root) return;
    unsigned movement = u32(root + 12 * frame + 4);
    vec offset = {((int)((movement >> 20) & 1023) - 512) / 128.,
                  ((int)((movement >> 10) & 1023) - 512) / 128., ((int)(movement & 1023) - 512) / 128.};
    parent[0].origin = rotate(parent, offset);
    for (unsigned i = 0; i < 21; i++) {
        const uint8_t *links = record(p, u32(cfg + 124 + i * 4), 64);
        if (!links) continue;
        const uint8_t *track = record(p, u32(tracks + 4 + i * 4), 4 * (frame + 1));
        uint32_t turn = track ? u32(track + 4 * frame) : 0;
        transform r = angles(turn >> 20 & 1023, turn >> 10 & 1023, turn & 1023);
        vec zero = {0, 0, 0};
        bones->joint[i] = child(parent + i, r, zero);
        bones->active |= 1u << i;
        for (unsigned j = 0; j < 4; j++) {
            const uint8_t *link = links + 16 * j;
            uint32_t node = u32(link + 12);
            if (node < first || (node - first) % 168) continue;
            unsigned k = (node - first) / 168;
            if (k <= i || k >= 21) continue;
            parent[k] = bones->joint[i];
            parent[k].origin = point(bones->joint + i, xyz(link));
        }
    }
}

/* BonesObjHitChk compares squared distances after a signed eight-bit shift.
 * The object tolerance is added to these squared distances in the overlay. */
int batwood_exit_contact(const sidescrl_phase *p) {
    if (!p || !p->exit_config || !p->projection_px || p->body_config != p->exit_config) return 0;
    int scale = (int)p->projection_px;
    int x = p->actor.x + SIDESCRL_ACTOR_WIDTH / 2 - (int)(p->finish_x / 2341u);
    int y = p->actor.y + SIDESCRL_STAND_HEIGHT - p->floor_delta - p->finish_y;
    if (abs(x) > 2 * scale || abs(y) > 2 * scale) return 0;
    double xx = (p->actor.x + SIDESCRL_ACTOR_WIDTH / 2) * 256. / scale + p->exit_world_x / 256.;
    double yy =
        (p->actor.y + SIDESCRL_STAND_HEIGHT - p->floor_delta) * 256. / scale + p->exit_world_y / 256.;
    if (xx * xx + yy * yy >= 220000 + 0x8000) return 0;
    if (xx * xx + yy * yy < 3000) return 1;
    skeleton bones = {0};
    pose(p, &bones);
    static const unsigned joints[] = {5, 2, 12, 19, 13, 20, 9, 16};
    for (unsigned i = 0; i < sizeof joints / sizeof *joints; i++) {
        unsigned j = joints[i];
        if (!(bones.active & (1u << j))) continue;
        vec v = bones.joint[j].origin;
        double dx = xx - v.x * 256, dy = yy - v.y * 256, dz = v.z * 256;
        if (dx * dx + dy * dy + dz * dz < 0x8000 + ((j == 12 || j == 19) ? 3000 : 2000)) return 1;
    }
    return 0;
}

static void add_shape(const sidescrl_phase *p, mesh *m, uint32_t address, const transform *joint,
                      int cx, int cy) {
    const uint8_t *shape = record(p, address, 16);
    if (!shape || u32(shape) != 0x2067ccu) return;
    uint32_t vertices = u32(shape + 8), polygons = u32(shape + 12);
    for (unsigned i = 0; i < 32 && m->count < sizeof m->faces / sizeof m->faces[0]; i++) {
        const uint8_t *q = record(p, polygons, 24);
        if (!q || u32(q) == UINT32_MAX) break;
        polygons += 24;
        uint32_t vram = u32(q + 8);
        if (vram < 0x25c08000u) continue;
        size_t offset = vram - 0x25c08000u;
        face *f = &m->faces[m->count];
        if (stxtr_next(p->textures, p->textures_size, &offset, &f->texture) != 1 ||
            f->texture.size / 2 < (size_t)f->texture.width * f->texture.height)
            continue;
        f->depth = 0;
        for (unsigned j = 0; j < 4; j++) {
            unsigned index = u16(q + 2 * j);
            if ((index & 3u) || vertices > UINT32_MAX - 3 * index) return;
            const uint8_t *vert = record(p, vertices + 3 * index, 12);
            if (!vert) return;
            vec v = point(joint, xyz(vert));
            f->corner[j].x = cx - v.x * p->projection_px;
            f->corner[j].y = cy - v.y * p->projection_px;
            f->depth += v.z;
        }
        m->count++;
    }
}
static int back_to_front(const void *aa, const void *bb) {
    double a = ((const face *)aa)->depth, b = ((const face *)bb)->depth;
    return (a > b) - (a < b);
}
static void triangle(const sidescrl_phase *p, const face *f, unsigned b, unsigned c) {
    vec a = f->corner[0], v = f->corner[b], w = f->corner[c];
    double vx = v.x - a.x, vy = v.y - a.y, wx = w.x - a.x, wy = w.y - a.y, det = vx * wy - vy * wx;
    if (fabs(det) < 0.001) return;
    double xmin = fmax(0, floor(fmin(a.x, fmin(v.x, w.x)))),
           xmax = fmin(319, ceil(fmax(a.x, fmax(v.x, w.x))));
    double ymin = fmax(0, floor(fmin(a.y, fmin(v.y, w.y)))),
           ymax = fmin(223, ceil(fmax(a.y, fmax(v.y, w.y))));
    if (xmin > xmax || ymin > ymax) return;
    for (int y = (int)ymin; y <= (int)ymax; y++)
        for (int x = (int)xmin; x <= (int)xmax; x++) {
            double dx = x + .5 - a.x, dy = y + .5 - a.y, s = (dx * wy - dy * wx) / det,
                   t = (vx * dy - vy * dx) / det;
            if (s < 0 || t < 0 || s + t > 1) continue;
            unsigned tx = (unsigned)((b == 1 ? s + t : s) * (f->texture.width - 1) + .5);
            unsigned ty = (unsigned)((b == 1 ? t : s + t) * (f->texture.height - 1) + .5);
            unsigned word = u16(f->texture.data + 2 * ((size_t)ty * f->texture.width + tx));
            if (!word) continue;
            const uint8_t *color = p->palette + 4 * (word & 255u);
            uint8_t *d = p->frame_rgba + 4 * (320 * y + x);
            d[0] = color[1];
            d[1] = color[2];
            d[2] = color[3];
            d[3] = 255;
        }
}
static void paint(const sidescrl_phase *p, mesh *m) {
    qsort(m->faces, m->count, sizeof *m->faces, back_to_front);
    for (unsigned i = 0; i < m->count; i++) {
        triangle(p, m->faces + i, 1, 2);
        triangle(p, m->faces + i, 2, 3);
    }
}
void batwood_draw(const sidescrl_phase *p, int screen_x, int feet_y, int camera_x) {
    mesh faces;
    for (unsigned i = 0; i < p->body_piece_count; i++) {
        int x = p->body_pieces[i].x / 256 - camera_x, y = p->body_pieces[i].y / 256 - p->map.camera_y;
        if ((p->collected & (1u << i)) || x < -64 || x > 384 || y < -64 || y > 288) continue;
        uint32_t va = p->body_pieces[i].attracted;
        if (!va) va = batwood_piece(p, p->body_pieces[i].mask);
        transform part = angles(0, (p->frames * 16u) & 1023, 0);
        faces.count = 0;
        for (unsigned j = 0; j < 4; j++) {
            const uint8_t *piece = record(p, va, 28);
            if (!piece || !u32(piece)) break;
            vec displacement = xyz(piece + 4);
            transform r =
                angles(u16(piece + 16) & 1023, u16(piece + 18) & 1023, u16(piece + 20) & 1023);
            part = child(&part, r, displacement);
            add_shape(p, &faces, u32(piece), &part, x, y);
            va += 28;
        }
        paint(p, &faces);
    }
    skeleton bones = {0};
    pose(p, &bones);
    uint32_t first = master(p);
    int origin_y = feet_y - p->floor_delta;
    if (p->latch == BAT_HANG) {
        const uint8_t *cfg = record(p, p->body_config, 16);
        const uint8_t *state = cfg ? record(p, u32(cfg + 12), 56) : NULL;
        if (state && u32(state + 52) >= first) {
            unsigned hand = (u32(state + 52) - first) / 168;
            if (hand < 21 && (bones.active & (1u << hand))) {
                vec grip = bones.joint[hand].origin;
                screen_x += (int)lround(grip.x * p->projection_px);
                origin_y = p->latch_y - p->map.camera_y + (int)lround(grip.y * p->projection_px);
            }
        }
    }
    faces.count = 0;
    for (unsigned j = 1; j < 21; j++) {
        if (!(bones.active & (1u << j))) continue;
        const uint8_t *node = record(p, first + 168 * j, 4);
        if (node) add_shape(p, &faces, u32(node), bones.joint + j, screen_x, origin_y);
    }
    paint(p, &faces);
}
