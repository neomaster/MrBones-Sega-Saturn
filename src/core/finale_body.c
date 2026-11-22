#include "finale_body.h"
#include "pan_palette.h"
#include "stxtr.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* US disc 2, /27FINALE/03CODE linked at 00232000. All motion keys,
 * attachments, textured faces and colors are read from the mounted disc. */
enum { JOINTS = FINALE_BODY_JOINTS, INIT = 0x2bc35c, MASTER = 0x2bb064, DIRECTIONS = 0x2b00b4 };
enum { BODY_FACES = 2 * JOINTS, LAYERS_MAX = BODY_FACES + FINALE_SPRITES_MAX };
typedef struct {
    double v[3][3], xyz[3];
} transform;
typedef struct {
    uint32_t tracks;
    unsigned frame, weight;
    int delta;
} motion;
typedef struct {
    /* Minimum transition speed is 17 on this disc: during one fade the
     * controller can retain at most 255/17 older motion players. */
    motion players[16];
    unsigned count, ticks;
} displayed_pose;
typedef struct {
    int x, y;
} pixel;
typedef struct {
    uint32_t shape;
    unsigned joint;
    double depth;
} layer;
typedef struct {
    stxtr_record tex;
    int x, y;
} health_piece;
typedef enum { MOTION_SUBMITTED, MOTION_FROM_BONES, MOTION_FROM_BOLT } motion_origin;

struct finale_body {
    const uint8_t *code, *texture;
    size_t code_size, texture_size;
    uint32_t config, guitar, sequence;
    uint32_t init, master, motion_next, motion_loop;
    unsigned joint_count, ice_mode, ice_track_mode, ice_motion_tick, ice_motion_running;
    const stage_body_pose *world_pose;
    double camera_depth, root_xyz[3];
    int heading;
    unsigned facing, hold;
    motion_origin pending_motion;
    uint32_t queued_sequence;
    displayed_pose live, submitted, prepared, visible;
    unsigned charge_cycle, prepared_pal, visible_pal;
    transform joints[JOINTS];
    layer faces[BODY_FACES];
    finale_sprite effects[FINALE_SPRITES_MAX];
    uint8_t draw_order[LAYERS_MAX], next_order[LAYERS_MAX];
    unsigned layer_count;
    uint32_t wiggle_mask;
    uint16_t wiggle_phase[JOINTS][2];
    int16_t sine[1024];
    uint32_t normal[256], red[16], guitar_pal[5][8];
    uint32_t display_colors[256];
    health_piece health[14];
    uint32_t face_colors[256], ring_colors[256];
    uint8_t nbg2_mask[320u * 224u];
    uint8_t backdrop[320u * 224u * 4u];
};

uint8_t *finale_body_front_mask(finale_body *body) {
    return body ? body->nbg2_mask : NULL;
}
uint8_t *ice_body_surface(finale_body *body) {
    return body ? body->backdrop : NULL;
}

static void prepare_layers(finale_body *b, const finale_sprite sprites[FINALE_SPRITES_MAX]);

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static unsigned be16(const uint8_t *p) { return p[0] * 256u + p[1]; }
static const uint8_t *data(const finale_body *b, uint32_t a, size_t size) {
    if (a < 0x232000u || size > b->code_size || a - 0x232000u > b->code_size - size)
        return NULL;
    return b->code + (a - 0x232000u);
}
static uint32_t word(const finale_body *b, uint32_t a) {
    const uint8_t *p = data(b, a, 4);
    return p ? be32(p) : 0;
}
static void select_sequence(finale_body *b, uint32_t address, motion_origin origin) {
    const uint8_t *row = data(b, address, 16);
    if (!row)
        return;
    uint32_t tracks = word(b, be32(row));
    if (!data(b, tracks, 4u * (JOINTS + 1u)))
        return;
    b->sequence = address;
    b->hold = be32(row + 8);
    b->facing = be32(row + 12);
    unsigned rate = be32(row + 4);
    if (rate < 17 || rate > 255)
        return;
    /* BonesCreateMotion discards players below a quarter of the aggregate
     * weight retained by the preceding call to BonesBuild. */
    unsigned subtotal = 0;
    const displayed_pose *previous = &b->submitted;
    unsigned n = previous->count > 1 ? previous->count - 1 : previous->count;
    for (unsigned i = 0; i < n; i++)
        subtotal += previous->players[i].weight;
    /* At 295380, a second creation before BonesBuild consumes BBS message
     * 01100001 and unlinks the player created by the first request. */
    if (b->pending_motion != MOTION_SUBMITTED && b->live.count)
        --b->live.count;
    unsigned kept = 0;
    for (unsigned i = 0; i < b->live.count; i++) {
        motion m = b->live.players[i];
        if (m.weight >= subtotal / 4) {
            m.delta = -(int)rate;
            b->live.players[kept++] = m;
        }
    }
    if (kept >= 16)
        return;
    b->live.players[kept] = (motion){tracks, 0, rate, (int)rate};
    b->live.count = kept + 1;
    b->pending_motion = origin;
}
static void apply_queued_sequence(finale_body *b) {
    if (b->queued_sequence) {
        select_sequence(b, b->queued_sequence, MOTION_FROM_BONES);
        b->queued_sequence = 0;
    }
}

