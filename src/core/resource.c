#include "resource.h"
#include <stdlib.h>

/* Native adaptation of the synchronous file-loading role at 0x002033e8. */
int resource_load(cdimage *cd, const char *path, void **data, size_t *size) {
    cd_iso_entry entry;
    void *buffer;
    if (!cd || !path || !data || !size) return -1;
    *data = NULL;
    *size = 0;
    if (cd_iso_lookup(cd, path, &entry) != 0 || entry.is_dir) return -1;
    buffer = malloc(entry.size ? entry.size : 1);
    if (!buffer) return -1;
    if (cd_iso_read(cd, &entry, 0, buffer, entry.size) != entry.size) {
        free(buffer);
        return -1;
    }
    *data = buffer;
    *size = entry.size;
    return 0;
}
