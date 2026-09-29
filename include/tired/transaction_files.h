#ifndef TIRED_TRANSACTION_FILES_H
#define TIRED_TRANSACTION_FILES_H
#include "tired/directory.h"
#include "tired/file_change.h"
typedef struct
{
    TiredFilePhaseResult phase;
    TiredError file_error;
    bool intent_durable, files_completed, outcome_durable;
} TiredTransactionFilesResult;
/* Bind private files.json and journal/ to a trusted approved preparation anchor,
 * require matching forward/rollback mode and held scope lock, then durably record
 * file-phase intent before applying changes. Matching pending intent resumes;
 * success records completed, any file failure records uncertain and stops.
 * No mutation begins if intent cannot be made durable. Journal write failure may
 * leave staging for explicit recovery. No cleanup, manager actions or automatic
 * rollback. Caller authorizes request, verifies all preconditions/backups and
 * chooses the operation's phase order. Result reports both file and journal state. */
bool tired_transaction_files_apply(const TiredLayout *layout, TiredDirectory *transaction,
                                   const TiredTransactionRecord *approved, bool rollback,
                                   const TiredOperationLock *lock,
                                   TiredTransactionFilesResult *result, TiredError *error);
#endif