void finale_body_command(finale_body *b, unsigned command) {
    if (!b || command > 6 || b->facing > 4)
        return;
    if (command <= 4u) {
        b->queued_sequence = word(b, DIRECTIONS + 32u * b->facing + 4u * command);
    } else {
        /* In a single CheckJoy sample, ChangeDirection (28d270) precedes
         * the shot (28d28c). The second lookup uses the new facing. */
        if (command == 6u) apply_queued_sequence(b);
        select_sequence(b, word(b, DIRECTIONS + 32u * b->facing + 4u * command),
                        command == 5u ? MOTION_FROM_BOLT : MOTION_FROM_BONES);
    }
}
static unsigned next_frame(const finale_body *b, motion m) {
    uint32_t fn = word(b, word(b, m.tracks) + 12 * m.frame + 8);
    if (fn == b->motion_loop)
        return 0;                                   /* Mloop */
    return fn == b->motion_next ? m.frame + 1 : m.frame; /* Mnext / Mhold */
}
void finale_body_tick(finale_body *b, unsigned charged, const finale_sprite sprites[FINALE_SPRITES_MAX]) {
    if (!b)
        return;
    ++b->live.ticks;
    /* In the US victory trace, ChangeDirection(4) first runs at logical
     * counter 0x24f. Sample new directions on the even pose-clock ticks
     * observed in CheckJoy (0x254, 25e, 266, 270, 278, 29e), before BonesBuild.
     * The duel retains the physical pad queue and collision clock of its own. */
    if (b->live.ticks == 63u)
        select_sequence(b, word(b, DIRECTIONS + 32u * 4u + 4u * 4u), MOTION_FROM_BONES);
    if (!(b->live.ticks & 1u)) apply_queued_sequence(b);
    /* ObjYeild advances the active motion after CheckJoy. A pending
     * replacement supersedes the outgoing motion's timed continuation. */
    /* BoltHit calls ChangeDirection(5) at 28d3ce after CFBones has already
     * decremented its timer at 28d11e that pass. BonesBuild sees the block
     * immediately, but its first countdown is the next actor pass. */
    if (b->pending_motion != MOTION_FROM_BOLT && !b->queued_sequence && b->hold && !--b->hold)
        select_sequence(b, b->sequence + 16u, MOTION_FROM_BONES);
    /* Hierarchy submission precedes MCplayer's weight and key advance.
     * The selected VDP1 framebuffer displays this list two retraces later. */
    b->visible = b->prepared;
    b->prepared = b->submitted;
    b->submitted = b->live;
    b->pending_motion = MOTION_SUBMITTED;
    prepare_layers(b, sprites);
    b->visible_pal = b->prepared_pal;
    b->prepared_pal = charged ? 1u + (b->charge_cycle >> 1) : 0u;
    if (charged)
        b->charge_cycle = (b->charge_cycle + 1u) & 7u;
    unsigned kept = 0;
    for (unsigned i = 0; i < b->live.count; i++) {
        motion m = b->live.players[i];
        int next = (int)m.weight + m.delta;
        if (next <= 0)
            continue;
        m.weight = next > 255 ? 255u : (unsigned)next;
        /* The stage-start player is at key 15 after absolute clock 0x24f
         * (Ymir, arena tick 62); the startup pass retains key zero. */
        if (b->live.ticks > 3u && (b->live.ticks & 3u) == 3u)
            m.frame = next_frame(b, m);
        b->live.players[kept++] = m;
    }
    b->live.count = kept;
}
static int palette(const finale_body *b, uint32_t a, uint32_t *pal, unsigned count) {
    const uint8_t *p = data(b, a, 8);
    size_t consumed = 0;
    return p && pan_palette_apply(p, b->code_size - (a - 0x232000u), pal, count, &consumed) == 0;
}
static void register_body_faces(finale_body *b) {
    /* SetShape inserts at the list head. Bonelet constructors register
     * both render objects in joint order; skip the loader's NullShape. */
    for (int i = (int)b->joint_count - 1; i > 0; --i) {
        for (int slot = 1; slot >= 0; --slot) {
            uint32_t entry = b->master + 168u * (unsigned)i + 8u * (unsigned)slot;
            uint32_t shape = i == 22 && !slot ? b->guitar : word(b, entry);
            if (!data(b, shape, 16))
                continue;
            unsigned id = 2u * (unsigned)i + (unsigned)slot;
            b->faces[id] = (layer){shape, (unsigned)i, (int32_t)word(b, entry + 4) / 16777216.};
            b->next_order[b->layer_count++] = (uint8_t)id;
        }
    }
}

static int body_health(finale_body *b, uint32_t table) {
    /* CSkelBar constructor (29593A) positions these normal sprites relative
     * to the VDP1 origin. HealthBarAnim has one descriptor per active piece. */
    for (unsigned i = 0; i < 14; i++) {
        uint32_t object = word(b, table + 12u * i);
        const uint8_t *leaf = data(b, word(b, object + 12u), 24);
        if (!leaf || be32(leaf + 8) < 0x25c08000u || be32(leaf + 12) != 0)
            return 0;
        size_t offset = be32(leaf + 8) - 0x25c08000u;
        health_piece *piece = &b->health[i];
        piece->x = 159 + 109 - (int16_t)be16(leaf);
        piece->y = 111 - 92 - (int16_t)be16(leaf + 2);
        if (stxtr_next(b->texture, b->texture_size, &offset, &piece->tex) != 1 ||
            piece->tex.size != (size_t)piece->tex.width * piece->tex.height || piece->x < 0 ||
            piece->x + piece->tex.width > 320 || piece->y < 0 || piece->y + piece->tex.height > 224)
            return 0;
    }
    return 1;
}

