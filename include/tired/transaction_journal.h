#ifndef TIRED_TRANSACTION_JOURNAL_H
#define TIRED_TRANSACTION_JOURNAL_H
#include "tired/publication.h"
#include "tired/transaction_progress.h"
#include "tired/transaction_record.h"
typedef struct
{
    TiredTransactionRecord *records;
    size_t count, staging_count;
    TiredTransactionProgress progress;
} TiredTransactionJournal;
/* Read a dedicated private journal directory. Immutable 0001.json..4096.json
 * records must be contiguous and agree on transaction/service/unit/scope/operation/
 * approved digest. Recognized unpublished staging names are counted, not replayed.
 * Unknown entries, gaps, unsafe files, invalid progress transitions and observed
 * directory changes fail closed.
 * Atomic owned output. Read-only: no lock, creation, cleanup or recovery occurs. */
bool tired_transaction_journal_read(TiredDirectory *directory, TiredTransactionJournal *journal,
                                    TiredError *error);
void tired_transaction_journal_destroy(TiredTransactionJournal *journal);
/* Under the supplied scope lock, re-read history and publish exactly its next
 * record. Refuse unexplained staging. Enforce sequence, identity and intent/outcome
 * pairing, not operation-specific order or outcome evidence. Return publication handle for
 * durability retry/cleanup even on failure; caller must inspect/discard/destroy it.
 * Directory and lock must belong to the same scope; directory outlives handle. */
bool tired_transaction_journal_append(TiredDirectory *directory, const TiredOperationLock *lock,
                                      const TiredTransactionRecord *record,
                                      TiredPublication **publication, TiredError *error);
/* Atomic private metadata append: the inode remains unnamed until its complete
 * contents are synced. Process death cannot leave partial records or staging. */
bool tired_transaction_journal_append_atomic(TiredDirectory *directory,
                                             const TiredOperationLock *lock,
                                             const TiredTransactionRecord *record,
                                             TiredError *error);
#endif
