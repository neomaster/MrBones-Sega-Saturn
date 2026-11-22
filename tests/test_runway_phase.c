#include "core/bones_tall.h"
#include "core/cdimage.h"
#include "core/identity.h"
#include "core/runway_phase.h"
#include "core/runway_projection.h"
#include "core/runway_sfx.h"
#include "core/saturn_pad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

/* US ICE2, same SMPC reports as the winning Ymir run. Each pair contains
 * a count of BonesBuild updates followed by the host pad mask. The original
 * took 581 video retraces for 579 updates. */
static int ice2_saturn_replay(runway_phase *p) {
    static const uint8_t runs[][2]={
        {36,6},{35,4},{21,5},{48,4},{2,6},{1,4},{2,6},{1,4},{4,6},{1,4},
        {1,6},{10,4},{15,5},{1,1},{2,5},{1,1},{2,5},{1,1},{10,5},{9,4},
        {12,6},{1,2},{2,6},{5,2},{1,6},{29,2},{1,10},{2,2},{1,10},{2,2},
        {1,10},{1,2},{1,10},{5,2},{4,6},{9,4},{20,5},{44,1},{14,5},{2,4},
        {1,5},{32,4},{40,6},{1,4},{5,6},{9,4},{24,5},{1,1},{14,5},{10,4},
        {19,6},{15,2},{1,6},{1,2},{15,6},{12,4},{8,5},{1,1},{10,9}
    };
    saturn_pad pad={0};
    uint32_t trajectory=2166136261u;
    for(unsigned i=0;i<sizeof runs/sizeof *runs;++i) for(unsigned j=0;j<runs[i][0];++j) {
        CHECK(!runway_phase_done(p));
        saturn_pad_update_host(&pad,runs[i][1]);
        CHECK(runway_phase_step(p,&pad)==0);
        uint32_t axes[2]={(uint32_t)p->ice_x,(uint32_t)p->ice_y};
        for(unsigned a=0;a<2;++a) for(int shift=24;shift>=0;shift-=8)
            trajectory=(trajectory^(uint8_t)(axes[a]>>shift))*16777619u;
        if(p->frames==60) {
            uint32_t rgb=2166136261u;
            for(unsigned px=0;px<320*224;++px)for(unsigned c=0;c<3;++c)
                rgb=(rgb^p->frame_rgba[4*px+c])*16777619u;
            CHECK(rgb==0x4671b37cu);
        }
    }
    CHECK(trajectory==0xea185ba3u && p->frames==579);
    CHECK(p->state==RUNWAY_ST_FINISHED && p->ice_x==-341119 && p->ice_y==-30593);
    return 0;
}

static int course_tape(runway_phase *p,unsigned limit,int autoplay,unsigned held) {
    saturn_pad pad={0};
    while (p->frames<limit && !runway_phase_done(p)) {
        unsigned buttons=autoplay ? runway_phase_autoplay_buttons(p,p->frames) : held;
        saturn_pad_update_host(&pad,buttons);
        CHECK(runway_phase_step(p,&pad)==0);
    }
    return 0;
}

static int course_negative(cdimage *cd,const char *dir,unsigned limit) {
    runway_phase p;
    for (unsigned mode=0;mode<2;++mode) {
        CHECK(runway_phase_load_dir(&p,cd,dir)==0);
        CHECK(course_tape(&p,limit,0,mode?MB_PAD_RIGHT:0)==0);
        CHECK(p.state!=RUNWAY_ST_FINISHED);
        if(p.course==COURSE_MOAT) CHECK(!p.nuts_released);
        else CHECK(p.state==RUNWAY_ST_LOST);
        runway_phase_free(&p);
    }
    return 0;
}

/* Backdrop column mapping (synthetic, no disc). The strip never wraps: the offset
 * (camera / ring index) is clamped to strip_w - 320, so a 320-column view always
 * stays inside the strip, adjacent screen columns stay adjacent (no seam) and the
 * offset only grows with the camera and the 15PAN ring index. */