finale_body *finale_body_load(const void *code, size_t code_size, const void *textures,
                              size_t texture_size, const uint8_t random_table[256]) {
    if (!code || !textures || !random_table)
        return NULL;
    finale_body *b = calloc(1, sizeof *b);
    if (!b)
        return NULL;
    b->code = code;
    b->code_size = code_size;
    b->texture = textures;
    b->texture_size = texture_size;
    /* Matches all 1,024 loader lookup entries at 0x21461c. */
    for (unsigned i = 0; i < 1024; i++)
        b->sine[i] = (int16_t)(256. * sin(6.2831853071795864769 * i / 1024.));
    b->init = INIT;
    b->master = MASTER;
    b->joint_count = JOINTS;
    b->camera_depth = 6.;
    b->motion_next = 0x295608u;
    b->motion_loop = 0x29562cu;
    b->config = word(b, b->init);
    /* WiggleInfo selects the mask for this stage's configuration. The
     * CBonelet constructors each draw two initial phases from loader Random. */
    for (uint32_t row = 0x2bc2ccu; word(b, row); row += 8u) {
        if (word(b, row) == b->config) {
            b->wiggle_mask = word(b, row + 4u);
            break;
        }
    }
    uint32_t seed = 0;
    for (unsigned i = 0; i < JOINTS; ++i) {
        for (unsigned axis = 0; axis < 2; ++axis) {
            seed += 0x07050301u;
            unsigned angle = random_table[seed >> 24] | ((unsigned)random_table[(seed >> 16) & 255u] << 8);
            b->wiggle_phase[i][axis] = (uint16_t)(angle & 1023u);
        }
    }
    b->guitar = word(b, 0x281ac8);
    b->facing = 4;
    b->live.ticks = 1; /* Global frame counter at the arena scheduler origin. */
    select_sequence(b, word(b, DIRECTIONS + 32u * 4u + 4u * 4u), MOTION_FROM_BONES);
    b->visible = b->prepared = b->submitted = b->live;
    uint32_t pal[1024] = {0};
    if (!data(b, b->config, 124 + 4 * JOINTS) || !data(b, MASTER, 168 * JOINTS) || !b->live.count ||
        !data(b, b->guitar, 16) || !palette(b, 0x289440, pal, 1024))
        goto fail;
    memcpy(b->normal, pal, sizeof b->normal);
    memcpy(b->ring_colors, pal + 512, sizeof b->ring_colors);
    memcpy(b->face_colors, pal + 256, sizeof b->face_colors);
    if (!palette(b, 0x28abd8, pal, 1024))
        goto fail;
    memcpy(b->red, pal, sizeof b->red);
    for (unsigned i = 0; i < 5; ++i) {
        if (!palette(b, i ? word(b, 0x2afec4 + 4 * (i - 1)) : 0x28a448, pal, 1024))
            goto fail;
        memcpy(b->guitar_pal[i], pal + 128, sizeof b->guitar_pal[i]);
    }
    if (!body_health(b,0x281dacu)) goto fail;
    register_body_faces(b);
    prepare_layers(b, NULL);
    return b;
fail:
    free(b);
    return NULL;
}
static int difference(int a, int b, int wrap) { return wrap ? ((b - a + 511) & 1023) - 511 : b - a; }
static void sample(const finale_body *b, motion m, int index, unsigned fraction, int out[3]) {
    unsigned stride = index ? 4u : 12u, offset = index ? 0u : 4u;
    uint32_t ptr = word(b, m.tracks + (uint32_t)index * 4u) + offset;
    uint32_t a = word(b, ptr + stride * m.frame), z = word(b, ptr + stride * next_frame(b, m));
    for (int k = 0; k < 3; k++) {
        unsigned shift = (unsigned)(2 - k) * 10u;
        int x = (a >> shift) & 1023, y = (z >> shift) & 1023;
        int unit = index ? 1 : 512; /* ANoff retains the full 16.16 displacement. */
        out[k] = x * unit + (int)floor(difference(x, y, index != 0) * unit * (int)fraction / 4.);
    }
}
static void pose_value(const finale_body *b, int i, int out[3]) {
    const displayed_pose *pose = &b->visible;
    unsigned sub = pose->count == 1u ? pose->ticks & 3u : 0u;
    sample(b, pose->players[0], i, sub, out);
    unsigned total = pose->players[0].weight;
    for (unsigned j = 1; j < pose->count; j++) {
        motion m = pose->players[j];
        int add[3];
        sample(b, m, i, 0, add);
        total += m.weight;
        /* BonesBuildHirearchyInterpolate (293B7E) takes the reciprocal
         * first and the signed high half of the angular product. ANoff
         * instead divides the signed linear displacement directly. */
        int fraction = (int)m.weight * (65536 / (int)total);
        for (int k = 0; k < 3; k++) {
            int diff = difference(out[k], add[k], i != 0);
            if (i)
                out[k] = (out[k] + (int)floor(diff * fraction / 65536.)) & 1023;
            else
                out[k] += diff * (int)m.weight / (int)total;
        }
    }
}
static void rotate_point(const transform *t, const double local[3], double world[3]) {
    for (int i = 0; i < 3; i++) {
        world[i] = t->xyz[i];
        for (int j = 0; j < 3; j++)
            world[i] += t->v[i][j] * local[j];
        world[i] = floor(world[i] * 65536.) / 65536.;
    }
}
static transform rotation(const finale_body *b, const int a[3]) {
    double sx = b->sine[a[0] & 1023] / 256., cx = b->sine[(a[0] + 256) & 1023] / 256.,
           sy = b->sine[a[1] & 1023] / 256., cy = b->sine[(a[1] + 256) & 1023] / 256.,
           sz = b->sine[a[2] & 1023] / 256., cz = b->sine[(a[2] + 256) & 1023] / 256.;
    transform t = {{{cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz},
                    {cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz},
                    {-sy, sx * cy, cx * cy}},
                   {0, 0, 0}};
    for (unsigned i = 0; i < 3; i++)
        for (unsigned j = 0; j < 3; j++)
            t.v[i][j] = floor(t.v[i][j] * 256.) / 256.;
    return t;
}
static void skeleton(const finale_body *b, transform joint[JOINTS]) {
    /* BonesStageInit supplies a world rotation, Rx * Ry * Rz. */
    transform parent[JOINTS] = {0}, base = {0};
    const uint8_t *angles = data(b, b->init + 20, 6);
    if (!angles)
        return;
    int inverse[3] = {-(int)be16(angles), -(int)be16(angles + 2), -(int)be16(angles + 4)};
    inverse[1] -= b->heading;
    if (b->world_pose)
        for (unsigned i=0;i<3;++i) inverse[i]=-b->world_pose->rotation[i];
    transform backwards = rotation(b, inverse);
    for (unsigned i = 0; i < 3; i++)
        for (unsigned j = 0; j < 3; j++)
            base.v[i][j] = backwards.v[j][i];
    if (b->world_pose && b->world_pose->scale!=256) {
        for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j)
            base.v[i][j]=floor(base.v[i][j]*b->world_pose->scale)/256.;
    }
    transform movement_base = base;
    if (b->ice_mode || b->world_pose) {
        /* CBnsIceFlr::Brain binds CBnsExtRot and supplies these 8.8 turns. */
        int turns[3] = {b->ice_mode == 2 ? -32 : -64, b->ice_mode == 2 ? 0 : -16, 0};
        if (b->world_pose)
            for(unsigned i=0;i<3;++i) turns[i]=-b->world_pose->post_rotation[i];
        transform external = rotation(b, turns), original = base;
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
            base.v[i][j] = 0;
            for (int k = 0; k < 3; ++k) base.v[i][j] += external.v[k][i] * original.v[k][j];
        }
    }
    int a[3];
    pose_value(b, 0, a);
    double shift[3];
    for (unsigned i = 0; i < 3; i++) {
        base.xyz[i] = b->ice_mode ? b->root_xyz[i] : (int32_t)word(b, b->init + 8 + 4 * i) / 65536.;
        if (b->world_pose) base.xyz[i]=b->world_pose->origin[i]/65536.;
        shift[i] = (a[i] - 512 * 512) / 65536.;
    }
    parent[0] = base;
    /* BonesBuild rotates ANoff before it applies the external rotation to the joint hierarchy. */
    memcpy(movement_base.xyz, base.xyz, sizeof base.xyz);
    rotate_point(&movement_base, shift, parent[0].xyz);
    for (int i = 0; (unsigned)i < b->joint_count; i++) {
        pose_value(b, i + 1, a);
        /* CBonelet::Brain (29F7D8) updates the offsets after submission.
         * The first six pose clocks cover startup and the initial build.
         * Its amplitude clamps to 0x8000 for FINALE's whole health range
         * (0..0x20000), giving floor(sin8 / 8) on both axes. */
        if (((b->wiggle_mask >> i) & 1u) && b->visible.ticks > 6u) {
            unsigned elapsed = b->visible.ticks - 6u;
            unsigned x = (b->wiggle_phase[i][0] + elapsed * 64u) & 1023u;
            unsigned y = (b->wiggle_phase[i][1] + elapsed * 38u) & 1023u;
            a[0] += (int)floor(b->sine[x] / 8.);
            a[1] += (int)floor(b->sine[y] / 8.);
        }
        transform r = rotation(b, a);
        for (int x = 0; x < 3; x++) {
            joint[i].xyz[x] = parent[i].xyz[x];
            for (int y = 0; y < 3; y++) {
                for (int k = 0; k < 3; k++)
                    joint[i].v[x][y] += parent[i].v[x][k] * r.v[k][y];
                joint[i].v[x][y] = floor(joint[i].v[x][y] * 256.) / 256.;
            }
        }
        uint32_t links = word(b, b->config + 124u + 4u * (unsigned)i);
        for (unsigned j = 0; j < 4; j++) {
            const uint8_t *link = data(b, links + 16u * j, 16);
            if (!link)
                break;
            uint32_t child = be32(link + 12);
            if (child < b->master || (child - b->master) % 168)
                break;
            unsigned n = (child - b->master) / 168;
            if (n <= (unsigned)i || n >= b->joint_count)
                continue;
            double local[3];
            for (unsigned k = 0; k < 3; k++)
                local[k] = (int32_t)be32(link + 4 * k) / 65536.;
            parent[n] = joint[i];
            rotate_point(joint + i, local, parent[n].xyz);
        }
    }
}
/* VDP1 traverses corresponding points on the AD and BC edges and paints
 * each scanline from its A-side endpoint to the B-side endpoint, inclusive.
 * Keeping zero-length lines matters for the hands, strings and spines. */
