#ifndef TIRED_MEMORY_H
#define TIRED_MEMORY_H
#include <stddef.h>

/* Clear sensitive bytes even when the compiler can prove they are no longer used.
 * A NULL pointer is accepted only when length is zero. */
void tired_memory_clear(void *data, size_t length);
#endif
