#ifndef TIRED_OBSERVED_FILE_H
#define TIRED_OBSERVED_FILE_H
#include "tired/file_fingerprint.h"
typedef struct
{
    TiredText resolved_path, bytes;
    TiredFileFingerprint fingerprint;
} TiredObservedFile;
/* Read a manager-reported configuration path for display, never mutation. Caller
 * binds it to a successful query from the selected verified manager. Resolves
 * <=32 trusted directory aliases component by component; every directory must
 * belong to root/current scope owner and forbid group/other writes. Links must
 * belong to root/scope owner. The final file is never followed if it is a link.
 * Stable single-link regular-file snapshot, <=4 MiB, with a shared read budget.
 * Missing final file is known absence; parent/read/trust failures retain errors.
 * Output owns sensitive bytes and starts zeroed; failures preserve it. Resolution
 * is an observation, not an atomic namespace snapshot or write authorization. */
bool tired_observed_file_read(const char *path, bool user_scope, size_t *remaining,
                              TiredObservedFile *output, TiredError *error);
void tired_observed_file_destroy(TiredObservedFile *file);
#endif
