#ifndef TIRED_HISTORY_H
#define TIRED_HISTORY_H
#include "tired/mutation.h"
/* Called under the scope lock after commit. Preserves references held by the
 * current record, retained history and every incomplete transaction. Idempotent;
 * foreign files stop cleanup and remain intact. */
bool tired_history_finalize(const TiredLayout *layout, const TiredMutation *mutation,
                            TiredError *error);
bool tired_transaction_retire(const TiredLayout *layout, const TiredMutation *mutation,
                              const TiredBackend *backend, TiredError *error);
bool tired_transaction_cleanup_pending(TiredDirectory *directory, bool *pending, TiredError *error);
bool tired_transaction_cleanup_mark(TiredDirectory *directory, TiredError *error);
bool tired_transaction_cleanup_clear(TiredDirectory *directory, TiredError *error);
bool tired_cleanup_record(struct json_object *rows, const char *uuid, const char *area,
                          const char *name, TiredDirectory *directory, bool remove_directory,
                          TiredError *error);
bool tired_cleanup_save(TiredDirectory *directory, const TiredTransactionRecord *anchor,
                        struct json_object *rows, TiredError *error);
bool tired_transaction_cleanup_resume(const TiredLayout *layout, TiredDirectory *transaction,
                                      const TiredTransactionRecord *anchor,
                                      const TiredBackend *backend, TiredError *error);
#endif
