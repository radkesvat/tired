#ifndef TIRED_SERVICE_METADATA_H
#define TIRED_SERVICE_METADATA_H
#include "tired/value.h"
#include <sys/types.h>
typedef struct
{
    char service_uuid[37], revision_uuid[37], transaction_uuid[37], unit_sha256[65];
    TiredText unit_name, writer_version;
    uint64_t created_usec, updated_usec;
    uid_t owner_uid, invoking_uid, service_uid;
    gid_t invoking_gid, service_gid;
    bool user_scope, interactive;
} TiredServiceMetadata;
/* Strict identity/revision portion of a private service record, schema 1.
 * System ownership is UID 0; user ownership/invoker/service UID must agree.
 * Descriptive persisted evidence only, never authentication or current NSS state.
 * All strings are owned after parsing. Outputs start zeroed; failure is atomic. */
bool tired_service_metadata_validate(const TiredServiceMetadata *metadata, TiredError *error);
bool tired_service_metadata_encode(const TiredServiceMetadata *metadata, TiredText *output,
                                   TiredError *error);
bool tired_service_metadata_parse(const char *data, size_t length, TiredServiceMetadata *output,
                                  TiredError *error);
void tired_service_metadata_destroy(TiredServiceMetadata *metadata);
#endif
