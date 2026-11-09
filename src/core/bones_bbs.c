#include "bones_bbs.h"
#include <string.h>

void bones_bbs_init(bones_bbs *bbs) {
    if (!bbs) return;
    memset(bbs, 0, sizeof *bbs);
    bbs->head = -1;
    bbs->tail = -1;
    bbs->cursor = -1;
    for (unsigned i = 0; i < BONES_BBS_CAP; i++) {
        bbs->nodes[i].next = -1;
        bbs->nodes[i].prev = -1;
    }
}

static void unlink_index(bones_bbs *bbs, int idx) {
    bones_bbs_node *n = &bbs->nodes[idx];
    if (n->prev < 0)
        bbs->head = n->next;
    else
        bbs->nodes[n->prev].next = n->next;
    if (n->next < 0)
        bbs->tail = n->prev;
    else
        bbs->nodes[n->next].prev = n->prev;
    if (bbs->cursor == idx)
        bbs->cursor = n->next;
    n->id = 0;
    n->value = 0;
    n->alive = 0;
    n->next = -1;
    n->prev = -1;
    if (bbs->count) bbs->count--;
}

int bones_bbs_post(bones_bbs *bbs, uint32_t id, uint32_t value) {
    if (!bbs) return -1;
    /* Original scans pool for id==0; first free slot wins. */
    int free_i = -1;
    for (unsigned i = 0; i < BONES_BBS_CAP; i++) {
        if (!bbs->nodes[i].alive) {
            free_i = (int)i;
            break;
        }
    }
    if (free_i < 0) return -1;

    bones_bbs_node *n = &bbs->nodes[free_i];
    n->id = id;
    n->value = value;
    n->alive = 1;
    n->next = -1;
    n->prev = bbs->tail;
    if (bbs->tail >= 0)
        bbs->nodes[bbs->tail].next = free_i;
    else
        bbs->head = free_i;
    bbs->tail = free_i;
    bbs->count++;
    return 0;
}

/* Walk from head; match (id & mask)==value. Sets cursor past match like Match. */
static int find_match(bones_bbs *bbs, uint32_t mask, uint32_t value) {
    int i = bbs->head;
    while (i >= 0) {
        bones_bbs_node *n = &bbs->nodes[i];
        if (n->alive && ((n->id & mask) == value)) {
            bbs->cursor = n->next;
            return i;
        }
        i = n->next;
        bbs->cursor = i;
    }
    return -1;
}

int bones_bbs_peek(bones_bbs *bbs, uint32_t mask, uint32_t value, uint32_t *out_value) {
    if (!bbs) return -1;
    int i = find_match(bbs, mask, value);
    if (i < 0) return 0;
    if (out_value) *out_value = bbs->nodes[i].value;
    return 1;
}

int bones_bbs_consume(bones_bbs *bbs, uint32_t mask, uint32_t value, uint32_t *out_value) {
    if (!bbs) return -1;
    int i = find_match(bbs, mask, value);
    if (i < 0) return 0;
    if (out_value) *out_value = bbs->nodes[i].value;
    unlink_index(bbs, i);
    return 1;
}

int bones_bbs_probe(bones_bbs *bbs, uint32_t id) {
    return bones_bbs_consume(bbs, BONES_BBS_MASK_EXACT, id, NULL);
}
