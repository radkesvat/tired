#define _DEFAULT_SOURCE
#include "tired/memory.h"
#include <assert.h>
#include <string.h>

void tired_memory_clear(void *data, size_t length)
{
    assert(data != NULL || length == 0);
    if (length != 0)
        explicit_bzero(data, length);
}