static int span(pixel a, pixel b) {
    int x = abs(a.x - b.x), y = abs(a.y - b.y);
    return x > y ? x : y;
}
/* Each edge has its own major-axis length. The shorter edge repeats
 * points so it reaches its last texel together with the longer edge. */
static int scaled_step(int step, int extent, int length, int ties_up) {
    return length ? (2 * step * extent + length - 1 + ties_up) / (2 * length) : 0;
}
static pixel edge_point(pixel a, pixel b, int row, int rows) {
    int dx = b.x - a.x, dy = b.y - a.y, extent = span(a, b);
    int joint_step = scaled_step(row, extent, rows, (abs(dx) >= abs(dy) ? dx : dy) < 0);
    int x = scaled_step(joint_step, abs(dx), extent, dy < 0);
    int y = scaled_step(joint_step, abs(dy), extent, dx < 0);
    pixel v = {a.x + (dx < 0 ? -x : x), a.y + (dy < 0 ? -y : y)};
    return v;
}
static unsigned texel(unsigned i, unsigned length, unsigned size, unsigned flip) {
    unsigned k;
    if (size > length + 1u)
        k = ((2 * i + 1) * size - flip) / (2 * (length + 1u));
    else
        k = length ? (2 * i * (size - 1u) + length - 1u + flip) / (2 * length) : 0;
    return flip ? size - 1u - k : k;
}
static void paint_texel(const uint8_t *backdrop, const uint8_t *front_mask, uint8_t *rgba,
                        pixel v, uint32_t c, unsigned back) {
    if ((unsigned)v.x >= 320u || (unsigned)v.y >= 224u)
        return;
    size_t o = (size_t)(320 * v.y + v.x) * 4u;
    /* A covered VDP1 word still replaces the previous sprite word. VDP2
     * consequently displays its own opaque dot at this coordinate. */
    if (front_mask && front_mask[o / 4u]) {
        memcpy(rgba + o, backdrop + o, 4);
        return;
    }
    for (unsigned ch = 0; ch < 3; ch++, c >>= 8)
        rgba[o + ch] = (uint8_t)(((c & 255u) * (32u - back) + backdrop[o + ch] * back) >> 5);
    rgba[o + 3] = 255;
}
static void quad(const uint8_t *backdrop, uint8_t *rgba, const pixel p[4], const stxtr_record *t,
                 unsigned wide, unsigned flip, const uint32_t colors[256], const uint8_t *front_mask, unsigned under_ice) {
    int left = span(p[0], p[3]), right = span(p[1], p[2]), height = left > right ? left : right;
    for (int y = 0; y <= height; y++) {
        pixel a = edge_point(p[0], p[3], y, height), b = edge_point(p[1], p[2], y, height);
        unsigned ty = texel((unsigned)y, (unsigned)height, t->height, flip >> 1);
        int dx = b.x - a.x, dy = b.y - a.y, n = span(a, b);
        pixel prev = a;
        for (int x = 0; x <= n; x++) {
            int vx = scaled_step(x, abs(dx), n, 0), vy = scaled_step(x, abs(dy), n, 0);
            pixel v = {a.x + (dx < 0 ? -vx : vx), a.y + (dy < 0 ? -vy : vy)};
            unsigned tx = texel((unsigned)x, (unsigned)n, t->width, flip & 1u);
            size_t k = (size_t)ty * t->width + tx;
            unsigned index = wide ? be16(t->data + 2 * k) : t->data[k];
            if (index) {
                /* SPCTL=2000, CCCTL=0040, CCRSA..D = 0,4,...,28.
                 * All body faces cover the same VDP2 backdrop. */
                unsigned back = under_ice ? 0u : 1u + 4u * (((wide ? index : 0x1000u) >> 11) & 7u);
                /* SPCTL type 0. PRISA=0106, PRISB=0704. Selectors 1 and 2
                 * rank below NBG2 (priority 5). Banked mesh/ring words use 0. */
                unsigned selector = wide ? index >> 14 : 0;
                const uint8_t *cover = selector == 1u || selector == 2u ? front_mask : NULL;
                uint32_t c = colors[index & 255u];
                paint_texel(backdrop, cover, rgba, v, c, back);
                if (prev.x != v.x && prev.y != v.y) {
                    int same_sign = (dx < 0) == (dy < 0);
                    pixel adjacent = v;
                    /* For either major axis VDP1 keeps the previous Y
                     * on diagonals of equal sign, and the previous X otherwise. */
                    if (same_sign)
                        adjacent.y = prev.y;
                    else
                        adjacent.x = prev.x;
                    paint_texel(backdrop, cover, rgba, adjacent, c, back);
                }
            }
            prev = v;
        }
    }
}
void finale_body_draw_ring(const finale_body *body, const uint8_t *backdrop, uint8_t *rgba,
                           const int16_t xy[8], const stxtr_record *texture) {
    pixel p[4];
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (pixel){159 + xy[2*i], 111 + xy[2*i + 1]};
    quad(backdrop, rgba, p, texture, 0, 0, body->ring_colors, NULL, 0);
}

