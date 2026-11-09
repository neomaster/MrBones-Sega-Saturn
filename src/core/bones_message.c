#include "bones_message.h"

int mb_message_dispatch(const mb_message_entry *entries, size_t count, void *context,
                        uint32_t message, void *payload, int *result) {
    if (!entries || !count || !result) return -1;
    *result = 0;
    for (size_t i = 0; i < count; ++i) {
        if (i && !entries[i].mask) return 0;
        if ((message & entries[i].mask) == entries[i].value) {
            if (!entries[i].handler) return -1;
            *result = entries[i].handler(context, message, payload);
            return 1;
        }
    }
    return -1;
}

int mb_bones_move_message(uint32_t position[3], const uint32_t delta[3], unsigned axes) {
    if (!position || !delta || !axes || (axes & ~7u)) return 0;
    for (unsigned i = 0; i < 3; ++i) if (axes & (1u << i)) position[i] += delta[i];
    return 1;
}


