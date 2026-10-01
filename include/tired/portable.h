#ifndef TIRED_PORTABLE_H
#define TIRED_PORTABLE_H
#include "tired/directory.h"
#include "tired/embedded.h"

#define TIRED_HELPER_CACHE "/var/cache/tired/helpers"

/* Descriptor-relative, atomic helper materialization. The supplied directory is
 * already trusted; production callers require root ownership. No existing file
 * is replaced. The payload digest names an immutable version directory. */
bool tired_helper_cache_find(TiredDirectory *directory, const TiredEmbeddedFile *file, bool *found,
                             TiredError *error);
bool tired_helper_cache_install(TiredDirectory *directory, const TiredEmbeddedFile *file,
                                TiredError *error);
bool tired_portable_prepare_helper(bool interactive, TiredText *path, TiredError *error);
/* Internal installation entry point: real/effective root, fixed compiled bytes
 * and destination only. Never accepts a request, source path or destination. */
int tired_portable_setup(void);
#endif