static void shape_draw(const finale_body *b, uint8_t *rgba, const layer *l, const transform *t,
                       const uint32_t colors[256]) {
    const uint8_t *shape = data(b, l->shape, 16);
    if (!shape || (be32(shape) != 0x20673cu && be32(shape) != 0x2067ccu))
        return; /* Rmesh / Rastx */
    uint32_t vs = be32(shape + 8), qs = be32(shape + 12);
    /* _Rastx chooses exactly one of four perpendicular textured strips.
     * Its visibility metric uses rows 6 and 8 of the fixed-point matrix. */
    int strip = be32(shape) == 0x2067ccu;
    if (strip) {
        int x = (int)lround(t->v[2][0] * 256.), z = (int)lround(t->v[2][2] * 256.);
        int distance[4] = {abs(256 * x), abs(181 * (z - x)), abs(256 * z), abs(181 * (x + z))};
        unsigned best = 0;
        for (unsigned j = 1; j < 4; j++)
            if (distance[j] < distance[best])
                best = j;
        qs += 24 * best;
    }
    for (unsigned i = 0; i < (strip ? 1u : 128u); i++) {
        const uint8_t *q = data(b, qs + 24 * i, 24);
        if (!q || be16(q) == 0xffff)
            break;
        uint32_t cg = be32(q + 8), ctrl = be32(q + 12);
        if (cg < 0x25c08000u || (ctrl & 15u) != 2)
            continue;
        size_t offset = cg - 0x25c08000u;
        unsigned wide = (be32(q + 16) >> 16) == 0x04a8u;
        stxtr_record tex;
        if (stxtr_next(b->texture, b->texture_size, &offset, &tex) != 1 ||
            tex.size < (size_t)tex.width * tex.height * (1u + wide))
            continue;
        pixel pix[4];
        for (unsigned j = 0; j < 4; j++) {
            const uint8_t *v = data(b, vs + 3u * be16(q + 2 * j), 12);
            if (!v)
                return;
            double local[3], world[3];
            for (unsigned k = 0; k < 3; k++)
                local[k] = (int32_t)be32(v + 4 * k) / 65536.;
            rotate_point(t, local, world);
            int dz = (int)floor((world[2] + b->camera_depth) * 256.);
            if (dz < 256 || dz > 32767)
                return;
            int focal = -65536 / dz;
            pix[j].x = 159 + (int)floor(world[0] * focal);
            pix[j].y = 111 + (int)floor(world[1] * focal);
            if (abs(pix[j].x) > 1024 || abs(pix[j].y) > 1024)
                return;
        }
        /* PlotPolyMesh subtracts packed (x << 16) | y; the low-half
         * borrow is significant for silhouette quads narrower than a pixel. */
        int ab = pix[1].x - pix[0].x - ((uint16_t)(pix[1].y - 111) < (uint16_t)(pix[0].y - 111));
        int ac = pix[2].x - pix[0].x - ((uint16_t)(pix[2].y - 111) < (uint16_t)(pix[0].y - 111));
        int area = ab * (pix[2].y - pix[0].y) - ac * (pix[1].y - pix[0].y);
        if (strip || area >= 0)
            quad(b->backdrop, rgba, pix, &tex, wide, (ctrl >> 4) & 3u, colors, b->nbg2_mask, b->ice_mode == 2u);
    }
}
static void prepare_layers(finale_body *b, const finale_sprite sprites[FINALE_SPRITES_MAX]) {
    memset(b->joints, 0, sizeof b->joints);
    skeleton(b, b->joints);
    unsigned count = 0;
    for (unsigned i = 0; i < b->layer_count; ++i) {
        unsigned id = b->next_order[i];
        if (id < BODY_FACES || (sprites && sprites[id - BODY_FACES].pixels))
            b->next_order[count++] = (uint8_t)id;
    }
    for (unsigned i = 0; i < FINALE_SPRITES_MAX; ++i) {
        if (sprites && sprites[i].pixels && !b->effects[i].pixels) {
            memmove(b->next_order + 1, b->next_order, count);
            b->next_order[0] = (uint8_t)(BODY_FACES + i);
            ++count;
        }
        b->effects[i] = sprites ? sprites[i] : (finale_sprite){0};
    }
    b->layer_count = count;
    memcpy(b->draw_order, b->next_order, count);
    /* Rendeng (205E90) draws in list order, then exchanges the current
     * object with its predecessor when farther away. Only the next frame
     * sees the exchange. New bolts and impacts share this list. */
    double depth[LAYERS_MAX];
    for (unsigned i = 0; i < count; ++i) {
        unsigned id = b->draw_order[i];
        depth[id] = id < BODY_FACES ? b->joints[b->faces[id].joint].xyz[2] + b->faces[id].depth
                                   : b->effects[id - BODY_FACES].depth;
    }
    for (unsigned i = 1; i < count; ++i) {
        uint8_t previous = b->next_order[i - 1], current = b->next_order[i];
        if (depth[current] > depth[previous]) {
            b->next_order[i - 1] = current;
            b->next_order[i] = previous;
        }
    }
}
static void effect_draw(const finale_body *b, uint8_t *rgba, const finale_sprite *s,
                        const uint32_t *palette) {
    if (!s->width || !s->height)
        return;
    /* Scaled sprites cover both VDP1 endpoints, duplicating the center texel. */
    for (unsigned y = 0; y <= s->height; y++) {
        unsigned ty = y - (2u * y >= s->height);
        for (unsigned x = 0; x <= s->width; x++) {
            unsigned tx = x - (2u * x >= s->width);
            unsigned index = s->pixels[ty * s->width + tx];
            if (index) {
                pixel at = {s->x + (int)x, s->y + (int)y};
                paint_texel(b->backdrop, b->nbg2_mask, rgba, at, palette[index], 1);
            }
        }
    }
}
void finale_body_draw(finale_body *b, uint8_t *rgba, unsigned hit_age, unsigned segments) {
    if (!b || !rgba)
        return;
    memcpy(b->backdrop, rgba, sizeof b->backdrop);
    uint32_t *colors = b->display_colors;
    memcpy(colors, b->normal, sizeof b->normal);
    /* PalCurrent(_BonesRedPal), then PalFade(_BonesNormPal,2).
     * CRAM latch: two full red frames, then each channel approaches its
     * target independently by two levels per display tick. */
    if (hit_age && hit_age < 130u) {
        int step = hit_age > 2u ? (int)(hit_age - 2u) * 2 : 0;
        for (unsigned i = 0; i < 16; i++) {
            uint32_t rgb = 0;
            for (unsigned shift = 0; shift < 24; shift += 8) {
                int from = (b->red[i] >> shift) & 255, to = (b->normal[i] >> shift) & 255;
                int diff = to - from;
                int channel = from + (diff > step ? step : diff < -step ? -step : diff);
                rgb |= (uint32_t)channel << shift;
            }
            colors[i] = rgb;
        }
    }
    memcpy(colors + 128, b->guitar_pal[b->visible_pal], sizeof b->guitar_pal[0]);
    for (unsigned i = 0; i < b->layer_count; i++) {
        unsigned id = b->draw_order[i];
        if (id >= BODY_FACES)
            /* CFace selects bank 8100 and the shots select 8200, both at priority 4. */
            effect_draw(b, rgba, &b->effects[id - BODY_FACES],
                        id == BODY_FACES + FINALE_FACE_SPRITE ? b->face_colors : b->ring_colors);
        else
            shape_draw(b, rgba, &b->faces[id], &b->joints[b->faces[id].joint], colors);
    }
    finale_body_draw_health(b, rgba, segments);
}
/* Both ice overlays instantiate the standard 21 CBonelets. CBnsIceFlr
 * sets the camera behind the XY plane and BonesStageInit rotates the body
 * into that plane. 04STXT contains the textures for the projected faces. */
