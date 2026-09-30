#ifndef TIRED_BACKEND_H
#define TIRED_BACKEND_H
#include "tired/layout.h"
#include "tired/manager_job.h"
#include "tired/profile_merge.h"
#include "tired/unit_query.h"
typedef struct
{
    bool found, loaded, active, enabled, running, failed, completed;
    TiredText active_state, sub_state, result, file_state, fragment;
    TiredTextList drop_ins;
    uint64_t restarts, pid;
    bool restarts_known, job_known;
    bool exit_known;
    int64_t exit_code, exit_status;
    uint32_t job_id;
} TiredRuntime;
void tired_runtime_destroy(TiredRuntime *runtime);
typedef bool (*TiredEffectSink)(void *, const char *, const TiredText *, TiredError *);
/* Internal injection seam. Production selects the native implementation only;
 * no CLI/environment setting selects a test backend or redirects system paths.
 * Mutations return uncertain=true when submission may have occurred without an
 * observed completion. Callers must journal intent before every mutation. */
typedef struct
{
    void *context;
    const TiredTextList *load_paths;
    TiredProfileContext features;
    bool linger_known, linger_enabled;
    bool capabilities_known;
    uint64_t permitted_capabilities, argument_max;
    bool nice_known;
    int minimum_nice;
    bool (*query)(void *, const char *, bool, TiredRuntime *, TiredError *);
    bool (*reload)(void *, bool *, TiredError *);
    bool (*enable)(void *, const char *, bool, bool *, TiredError *);
    bool (*job)(void *, const char *, TiredJobAction, bool *, TiredError *);
    bool (*reset_failed)(void *, const char *, bool *, TiredError *);
    bool (*verify)(void *, const TiredServiceSpec *, const TiredText *, TiredError *);
    bool (*linger)(void *, uid_t, bool *, TiredError *);
    void (*tick)(void *, const char *);
    void (*effects)(void *, TiredEffectSink, void *);
    bool (*execution_possible)(void *);
} TiredBackend;
typedef struct TiredNativeBackend TiredNativeBackend;
/* No manager or filesystem mutation; owner/version/load-path discovery only. */
bool tired_backend_open(const TiredLayout *layout, TiredNativeBackend **native,
                        TiredBackend *backend, TiredError *error);
void tired_backend_authorization(TiredNativeBackend *native, bool interactive);
void tired_backend_progress(TiredNativeBackend *native, bool enabled);
void tired_backend_destroy(TiredNativeBackend *native);
uint64_t tired_monotonic_usec(void);
#endif