static int test_bg_column(void) {
    static const unsigned widths[] = { 640u, 1864u, 2048u };
    unsigned wi, x, cam, idx;
    CHECK(runway_phase_bg_column(5u, 10u, 10u, 0u) == 0u);
    for (wi = 0u; wi < sizeof widths / sizeof widths[0]; wi++) {
        const unsigned w = widths[wi];
        const unsigned room = w - 320u;
        for (cam = 0u; cam <= 5000u; cam += 37u) {
            for (idx = 0u; idx <= 6000u; idx += 331u) {
                unsigned raw = cam / PAN_SCRL_BG_CAM_DIV + idx / PAN_SCRL_BG_IDX_DIV;
                unsigned dx = raw < room ? raw : room;
                for (x = 0u; x < 320u; x++) {
                    unsigned c0 = runway_phase_bg_column(x, cam, idx, w);
                    CHECK(c0 < w);
                    CHECK(c0 == x + dx);
                    if (x + 1u < 320u)
                        CHECK(runway_phase_bg_column(x + 1u, cam, idx, w) == c0 + 1u);
                }
            }
        }
        /* Different camera / ring index sample different strip columns. */
        CHECK(runway_phase_bg_column(7u, 40u, 0u, w) != runway_phase_bg_column(7u, 240u, 0u, w));
        CHECK(runway_phase_bg_column(7u, 100u, 0u, w) != runway_phase_bg_column(7u, 100u, 117u, w));
        /* Distant layer: one camera pixel moves the backdrop by at most one column. */
        for (cam = 0u; cam < 800u; cam++) {
            unsigned a = runway_phase_bg_column(0u, cam, 0u, w);
            unsigned b = runway_phase_bg_column(0u, cam + 1u, 0u, w);
            CHECK(b >= a && b - a <= 1u);
        }
        /* Clamp: far past the strip end the view stays on the last 320 columns. */
        CHECK(runway_phase_bg_column(0u, 4000000u, 4000000u, w) == room);
        CHECK(runway_phase_bg_column(319u, 4000000u, 4000000u, w) == w - 1u);
    }
    return 0;
}

static int test_runway_obstacle(cdimage *cd, int jump_tick, int jump) {
    runway_phase phase;
    saturn_pad pad = {0};
    int32_t approach_x = 0, sy;
    int approach_feet = 0;
    CHECK(runway_phase_load(&phase, cd) == 0);
    for (int tick = 1; tick <= jump_tick + 54; tick++) {
        unsigned buttons = MB_PAD_RIGHT;
        /* On the way to the stone, both runs clear the preceding fence. */
        if ((jump_tick > 350 && tick >= 350 && tick < 386) ||
            (jump && tick >= jump_tick && tick < jump_tick + 36))
            buttons |= MB_PAD_B;
        saturn_pad_update_host(&pad, buttons);
        CHECK(runway_phase_step(&phase, &pad) == 0 && !runway_phase_done(&phase));
        if (tick == jump_tick - 1) {
            runway_project_center(phase.camera_x, phase.camera_y, phase.bones_x, phase.bones_y,
                                  &approach_x, &sy);
            approach_feet = (int)(phase.bones_y / phase.enemies.units_per_pixel);
        }
        if (tick >= jump_tick)
            CHECK(!!(phase.sfx_flags & RUNWAY_SFX_FLAG_JUMP) == (jump && tick == jump_tick));
        if (tick == jump_tick + 22) {
            int32_t sx;
            int feet = (int)(phase.bones_y / phase.enemies.units_per_pixel);
            runway_project_center(phase.camera_x, phase.camera_y, phase.bones_x, phase.bones_y, &sx, &sy);
            CHECK(phase.pan.index == (jump_tick == 350 ? 93u : 183u));
            int top = pan_collision_floor(&phase.collision, 160 - approach_x, approach_feet);
            CHECK(top > 0 && top < approach_feet);
            if (jump) {
                CHECK(phase.airborne && sx == approach_x && feet < top);
            } else {
                CHECK(!phase.airborne && sx > approach_x && feet > top);
                CHECK(phase.vel[1] == 0);
            }
        }
    }
    int32_t end_x;
    runway_project_center(phase.camera_x, phase.camera_y, phase.bones_x, phase.bones_y, &end_x, &sy);
    CHECK(jump ? end_x == approach_x : end_x > approach_x + 30);
    CHECK(!phase.airborne && phase.state == RUNWAY_ST_WALK);
    runway_phase_free(&phase);
    return 0;
}