finale_body *ice_body_load(const void *code, size_t code_size, const void *texture,
                          size_t texture_size, uint32_t init, unsigned skate,
                          const uint8_t palette_rgba[4096]) {
    finale_body *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->code = code;
    b->code_size = code_size;
    b->texture = texture;
    b->texture_size = texture_size;
    b->init = init;
    b->ice_mode = skate ? 2 : 1;
    b->joint_count = 21;
    b->camera_depth = skate ? 3.5 : 8.;
    b->config = word(b, init);
    b->master = word(b, word(b, b->config + 124) + 12) - 168;
    if (!data(b, b->config, 124 + 4*b->joint_count) || !data(b, b->master, 168*b->joint_count)) {
        free(b);
        return NULL;
    }
    b->motion_next = skate ? 0x299e28u : 0x2a78acu;
    b->motion_loop = skate ? 0x299e4cu : 0x2a78d0u;
    for (unsigned i = 0; i < 1024; ++i)
        b->sine[i] = (int16_t)(256. * sin(6.2831853071795864769 * i / 1024.));
    for (unsigned i = 0; i < 256; ++i) {
        const uint8_t *c = palette_rgba + 4*i;
        b->normal[i] = c[1] | (uint32_t)c[2] << 8 | (uint32_t)c[3] << 16;
    }
    memcpy(b->display_colors,b->normal,sizeof b->normal);
    if (!body_health(b,skate ? 0x235f68u : 0x23a018u)) {free(b);return NULL;}
    register_body_faces(b);
    return b;
}

