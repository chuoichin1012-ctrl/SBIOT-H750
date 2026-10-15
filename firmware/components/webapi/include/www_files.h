// Static UI files embedded by tools/embed_web.py (gzip-encoded), in QSPI.
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *path;
    const uint8_t *start, *end;
    const char *type;
    const char *etag;  // quoted hash of the content
} www_file_t;

extern const www_file_t www_files[];
extern const size_t www_file_count;
