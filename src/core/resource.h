#ifndef MB_RESOURCE_H
#define MB_RESOURCE_H
#include "cdimage.h"

/* Caller owns *data on success, including for empty files. */
int resource_load(cdimage *cd, const char *path, void **data, size_t *size);
#endif