void ice_body_draw(finale_body *b, uint8_t *rgba, unsigned tick, int track_mode,
                   int heading, double world_x, double world_y, double world_z) {
    if (!b || !rgba) return;
    unsigned display_delay = b->ice_mode == 2 ? 4u : 5u;
    uint32_t standing = b->ice_mode == 2 ? 0x23cab4u : 0x243b28u;
    uint32_t stand=word(b,b->config+4);
    uint32_t tracks=track_mode ? word(b,word(b,stand+(track_mode==2 ? 24 : 16))) : standing;
    if (!data(b, tracks, 4 * (b->joint_count + 1))) return;
    if (!track_mode && !b->ice_motion_running) {
        unsigned keytick=tick>display_delay ? tick-display_delay : 0;
        motion *m=&b->visible.players[0];
        if(m->tracks!=tracks) *m=(motion){tracks,0,255,0};
        else if (keytick!=b->visible.ticks && keytick>=4 && !(keytick&3u)) m->frame=next_frame(b,*m);
        b->visible.count=1;
        b->visible.ticks=keytick;
        b->submitted=b->prepared=b->live=b->visible;
    } else if(tick!=b->ice_motion_tick) {
        b->ice_motion_running=1;
        b->visible=b->submitted;
        b->submitted=b->live;
        unsigned count=0;
        for(unsigned i=0;i<b->live.count;++i) {
            motion m=b->live.players[i];
            int weight=(int)m.weight+m.delta;
            if(weight<=0)continue;
            m.weight=weight>255 ? 255u : (unsigned)weight;
            if(tick%4==2) m.frame=next_frame(b,m);
            b->live.players[count++]=m;
        }
        if((unsigned)track_mode!=b->ice_track_mode) {
            int rate=track_mode==2 ? 32 : 24;
            /* BonesWalk starts the new player with positive weight as surviving
             * players begin their negative ramps. Tracks come from Stand+16/24. */
            for(unsigned i=0;i<count;++i)b->live.players[i].delta=-rate;
            if(count<sizeof b->live.players/sizeof *b->live.players)
                b->live.players[count++]=(motion){tracks,0,(unsigned)rate,rate};
            b->ice_track_mode=(unsigned)track_mode;
        }
        b->live.count=count;
        b->live.ticks=(tick+2)&3u;
    }
    b->ice_motion_tick=tick;
    b->root_xyz[0] = world_x;
    b->root_xyz[1] = world_y;
    b->root_xyz[2] = world_z;
    b->heading = heading;
    prepare_layers(b, NULL);
    memcpy(b->backdrop, rgba, sizeof b->backdrop);
    for (unsigned i = 0; i < b->layer_count; ++i) {
        const layer *face = &b->faces[b->draw_order[i]];
        shape_draw(b, rgba, face, &b->joints[face->joint], b->normal);
    }
}

