#ifndef TIRED_BACKUP_LEDGER_H
#define TIRED_BACKUP_LEDGER_H
#include "tired/file_fingerprint.h"
bool tired_backup_ledger_read(TiredDirectory *transaction, size_t index, bool *found, char uuid[37],
                              TiredFileFingerprint *before, TiredError *error);
bool tired_backup_ledger_clear(TiredDirectory *transaction, TiredDirectory *artifacts,
                               TiredError *error);
#endif
