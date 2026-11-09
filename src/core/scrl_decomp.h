#ifndef MB_SCRL_DECOMP_H
#define MB_SCRL_DECOMP_H
#include <stddef.h>
#include <stdint.h>

/*
 * Tile-stream decompressor (compress.o _DecompByte, docs/ghidra-vdp2-tiles.txt §3.3).
 *
 * Stream: a control byte, consumed MSB first, then one item per bit.
 *   bit 0  literal: the next source byte is copied to the output.
 *   bit 1  match: the next two source bytes form a BE16 code
 *            nibble = code & 0xF   0 ends the stream (the terminator);
 *                                  1..15 copies nibble + 1 bytes (2..16)
 *            dist   = code >> 4    1..4095 bytes back from the current output position
 *          The copy runs forward one byte at a time, so a distance shorter than the
 *          length repeats the pattern (dist 1 is a run).
 * After the last bit of a control byte the next source byte is a control byte again.
 *
 * Hardening over a plain port: every read and write is bounded by src_cap / dst_cap, and a
 * match that the format does not allow is reported instead of silently read through.
 * Such a match (distance 0, or reaching before the start of the output) is still decoded
 * the way scrl_decomp_byte always did, so callers that ignore `info` see no change:
 * distance 0 counts as 1 and bytes before the start of the output read as 0.
 */

typedef struct {
    size_t src_used;      /* source bytes consumed, including the terminator's two */
    unsigned terminated;  /* 1 when the stream ended on its terminator, 0 when the output
                             filled up first or the source ran out */
    unsigned bad_matches; /* matches with distance 0 or distance > bytes written so far */
} scrl_decomp_info;

/*
 * Decode src[0..src_cap) into dst[0..dst_cap). Returns the bytes written (never more than
 * dst_cap). Decoding stops at the terminator, when dst is full, or when src runs out. `info`
 * may be NULL. NULL buffers or a zero capacity give 0 and a zeroed `info`.
 */
size_t scrl_decomp_byte_ex(const uint8_t *src, size_t src_cap, uint8_t *dst, size_t dst_cap,
                           scrl_decomp_info *info);

#endif
