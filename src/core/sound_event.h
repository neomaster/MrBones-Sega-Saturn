#ifndef MB_SOUND_EVENT_H
#define MB_SOUND_EVENT_H
#include <stddef.h>
#include <stdint.h>

typedef struct { const uint8_t *data; size_t size, offset; int done; } mb_sound_event;
typedef struct { uint32_t words[8]; } mb_sound_event_record;
/* Component 0 of 05SND1; 1=event exists, 0=empty, -1=invalid. */
int mb_sound_event_open(mb_sound_event *event, const void *data, size_t size, uint32_t index);
/* 1=record, 0=end, -1=truncated; end bit is in words[0]. */
int mb_sound_event_next(mb_sound_event *event, mb_sound_event_record *record);
#endif
