#ifndef TIRED_FILE_FINGERPRINT_H
#define TIRED_FILE_FINGERPRINT_H
#include "tired/directory.h"
#include <sys/stat.h>
typedef struct
{
    bool exists;
    dev_t device;
    ino_t inode;
    uid_t uid;
    gid_t gid;
    mode_t mode; /* Permission/special bits; existing snapshots are regular files. */
    uint64_t size;
    char sha256[65];
} TiredFileFingerprint;
/* Observe one entry in a validated directory. Missing is known absence; links,
 * nonregular/multiply-linked files, access errors and observed changes fail.
 * Stream <=limit bytes (maximum 16 MiB); no secrets retained in the result.
 * Atomic output. Descriptive metadata only, not ownership/write authorization or
 * a reservation. Callers revalidate under the mutation lock before publication. */
bool tired_file_fingerprint(TiredDirectory *directory, const char *name, size_t limit,
                            TiredFileFingerprint *fingerprint, TiredError *error);
/* Compare absence or regular-file identity, metadata and content. Timestamps are
 * used for read stability, not equality; touching unchanged bytes is not drift. */
bool tired_file_fingerprint_equal(const TiredFileFingerprint *a, const TiredFileFingerprint *b);
#endif
