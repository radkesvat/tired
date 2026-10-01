#ifndef TIRED_EMBEDDED_H
#define TIRED_EMBEDDED_H
#include <stddef.h>

typedef struct
{
    const char *name;
    const unsigned char *data;
    size_t length;
    const char *sha256;
} TiredEmbeddedFile;

extern const TiredEmbeddedFile tired_embedded_profiles[];
extern const size_t tired_embedded_profiles_count;
extern const TiredEmbeddedFile tired_embedded_helper[];
extern const size_t tired_embedded_helper_count;
#endif
