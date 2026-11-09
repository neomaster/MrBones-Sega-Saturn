#ifndef MB_SND1_H
#define MB_SND1_H
#include <stddef.h>
#include <stdint.h>

#define SND1_SPAN_COUNT 12
typedef struct { const uint8_t *data; size_t size; } snd1_span;

/* Parses the 12 offset/size entries observed in the two USA V1.000 discs. */
int snd1_parse(const uint8_t *data, size_t size, snd1_span out[SND1_SPAN_COUNT]);
#endif
