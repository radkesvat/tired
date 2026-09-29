#ifndef TIRED_PRIVATE_FILE_H
#define TIRED_PRIVATE_FILE_H
#include "tired/directory.h"
#define TIRED_PRIVATE_FILE_LIMIT (16U * 1024U * 1024U)
/* Read a single regular owner file with mode 0600 and exactly one link, from an
 * owner 0700 directory. No links followed. Check metadata/binding before and after
 * reading; failure preserves output. Binary contents are allowed; length matters.
 * Caller chooses a limit <=16 MiB and validates the application format separately. */
bool tired_private_file_read(TiredDirectory *directory, const char *name, size_t limit,
                             TiredText *output, TiredError *error);
/* Exclusively create one 0600 file, write all bytes, sync file, check binding,
 * close, and sync directory. Never overwrite or truncate an existing entry.
 * Creation is visible before completion: use only for private staging/immutable
 * revisions under the operation lock, not publication of a current record.
 * Failure may leave an incomplete private file. Never unlink blindly on error. */
bool tired_private_file_create(TiredDirectory *directory, const char *name, const char *data,
                               size_t length, TiredError *error);
#endif
