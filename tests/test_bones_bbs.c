#include "core/bones_bbs.h"
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    bones_bbs bbs;
    uint32_t val = 0;

    bones_bbs_init(&bbs);
    CHECK(bones_bbs_probe(&bbs, 0x00010700u) == 0);
    CHECK(bones_bbs_post(&bbs, 0x00010700u, 1) == 0);
    CHECK(bones_bbs_post(&bbs, 0x00020600u, 0x11) == 0);
    CHECK(bones_bbs_probe(&bbs, 0x00020800u) == 0);
    CHECK(bones_bbs_probe(&bbs, 0x00010700u) == 1);
    CHECK(bones_bbs_probe(&bbs, 0x00010700u) == 0); /* consumed */
    CHECK(bones_bbs_probe(&bbs, 0x00020600u) == 1);
    CHECK(bones_bbs_post(NULL, 1, 1) == -1);
    CHECK(bones_bbs_probe(NULL, 1) == -1);

    /* Fill then overflow. */
    bones_bbs_init(&bbs);
    for (unsigned i = 0; i < BONES_BBS_CAP; i++)
        CHECK(bones_bbs_post(&bbs, 0x1000u + i, i) == 0);
    CHECK(bones_bbs_post(&bbs, 0x9999u, 0) == -1);

    /* Peek leaves node; consume removes. Match (id & mask)==value. */
    bones_bbs_init(&bbs);
    CHECK(bones_bbs_post(&bbs, 0x00010700u, 7) == 0);
    CHECK(bones_bbs_post(&bbs, 0x00020600u, 0x11) == 0);
    CHECK(bones_bbs_peek(&bbs, BONES_BBS_MASK_EXACT, 0x00010700u, &val) == 1);
    CHECK(val == 7);
    CHECK(bbs.count == 2); /* still there */
    CHECK(bones_bbs_peek(&bbs, BONES_BBS_MASK_EXACT, 0x00010700u, &val) == 1);
    CHECK(bones_bbs_consume(&bbs, BONES_BBS_MASK_EXACT, 0x00010700u, &val) == 1);
    CHECK(val == 7);
    CHECK(bbs.count == 1);
    CHECK(bones_bbs_peek(&bbs, BONES_BBS_MASK_EXACT, 0x00010700u, NULL) == 0);

    /* Masked match: high byte family 0x0002xxxx with mask 0xffff0000 value 0x00020000. */
    bones_bbs_init(&bbs);
    CHECK(bones_bbs_post(&bbs, 0x00010700u, 1) == 0);
    CHECK(bones_bbs_post(&bbs, 0x00020600u, 2) == 0);
    CHECK(bones_bbs_peek(&bbs, 0xffff0000u, 0x00020000u, &val) == 1);
    CHECK(val == 2);
    CHECK(bones_bbs_consume(&bbs, 0xffff0000u, 0x00020000u, NULL) == 1);
    CHECK(bbs.count == 1);
    CHECK(bones_bbs_probe(&bbs, 0x00010700u) == 1);

    /* FIFO: first posted is first matched. */
    bones_bbs_init(&bbs);
    CHECK(bones_bbs_post(&bbs, 0x00020A00u, 10) == 0);
    CHECK(bones_bbs_post(&bbs, 0x00020A00u, 20) == 0);
    CHECK(bones_bbs_consume(&bbs, BONES_BBS_MASK_EXACT, 0x00020A00u, &val) == 1);
    CHECK(val == 10);
    CHECK(bones_bbs_consume(&bbs, BONES_BBS_MASK_EXACT, 0x00020A00u, &val) == 1);
    CHECK(val == 20);

    /* Reuse free slot after consume (pool scan like 0x20b264). */
    bones_bbs_init(&bbs);
    for (unsigned i = 0; i < BONES_BBS_CAP; i++)
        CHECK(bones_bbs_post(&bbs, 0x2000u + i, i) == 0);
    CHECK(bones_bbs_post(&bbs, 0x9999u, 0) == -1);
    CHECK(bones_bbs_consume(&bbs, BONES_BBS_MASK_EXACT, 0x2000u, NULL) == 1);
    CHECK(bones_bbs_post(&bbs, 0x9999u, 99) == 0);
    CHECK(bones_bbs_peek(&bbs, BONES_BBS_MASK_EXACT, 0x9999u, &val) == 1);
    CHECK(val == 99);

    return 0;
}