void finale_body_draw_health(const finale_body *b, uint8_t *rgba, unsigned segments) {
    if (!b || !rgba)
        return;
    for (unsigned i = 0; i < segments && i < 14; i++) {
        const health_piece *piece = &b->health[i];
        for (unsigned y = 0; y < piece->tex.height; y++) {
            const uint8_t *src = piece->tex.data + y * piece->tex.width;
            for (unsigned x = 0; x < piece->tex.width; x++) {
                if (!src[x])
                    continue;
                pixel at = {piece->x + (int)x, piece->y + (int)y};
                paint_texel(b->backdrop, NULL, rgba, at, b->display_colors[src[x]], 1);
            }
        }
    }
}

finale_body *stage_body_load_joints(const void *code,size_t size,uint32_t init,
                                    uint32_t next_key,uint32_t loop_key) {
    finale_body *b=calloc(1,sizeof *b);
    if (!b) return NULL;
    b->code=code;
    b->code_size=size;
    b->init=init;
    b->config=word(b,init);
    b->joint_count=21;
    b->master=word(b,word(b,b->config+124)+12)-168u;
    if (!data(b,init,26) || !data(b,b->config,124+4*b->joint_count) ||
        !data(b,b->master,168*b->joint_count)) {
        free(b);
        return NULL;
    }
    b->motion_next=next_key;
    b->motion_loop=loop_key;
    for(unsigned i=0;i<1024;++i)
        b->sine[i]=(int16_t)(256.*sin(6.2831853071795864769*i/1024.));
    return b;
}

int stage_body_joints(finale_body *b,const stage_body_pose *pose,int32_t points[JOINTS][3]) {
    if (!b || !pose || !points || !data(b,pose->tracks,4*(b->joint_count+1))) return 0;
    uint32_t config=pose->config ? pose->config : word(b,b->init);
    if (!data(b,config,124+4*b->joint_count)) return 0;
    b->config=config;
    motion *m=&b->visible.players[0];
    if (m->tracks!=pose->tracks || pose->tick<b->visible.ticks) {
        *m=(motion){pose->tracks,0,255,0};
        b->visible.ticks=0;
    }
    for(unsigned i=b->visible.ticks/4;i<pose->tick/4;++i)
        m->frame=next_frame(b,*m);
    b->visible.ticks=pose->tick;
    b->visible.count=1;
    b->world_pose=pose;
    memset(b->joints,0,sizeof b->joints);
    skeleton(b,b->joints);
    b->world_pose=NULL;
    for (unsigned i=0;i<b->joint_count;++i)
        for (unsigned k=0;k<3;++k) {
            points[i][k]=(int32_t)(b->joints[i].xyz[k]*65536.);
        }
    return (int)b->joint_count;
}
