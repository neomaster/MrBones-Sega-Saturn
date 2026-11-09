#ifndef MB_BONES_MESSAGE_H
#define MB_BONES_MESSAGE_H
#include <stddef.h>
#include <stdint.h>

typedef int (*mb_message_handler)(void *context, uint32_t message, void *payload);
typedef struct { uint32_t mask, value; mb_message_handler handler; } mb_message_entry;

/* 1=matched (result set), 0=no match (result=0), -1=invalid table. */
int mb_message_dispatch(const mb_message_entry *entries, size_t count, void *context,
                        uint32_t message, void *payload, int *result);
/* Axes 1=X, 2=Y, 4=Z; valid combinations reproduce seven RUNWAY move handlers. */
int mb_bones_move_message(uint32_t position[3], const uint32_t delta[3], unsigned axes);
#endif



