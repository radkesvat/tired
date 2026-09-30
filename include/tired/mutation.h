#ifndef TIRED_MUTATION_H
#define TIRED_MUTATION_H
#include "tired/backend.h"
#include "tired/directory.h"
#include "tired/plan.h"
#include "tired/service_record.h"
#include "tired/transaction_record.h"
typedef struct
{
    TiredTransactionOperation operation;
    uid_t actor_uid;
    TiredServiceRecord proposed;
    TiredText previous_name, expected_unit_sha256, expected_record_sha256;
    bool now, defer, restore_drift, keep_history, recovery, finish;
    bool root_previously_selected; /* Internal observed fact; never accepted from IPC. */
    uint64_t observation_usec, history_revisions;
} TiredMutation;
typedef struct
{
    TiredStatus status;
    const char *outcome;
    bool installed, rolled_back, recovery_required, deferred, user_scope, drift_backup,
        leaves_children;
    TiredText unit_name, unit_path, run_as;
    char service_uuid[37], transaction_uuid[37];
    TiredRuntime runtime;
    TiredError error, original_error;
} TiredOperationResult;
/* Owning outputs start zeroed. Protocol schema 1 is bounded to 1 MiB. A parsed
 * request is data only; the controller independently validates current authority,
 * derived destinations, field combinations, risks, ownership and stale evidence. */
void tired_mutation_destroy(TiredMutation *mutation);
/* A changed high-risk choice requires a new acknowledgment for its new value. */
void tired_mutation_change(TiredMutation *mutation, TiredFieldId field);
void tired_operation_result_destroy(TiredOperationResult *result);
bool tired_mutation_from_plan(const TiredPlan *plan, const TiredLayout *layout,
                              const TiredSettings *settings, const TiredTextList *risks,
                              TiredMutation *mutation, TiredError *error);
bool tired_mutation_encode(const TiredMutation *mutation, TiredText *output, TiredError *error);
bool tired_mutation_parse(const char *bytes, size_t length, TiredMutation *output,
                          TiredError *error);
bool tired_mutation_digest(const TiredMutation *mutation, char digest[65], TiredError *error);
bool tired_digest_bytes(const char *bytes, size_t length, char digest[65], TiredError *error);
/* Read-only full validation. The same call runs before approval and under lock.
 * Existing mutations require exact expected record/unit digests. */
/* Revalidate current workload context before admitting a new runtime action.
 * Does not inspect transaction inventory or assert a prior file revision. */
bool tired_mutation_workload_check(const TiredMutation *mutation, const TiredLayout *layout,
                                   const TiredBackend *backend, TiredRiskReport *risks,
                                   TiredError *error);
bool tired_mutation_validate(const TiredMutation *mutation, const TiredLayout *layout,
                             const TiredBackend *backend, TiredRiskReport *risks,
                             TiredError *error);
/* Explicitly authorized operation, internal injectable backend/layout. The
 * privileged executable never accepts a layout or backend from IPC/environment.
 * A factual failed result is returned through result; false means preflight failed.
 * Every reached external step has durable intent and completion/uncertainty. */
/* Under the scope lock only: adjust a headless automatic create name without
 * changing captured executable, identity or review facts. */
bool tired_mutation_final_name(TiredMutation *mutation, const TiredLayout *layout,
                               const TiredBackend *backend, TiredError *error);
bool tired_mutation_apply(const TiredMutation *mutation, const TiredLayout *layout,
                          const TiredBackend *backend, TiredOperationResult *result,
                          TiredError *error);
bool tired_mutation_recover(const TiredLayout *layout, const TiredBackend *backend,
                            const char *transaction_uuid, bool finish, TiredOperationResult *result,
                            TiredError *error);
bool tired_operation_output(const TiredOperationResult *result, const char *command, bool json,
                            TiredText *output, TiredError *error);
/* Create trusted directory components, never follow links or repair permissions.
 * Internal API; roots must already derive from a trusted layout. */
bool tired_directory_ensure(const char *path, bool private_final, TiredDirectory **directory,
                            TiredError *error);
#endif
