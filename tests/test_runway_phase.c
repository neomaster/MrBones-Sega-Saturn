#include "core/bones_tall.h"
#include "core/cdimage.h"
#include "core/identity.h"
#include "core/runway_phase.h"
#include "core/saturn_pad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(int argc, char **argv) {
    const char *disc = getenv("MB_DISC1");
    const char *man = getenv("MB_MANIFEST");
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
    CHECK(phase.hazards.zone_count == 3u); /* §22 RUNWAY hazards */
    CHECK(phase.brain_slot == BONES_BRAIN_STAND);
    CHECK(phase.tall == 0);
    /* Boot posts 0x00020A00; first step consumes it. */
    CHECK(phase.last_message == 0 || phase.bbs.count >= 1);
    {
        saturn_pad boot = {0};
        saturn_pad_update_host(&boot, 0);
        runway_phase_step(&phase, &boot);
        CHECK(phase.last_message == 0x00020A00u || phase.obj_flags & 0x01u);
    }
    /* Down → 0x10700 → SoundEvent(0xc) + BrainSwitch crouch → TallSet; release → stand. */
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
        saturn_pad_update_host(&down, 0);
        runway_phase_step(&phase, &down);
        CHECK(phase.tall == 0);
        CHECK(phase.brain_slot == BONES_BRAIN_STAND);
        CHECK(!bones_is_crouch_slot(phase.brain_slot));
        CHECK(phase.sound_event_id == 0u);
    }
    /* C edge posts 0x10600 then 0x20600; last seen is 0x20600 after probes. */
    {
        saturn_pad cpad = {0};
        saturn_pad_update_host(&cpad, MB_PAD_C);
        runway_phase_step(&phase, &cpad);
        CHECK(phase.last_message == 0x00020600u || phase.last_message == 0x00010600u);
    }
    saturn_pad pad = {0};
    uint32_t start_x = phase.bones_x;
    unsigned anim0 = phase.anim_index;
    unsigned pan0 = phase.pan.index;
    saturn_pad_update_host(&pad, MB_PAD_RIGHT);
    for (int i = 0; i < 30; i++) runway_phase_step(&phase, &pad);
    CHECK(phase.bones_x != start_x || phase.state == RUNWAY_ST_STAND);
    CHECK(phase.facing == 1);
    /* §22: camera advance may seek pandcomp; collision tracks live pan map. */
    if (phase.pan.index != pan0) {
        CHECK(memcmp(&phase.collision, &phase.pan.collision,
                     sizeof phase.collision) == 0);
    }
    /* Walk uses morph bank; anim advances with screen X. */
    if (phase.bones_x != start_x && phase.state == RUNWAY_ST_WALK) {
        CHECK(phase.anim_bank == RUNWAY_ANIM_BANK_MORPH);
        CHECK(phase.anim_index != anim0 || phase.anim_count <= 1);
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
    /* §12o: Start toggles pause (PauseBrain); win = goal X → EndLevel. */
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
    /* Walk to goal (EvalSuccess / EndLevel win). */
    saturn_pad_update_host(&pad, MB_PAD_RIGHT);
    for (int i = 0; i < 5000 && !runway_phase_done(&phase); i++)
        runway_phase_step(&phase, &pad);
    CHECK(phase.state == RUNWAY_ST_FINISHED);
    CHECK(phase.skel.skeletism > 0);
    runway_phase_free(&phase);
    cd_close(&cd);
    return 0;
}
