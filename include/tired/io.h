#ifndef TIRED_IO_H
#define TIRED_IO_H
#include "tired/value.h"

/* User-selected input: symlinks may resolve to regular files. FIFOs/devices are
 * rejected without blocking on open. Enforce limit before and during reads.
 * Failure preserves the previous owned output. No privileged trust is inferred. */
bool tired_read_file(const char *path, size_t limit, TiredText *output, TiredError *error);
/* Borrow an already-open regular descriptor at its current offset; never closes it. */
bool tired_read_fd(int fd, size_t limit, TiredText *output, TiredError *error);
/* Create a new private output; refuses existing files/symlinks. Never overwrites.
 * A write/fsync/close failure may leave a partial private file at the requested
 * path. Do not unlink by pathname after failure, as it might have been replaced. */
bool tired_write_private_new(const char *path, const char *data, size_t length, TiredError *error);
/* Preserve lexical path meaning, including symlink-sensitive .. components. */
bool tired_path_absolute(const TiredText *directory, const char *path, size_t length,
                         TiredText *output, TiredError *error);
/* Cryptographic kernel randomness, UUIDv4, canonical 36-byte lowercase text. */
bool tired_uuid_create(char output[37], TiredError *error);
/* Validate an exact lowercase UUIDv4 byte view; no terminator is required. */
bool tired_uuid_valid(const char *data, size_t length);
#endif
