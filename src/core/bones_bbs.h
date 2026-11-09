#ifndef MB_BONES_BBS_H
#define MB_BONES_BBS_H
#include <stddef.h>
#include <stdint.h>

/*
 * Native reconstruction of loader message board (/02):
 *   post     @ 0x0020b264  — append free 0x20-byte node {id,value}
 *   peek     @ 0x0020b2e8  — match (id & mask)==value, do NOT unlink
 *   consume  @ 0x0020b308  — match then unlink (clear id)
 *   unlink   @ 0x0020b2a4  — unlink known node
 *   match    @ 0x0020b35c  — cursor walk with (id & mask)==value
 *
 * Original board: count, cursor, mask, value, head, tail, then inline pool.
 * Native keeps a fixed pool + index links (FIFO append at tail).
 * Evidence: docs/ghidra-bbs-match.txt, docs/ghidra-bbs-peek.txt.
 */

#define BONES_BBS_CAP 32
#define BONES_BBS_MASK_EXACT 0xffffffffu

typedef struct {
    uint32_t id;
    uint32_t value;
    int alive;
    int next; /* index, or -1 */
    int prev;
} bones_bbs_node;

typedef struct {
    bones_bbs_node nodes[BONES_BBS_CAP];
    unsigned count;
    int head; /* index of first live node, or -1 */
    int tail;
    int cursor; /* match cursor index, or -1 */
} bones_bbs;

void bones_bbs_init(bones_bbs *bbs);

/* Append; 0 ok, -1 full/invalid. Mirrors 0x20b264 free-slot scan + tail link. */
int bones_bbs_post(bones_bbs *bbs, uint32_t id, uint32_t value);

/*
 * Peek (0x20b2e8): find first (id & mask)==value from head; leave on board.
 * 1 found (optionally writes *out_value), 0 miss, -1 invalid.
 */
int bones_bbs_peek(bones_bbs *bbs, uint32_t mask, uint32_t value, uint32_t *out_value);

/*
 * Consume (0x20b308): peek-equivalent match then unlink.
 * 1 found+removed, 0 miss, -1 invalid.
 */
int bones_bbs_consume(bones_bbs *bbs, uint32_t mask, uint32_t value, uint32_t *out_value);

/*
 * Compatibility wrapper used by earlier phase code: exact-id consume
 * (mask=0xffffffff, value=id). Prefer peek/consume with explicit mask.
 */
int bones_bbs_probe(bones_bbs *bbs, uint32_t id);

#endif
