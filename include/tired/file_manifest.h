#ifndef TIRED_FILE_MANIFEST_H
#define TIRED_FILE_MANIFEST_H
#include "tired/file_fingerprint.h"
#include "tired/file_target.h"
#include "tired/transaction_record.h"
#define TIRED_FILE_MANIFEST_LIMIT TIRED_INPUT_LIMIT
#define TIRED_FILE_CHANGE_LIMIT 256U
typedef struct
{
    TiredFileTarget target;
    TiredFileFingerprint before, after;
    char staging_uuid[37], rollback_uuid[37];
} TiredFileChange;
typedef struct
{
    TiredTransactionRecord prepared;
    TiredFileChange *files;
    size_t count;
} TiredFileManifest;
/* Strict file-phase plan, anchored to sequence-1 prepare/completed identity.
 * Parsed output owns entries/unit names. Source views may borrow data for encode.
 * Staging UUID identifies .tired-UUID.tmp in the resolved target directory;
 * rollback UUID identifies a private transaction artifact. No arbitrary paths.
 * No I/O or authorization: actual artifacts, approvals and live state must match.
 * Outputs start zeroed and remain unchanged on failure. */
bool tired_file_manifest_parse(const char *data, size_t length, TiredFileManifest *manifest,
                               TiredError *error);
bool tired_file_manifest_encode(const TiredFileManifest *manifest, TiredText *output,
                                TiredError *error);
bool tired_file_manifest_validate(const TiredFileManifest *manifest, TiredError *error);
void tired_file_manifest_destroy(TiredFileManifest *manifest);
#endif
