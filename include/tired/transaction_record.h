#ifndef TIRED_TRANSACTION_RECORD_H
#define TIRED_TRANSACTION_RECORD_H
#include "tired/value.h"
#define TIRED_TRANSACTION_RECORD_LIMIT 4096U
#define TIRED_TRANSACTION_SEQUENCE_LIMIT 4096U
typedef enum
{
    TIRED_TRANSACTION_CREATE,
    TIRED_TRANSACTION_EDIT,
    TIRED_TRANSACTION_REMOVE,
    TIRED_TRANSACTION_RENAME,
    TIRED_TRANSACTION_RESTORE,
    TIRED_TRANSACTION_START,
    TIRED_TRANSACTION_STOP,
    TIRED_TRANSACTION_RESTART,
    TIRED_TRANSACTION_ENABLE,
    TIRED_TRANSACTION_DISABLE,
    TIRED_TRANSACTION_OPERATION_COUNT
} TiredTransactionOperation;
typedef enum
{
    TIRED_ACTION_PREPARE,
    TIRED_ACTION_PUBLISH_FILES,
    TIRED_ACTION_RELOAD,
    TIRED_ACTION_ENABLE,
    TIRED_ACTION_DISABLE,
    TIRED_ACTION_START,
    TIRED_ACTION_STOP,
    TIRED_ACTION_RESTART,
    TIRED_ACTION_OBSERVE,
    TIRED_ACTION_STORE_RECORD,
    TIRED_ACTION_REMOVE_FILES,
    TIRED_ACTION_ROLLBACK,
    TIRED_ACTION_COMMIT,
    TIRED_ACTION_COUNT
} TiredTransactionAction;
typedef enum
{
    TIRED_ACTION_INTENT,
    TIRED_ACTION_COMPLETED,
    TIRED_ACTION_FAILED,
    TIRED_ACTION_UNCERTAIN,
    TIRED_ACTION_STATE_COUNT
} TiredTransactionActionState;
typedef struct
{
    char transaction_uuid[37], service_uuid[37], approved_sha256[65];
    TiredText unit_name;
    bool user_scope;
    TiredTransactionOperation operation;
    TiredTransactionAction action;
    TiredTransactionActionState state;
    uint64_t sequence;
} TiredTransactionRecord;
/* Strict schema 1 progress envelope. UUIDs are canonical lowercase UUIDv4,
 * digest is lowercase SHA-256 hex, unit_name is a full safe .service name.
 * Descriptive evidence only: parsing neither authorizes actions nor proves their
 * outcomes. Controller validates transitions, manifests and live observations.
 * Owned outputs start zeroed and are replaced only on success. */
bool tired_transaction_record_parse(const char *data, size_t length, TiredTransactionRecord *record,
                                    TiredError *error);
bool tired_transaction_record_encode(const TiredTransactionRecord *record, TiredText *output,
                                     TiredError *error);
void tired_transaction_record_destroy(TiredTransactionRecord *record);
#endif
