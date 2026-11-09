#ifndef MB_SATURN_PAD_H
#define MB_SATURN_PAD_H
#include <stdint.h>

typedef struct { uint16_t held, released, pressed, previous; } saturn_pad;
enum {
 MB_PAD_LEFT=1u<<0, MB_PAD_RIGHT=1u<<1, MB_PAD_UP=1u<<2, MB_PAD_DOWN=1u<<3,
 MB_PAD_A=1u<<4, MB_PAD_B=1u<<5, MB_PAD_C=1u<<6, MB_PAD_START=1u<<7,
 MB_PAD_X=1u<<8, MB_PAD_Y=1u<<9, MB_PAD_Z=1u<<10, MB_PAD_L=1u<<11, MB_PAD_R=1u<<12
};
/* Adaptation of loader 0x0020bf08. Raw digital-pad bytes are active-low. */
void saturn_pad_update(saturn_pad *pad, uint8_t raw_high, uint8_t raw_low);
/* Host supplies a bitset of physical Saturn-equivalent buttons held now. */
void saturn_pad_update_host(saturn_pad *pad, uint32_t buttons);
#endif
