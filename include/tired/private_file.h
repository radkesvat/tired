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
/* Write and sync an unnamed 0600 inode, check writer close, then link it
 * exclusively and sync the directory. Never overwrite an existing entry.
 * Requires O_TMPFILE and procfs descriptor linking. Failure after linking can
 * leave complete bytes with unknown directory durability; never unlink blindly.
 * Current-record replacements still require the checked publication API. */
bool tired_private_file_create(TiredDirectory *directory, const char *name, const char *data,
                               size_t length, TiredError *error);
#endif