int main(int argc, char **argv) {
    const char *disc = getenv("MB_DISC1");
    const char *man = getenv("MB_MANIFEST");
    (void)argc;
    (void)argv;
    if (test_bg_column()) return 1;
    if (!disc || !man) {
        printf("SKIP runway_phase (set MB_DISC1 and MB_MANIFEST)\n");
        return 0;
    }
    char err[512];
    cdimage cd;
    id_manifest manifest;
    id_result result;
    runway_phase phase;
    CHECK(manifest_load(&manifest, man, err, sizeof err) == 0);
    CHECK(cd_open(&cd, disc, err, sizeof err) == 0);
    CHECK(identity_check(&manifest, &cd, 1, ID_QUICK, &result));
    manifest_free(&manifest);
    for (int jump = 0; jump <= 1; jump++) {
        CHECK(test_runway_obstacle(&cd, 350, jump) == 0); /* fence */
        CHECK(test_runway_obstacle(&cd, 710, jump) == 0); /* gravestone */
    }
    CHECK(runway_phase_load(&phase, &cd) == 0);
    CHECK(phase.bg_rgba && phase.frame_rgba && phase.sprite_rgba);
    /* §12l/§12m: morph×13 + full×20; 15PAN stream; stand brain slot. */
    CHECK(phase.anim_bank_count[0] == 13u);
    CHECK(phase.anim_bank_count[1] == 20u);
    CHECK(phase.anim_rgba[0][0] && phase.anim_rgba[0][12]);
    CHECK(phase.anim_rgba[1][0] && phase.anim_rgba[1][19]);
    CHECK(phase.sprite_rgba == phase.anim_rgba[phase.anim_bank][phase.anim_index]);
    CHECK(phase.hit.top_right_x == 5 && phase.hit.bot_left_y == 12);
    CHECK(phase.pan.count > 1u);
    CHECK(phase.pan.index == 0u);
    CHECK(phase.state == RUNWAY_ST_WALK && phase.anim_index == 0);
    CHECK(phase.bones_x - phase.camera_x == 0x2b333u);
    CHECK(phase.anim_bank == RUNWAY_ANIM_BANK_FULL && phase.mplay.duration == 3);
    CHECK(phase.gang_start_tick == 34 && phase.enemies.count == 4);
    CHECK(phase.gang_tracks[0].count == 10391 && phase.gang_tracks[3].count == 10206);
    CHECK(phase.gang_shapes[0].anchor_x == 24 && phase.gang_shapes[0].anchor_y == 43);
    CHECK(phase.hazards.zone_count == 0u);
    CHECK(phase.fall_height == -2338230 && phase.fall_index == 0u);
    CHECK(phase.fall_profile[0].x == -9168018 && phase.fall_profile[0].y == -121709);
    CHECK(phase.fall_profile[26].x == -74149302);
    CHECK(phase.butterfly_count == 16u && phase.butterfly_start[0] == 401u);
    CHECK(phase.butterfly_anim[25].end_tick > 0 && phase.butterfly_anim[0].shape.anchor_x == 7);
    CHECK(phase.brain_slot == BONES_BRAIN_STAND);
    CHECK(phase.tall == 0);
    /* Boot posts 0x00020A00; first step consumes it. */
    CHECK(phase.last_message == 0 || phase.bbs.count >= 1);
    {
        saturn_pad boot = {0};
        saturn_pad_update_host(&boot, 0);
        runway_phase_step(&phase, &boot);
        CHECK(phase.last_message == 0x00020A00u || phase.obj_flags & 0x01u);
        for (int i = 0; i < 2; i++) runway_phase_step(&phase, &boot);
        CHECK(phase.state == RUNWAY_ST_WALK && phase.anim_index == 1);
        CHECK(phase.pan.index == 0 && phase.bones_x - phase.camera_x == 0x2b333u - 3u * 1966u);
    }
    /* Down → 0x10700 → SoundEvent(0xc) + BrainSwitch crouch → TallSet + BonesSetConfig HitChk;
     * release → stand. Crouch8 AABB ≡ bnscfg 0x2873EC (±5,−8)/(±5,+12). */
    {
        saturn_pad down = {0};
        saturn_pad_update_host(&down, MB_PAD_DOWN);
        runway_phase_step(&phase, &down);
        CHECK(phase.tall == 1);
        CHECK(phase.last_message == 0x00010700u);
        CHECK(phase.sched_enqueued_0c == 1);
        CHECK(phase.sound_event_id == 0xcu); /* §12n: _SoundEvent@0x204128 */
        CHECK(bones_is_crouch_slot(phase.brain_slot));
        CHECK(phase.brain_slot == BONES_CROUCH_SLOT0);
        CHECK(phase.hit.top_right_x == 5 && phase.hit.top_right_y == -8);
        CHECK(phase.hit.top_left_x == -5 && phase.hit.bot_left_y == 12);
        saturn_pad_update_host(&down, 0);
        runway_phase_step(&phase, &down);
        CHECK(phase.tall == 0);
        CHECK(phase.brain_slot == BONES_BRAIN_STAND);
        CHECK(!bones_is_crouch_slot(phase.brain_slot));
        CHECK(phase.sound_event_id == 0u);
        CHECK(phase.hit.bot_right_x == 5 && phase.hit.bot_right_y == 12);
    }
    /* C edge posts 0x10600 then 0x20600; fires Blues bolt. A fires radial pulse. */
    {
        saturn_pad cpad = {0};
        saturn_pad_update_host(&cpad, MB_PAD_C);
        runway_phase_step(&phase, &cpad);
        CHECK(phase.last_message == 0x00020600u || phase.last_message == 0x00010600u);
        CHECK(phase.enemies.bolts_fired >= 1u);
        saturn_pad_update_host(&cpad, MB_PAD_A);
        runway_phase_step(&phase, &cpad);
        CHECK(phase.enemies.bolts_fired >= 2u);
        CHECK((phase.sfx_flags & RUNWAY_SFX_FLAG_C) != 0u);
    }
    saturn_pad pad = {0};
    uint32_t start_x = phase.bones_x;
    unsigned anim0 = phase.anim_index;
    CHECK(phase.runway_win_record == 2635u);
    unsigned pan0 = phase.pan.index;
    saturn_pad_update_host(&pad, MB_PAD_RIGHT);
    int changed_pose = 0;
    for (int i = 0; i < 30; i++) {
        runway_phase_step(&phase, &pad);
        if (phase.anim_index != anim0) changed_pose = 1;
    }
    CHECK(phase.bones_x != start_x || phase.state == RUNWAY_ST_STAND);
    CHECK(phase.facing == 1);
    /* Stand→Walk posts 0x11001 (BonesWalk entry); NearVel drives bones_x @0x2858BC. */
    if (phase.bones_x != start_x && phase.state == RUNWAY_ST_WALK) {
        CHECK(phase.last_message == 0x00011001u || phase.obj_flags & 0x02u);
        /* InputBrain leaves upright right stationary against the scrolling PAN. */
        CHECK(phase.vel[0] == 0 || phase.tall || phase.hurt_timer);
    }
    /* §22: camera advance may seek pandcomp; collision tracks live pan map. */
    if (phase.pan.index != pan0) {
        CHECK(memcmp(&phase.collision, &phase.pan.collision,
                     sizeof phase.collision) == 0);
    }
    /* The forced chase keeps the full-body run cycle even with no screen-space thrust. */
    if (phase.bones_x != start_x && phase.state == RUNWAY_ST_WALK) {
        CHECK(phase.anim_bank == RUNWAY_ANIM_BANK_FULL);
        CHECK(changed_pose || phase.anim_count <= 1);
    }
    /* Facing left → flip_x path (RWorldSprite H-flip). */
    {
        saturn_pad_update_host(&pad, MB_PAD_LEFT);
        for (int i = 0; i < 10; i++) runway_phase_step(&phase, &pad);
        CHECK(phase.facing == -1);
    }
    /* B edge → air (MSG_JUMP 0x20800); Y rises (smaller bones_y) then lands. */
    {
        uint32_t y0 = phase.bones_y;
        saturn_pad_update_host(&pad, 0);
        runway_phase_step(&phase, &pad);
        saturn_pad_update_host(&pad, MB_PAD_B);
        runway_phase_step(&phase, &pad);
        CHECK(phase.airborne == 1 || phase.state == RUNWAY_ST_AIR);
        CHECK(phase.bones_y <= y0);
        saturn_pad_update_host(&pad, 0);
        for (int i = 0; i < 90; i++) runway_phase_step(&phase, &pad);
        CHECK(phase.airborne == 0);
        CHECK(phase.state == RUNWAY_ST_STAND || phase.state == RUNWAY_ST_WALK);
    }
    /* Start toggles PauseBrain and freezes the PAN clock. */
    {
        saturn_pad_update_host(&pad, 0);
        runway_phase_step(&phase, &pad);
        runway_phase_state before = phase.state;
        saturn_pad_update_host(&pad, MB_PAD_START);
        runway_phase_step(&phase, &pad);
        CHECK(phase.state == RUNWAY_ST_PAUSED);
        CHECK(phase.skel.paused == 1);
        saturn_pad_update_host(&pad, 0); /* release for next Start edge */
        runway_phase_step(&phase, &pad);
        CHECK(phase.state == RUNWAY_ST_PAUSED); /* still paused while Start up */
        saturn_pad_update_host(&pad, MB_PAD_START);
        runway_phase_step(&phase, &pad);
        CHECK(phase.state == before);
        CHECK(phase.skel.paused == 0);
    }
    /* Backward pan_scroll_seek via 32-frame keyframe cache matches forward decode byte-for-byte. */
    {
        uint32_t hash_bg_fwd = 2166136261u, hash_coll_fwd = 2166136261u;
        uint32_t hash_bg_bwd = 2166136261u, hash_coll_bwd = 2166136261u;
        CHECK(pan_scroll_seek(&phase.pan, 70u) == 0);
        for (size_t bi = 0; bi < (size_t)PAN_WIDTH * PAN_HEIGHT * 4u; bi++)
            hash_bg_fwd = (hash_bg_fwd ^ phase.pan.bg_rgba[bi]) * 16777619u;
        for (size_t ci = 0; ci < phase.pan.collision.size; ci++)
            hash_coll_fwd = (hash_coll_fwd ^ phase.pan.collision.cells[ci]) * 16777619u;
        CHECK(pan_scroll_seek(&phase.pan, 110u) == 0);
        CHECK(pan_scroll_seek(&phase.pan, 70u) == 0);
        for (size_t bi = 0; bi < (size_t)PAN_WIDTH * PAN_HEIGHT * 4u; bi++)
            hash_bg_bwd = (hash_bg_bwd ^ phase.pan.bg_rgba[bi]) * 16777619u;
        for (size_t ci = 0; ci < phase.pan.collision.size; ci++)
            hash_coll_bwd = (hash_coll_bwd ^ phase.pan.collision.cells[ci]) * 16777619u;
        CHECK(hash_bg_fwd == hash_bg_bwd);
        CHECK(hash_coll_fwd == hash_coll_bwd);
        CHECK(pan_scroll_seek(&phase.pan, 0u) == 0);
    }
    /* Walk to goal (EvalSuccess / EndLevel win) using autoplay over solid obstacles. */
    for (int i = 0; (unsigned)i <= 4u * phase.runway_win_record && !runway_phase_done(&phase); i++) {
        saturn_pad_update_host(&pad, runway_phase_autoplay_buttons(&phase, (unsigned)i));
        runway_phase_step(&phase, &pad);
    }
    if (phase.state != RUNWAY_ST_FINISHED)
        fprintf(stderr, "RUNWAY PAN %u, X relative %d, fall row %u, health pickups %u: %s\n",
                phase.pan.index, (int32_t)(phase.bones_x - phase.camera_x) / 2341,
                phase.fall_index, phase.enemies.pickups_collected, phase.status);
    CHECK(phase.state == RUNWAY_ST_FINISHED);
    CHECK(phase.pan.index == 2635u && phase.runway_clock == 10540u);
    CHECK(phase.fall_index >= 25u && phase.butterfly_collected != 0u);
    CHECK(phase.skel.skeletism > 0);
    runway_phase_free(&phase);

    /* Jumping over CThrown avoids hit; idling on hazard/enemy kills Bones. */
    CHECK(runway_phase_load(&phase, &cd) == 0);
    {
        unsigned u = phase.enemies.units_per_pixel ? phase.enemies.units_per_pixel : 2341u;
        saturn_pad jp = {0};
        runway_hazards_spawn_thrown_vel(&phase.hazards, phase.bones_x,
                                        phase.bones_y, 0, RUNWAY_DAMAGE_DEFAULT);
        saturn_pad_update_host(&jp, MB_PAD_B);
        runway_phase_step(&phase, &jp);
        saturn_pad_update_host(&jp, 0);
        for (int i = 0; i < 4; i++) runway_phase_step(&phase, &jp);
        CHECK(phase.skel.skeletism == RUNWAY_SKEL_MAX);

        phase.bones_x = 125u * u;
        for (int i = 0; i <= 0x2d0 && !runway_phase_done(&phase); i++)
            runway_phase_step(&phase, &jp);
        CHECK(phase.state == RUNWAY_ST_LOST && phase.skel.died == 1);
    }
    runway_phase_free(&phase);

    /* 11LOGS (disc 1): its only backdrop plane is D 320x224, a full-screen picture
     * whose left/right edges do not match, so the backdrop stays frozen (no seam). */
    CHECK(runway_phase_load_dir(&phase, &cd, "/11LOGS") == 0);
    CHECK(phase.scrl_bg_rgba != NULL && phase.scrl_bg_w==0);
    CHECK(phase.pan_event_count==152 && phase.course_x==-400660 && phase.course_y==-122880);
    CHECK(course_tape(&phase,phase.pan.count*4+120,1,0)==0);
    CHECK(phase.state==RUNWAY_ST_FINISHED && phase.pan.index>2856);
    CHECK(phase.pan_event_index==152 && phase.section==2);
    runway_phase_free(&phase);
    CHECK(course_negative(&cd,"/11LOGS",12000)==0);
    cd_close(&cd);

    /* Etapa 3: 15PAN physics/pacing profiles on Disc 2 (ICE1/ICE2 coast, MOAT WINGNUT, RIDE/STAING). */
    {
        const char *d2 = getenv("MB_DISC2");
        if (d2) {
            cdimage cd2;
            CHECK(cd_open(&cd2, d2, err, sizeof err) == 0);
            {
                const char *const ice_stages[] = {"/20ICE1", "/21ICE2"};
                for (unsigned skate = 0; skate < 2; ++skate) {
                    const int finish_y = skate ? -0x8c46 : -0x14000;
                    CHECK(runway_phase_load_dir(&phase, &cd2, ice_stages[skate]) == 0);
                    CHECK(phase.ice_path_count == (skate ? 43u : 86u));
                    CHECK(phase.ice_node_count == (skate ? 765u : 1622u));
                    CHECK(phase.ice_block_count == (skate ? 0u : 64u));
                    CHECK(phase.ice_x == (skate ? -179550 : -1228800));
                    CHECK(phase.ice_y == (skate ? -1885282 : -16384000));
                    CHECK(phase.scrl_bg_w == (skate ? 640u : 1864u));
                    CHECK(phase.hazards.zone_count == 0);
                    if (skate) {
                        /* Disc 2, neutral CBnsIceFlr Brain tick 60, Ymir displayFB 0.
                         * Includes the underwater mesh, original health bar and the lake. */
                        uint32_t rgb = 2166136261u;
                        CHECK(course_tape(&phase,60,0,0)==0);
                        for (unsigned pixel=0;pixel<320*224;++pixel) for (unsigned c=0;c<3;++c)
                            rgb=(rgb ^ phase.frame_rgba[4*pixel+c])*16777619u;
                        CHECK(rgb==0x43f71baeu);
                    }
                    if (!skate) {
                        /* First authored block, first waypoint: travel
                         * (30720,-235520) over 300 ticks, carrying the rider. */
                        phase.ice_x = -1294336; phase.ice_y = -786432;
                        CHECK(course_tape(&phase,1,0,0) == 0 && phase.ice_support == 0);
                        int32_t start_x = phase.ice_x, start_y = phase.ice_y;
                        CHECK(course_tape(&phase,6,0,0) == 0 && phase.ice_carried == 5);
                        CHECK(phase.ice_x == start_x + 5*(30720/300));
                        CHECK(phase.ice_y == start_y - 5*(235520/300));
                        saturn_pad jumping = {0};
                        saturn_pad_update_host(&jumping,MB_PAD_B);
                        CHECK(runway_phase_step(&phase,&jumping)==0 && phase.ice_support == -1);
                        CHECK(phase.airborne && phase.state != RUNWAY_ST_FINISHED);
                    }
                    runway_phase_free(&phase);
                    for (unsigned negative=0;negative<2;++negative) {
                        CHECK(runway_phase_load_dir(&phase,&cd2,ice_stages[skate])==0);
                        CHECK(course_tape(&phase,600,0,negative?MB_PAD_RIGHT:0)==0);
                        CHECK(phase.state!=RUNWAY_ST_FINISHED && phase.ice_y<=finish_y);
                        runway_phase_free(&phase);
                    }
                    CHECK(runway_phase_load_dir(&phase,&cd2,ice_stages[skate])==0);
                    CHECK(course_tape(&phase,10000,1,0)==0);
                    CHECK(phase.state==RUNWAY_ST_FINISHED && phase.ice_y>finish_y);
                    runway_phase_free(&phase);
                    if(skate) {
                        CHECK(runway_phase_load_dir(&phase,&cd2,ice_stages[skate])==0);
                        CHECK(ice2_saturn_replay(&phase)==0);
                        runway_phase_free(&phase);
                    }
                }

                CHECK(runway_phase_load_dir(&phase,&cd2,"/23MOAT")==0);
                CHECK(phase.scrl_bg_rgba && !phase.scrl_bg_w);
                CHECK(phase.nuts[0].turns==6 && phase.nuts[1].turns==6);
                unsigned target_bits=0;
                for(unsigned t=0;t<9;++t) target_bits|=1u<<phase.sparky_targets[t].id;
                CHECK(target_bits==0x3feu);
                memset(&pad,0,sizeof pad);
                while (!phase.nuts_released && phase.frames<5000) {
                    saturn_pad_update_host(&pad,runway_phase_autoplay_buttons(&phase,phase.frames));
                    CHECK(runway_phase_step(&phase,&pad)==0);
                }
                CHECK(phase.nuts_released==1 && phase.nuts[1].turns==6);
                CHECK(course_tape(&phase,phase.frames+560,0,MB_PAD_DOWN)==0);
                CHECK(phase.nuts_released==1 && phase.state!=RUNWAY_ST_FINISHED && phase.moat_clip!=11);
                CHECK(course_tape(&phase,5000,1,0)==0);
                CHECK(phase.state==RUNWAY_ST_FINISHED && phase.nuts_released==3);
                CHECK(phase.moat_clip==11 && phase.pan.index==120);
                runway_phase_free(&phase);
                CHECK(course_negative(&cd2,"/23MOAT",5000)==0);

                CHECK(runway_phase_load_dir(&phase,&cd2,"/24STAING")==0);
                CHECK(phase.pan_event_count==133);
                CHECK(course_tape(&phase,phase.pan.count*4+120,1,0)==0);
                CHECK(phase.state==RUNWAY_ST_FINISHED && phase.pan.index>=2520);
                CHECK(phase.glass_hits>=12 && phase.rewards>100);
                runway_phase_free(&phase);
                CHECK(course_negative(&cd2,"/24STAING",10500)==0);

                CHECK(runway_phase_load_dir(&phase,&cd2,"/18RIDE")==0);
                CHECK(phase.scrl_bg_rgba && phase.scrl_bg_w==0);
                for(unsigned c=0;c<8;++c) CHECK(phase.current.channels[c].count>0);
                CHECK(course_tape(&phase,phase.pan.count*4+120,1,0)==0);
                CHECK(phase.state==RUNWAY_ST_FINISHED && phase.pan.index==2396);
                runway_phase_free(&phase);
                CHECK(course_negative(&cd2,"/18RIDE",10000)==0);
            }
            cd_close(&cd2);
        }
    }
    return 0;
}
