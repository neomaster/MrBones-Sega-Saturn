#include "scrl_decomp.h"

size_t scrl_decomp_byte_ex(const uint8_t *src, size_t src_cap, uint8_t *dst, size_t dst_cap,
                           scrl_decomp_info *info) {
    scrl_decomp_info local;
    scrl_decomp_info *in = info ? info : &local;
    size_t sp = 0, dp = 0;
    in->src_used = 0;
    in->terminated = 0;
    in->bad_matches = 0;
    if (!src || !dst || src_cap == 0 || dst_cap == 0) return 0;
    while (sp < src_cap && dp < dst_cap) {
        const uint8_t ctrl = src[sp++];
        int bit;
        for (bit = 7; bit >= 0 && dp < dst_cap; bit--) {
            if ((ctrl & (unsigned)(1u << bit)) == 0) {
                if (sp >= src_cap) goto done;
                dst[dp++] = src[sp++];
            } else {
                unsigned code, nibble, dist, count, k;
                if (src_cap - sp < 2u) goto done;
                code = ((unsigned)src[sp] << 8) | src[sp + 1u];
                sp += 2u;
                nibble = code & 0x0Fu;
                if (nibble == 0u) {
                    in->terminated = 1;
                    goto done;
                }
                dist = code >> 4;
                count = nibble + 1u;
                if (dist == 0u || dist > dp) in->bad_matches++;
                if (dist == 0u) dist = 1u;
                for (k = 0; k < count && dp < dst_cap; k++) {
                    dst[dp] = (dp >= dist) ? dst[dp - dist] : 0u;
                    dp++;
                }
            }
        }
    }
done:
    in->src_used = sp;
    return dp;
}
