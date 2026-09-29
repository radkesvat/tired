#ifndef TIRED_VERIFY_H
#define TIRED_VERIFY_H
#include "tired/process.h"
#include "tired/stage.h"
typedef struct TiredVerification TiredVerification;
typedef enum
{
    TIRED_VERIFY_RUNNING,
    TIRED_VERIFY_CLEAN,
    TIRED_VERIFY_DIAGNOSTICS,
    TIRED_VERIFY_NONZERO,
    TIRED_VERIFY_INCOMPLETE
} TiredVerifyState;
typedef struct
{
    /* NULL context selects system scope. A user context must supply all four
     * absolute locations for the current identity; caller establishes their trust. */
    const char *home, *config_home, *data_home, *runtime_directory;
} TiredVerifyUserPaths;
typedef struct
{
    TiredVerifyState state;
    TiredProcessResult process;
    bool cleanup_complete;
    TiredError cleanup_error;
    const char *retained_directory;
} TiredVerifyResult;
/* Explicit verification only: stages bytes and starts fixed /usr/bin/systemd-analyze
 * after root ownership/ancestor checks. Disables generators, man checks and pager.
 * Failure may return a terminal handle for cleanup diagnostics. *verification is
 * initially NULL; inspect and destroy every returned handle. */
bool tired_verify_start(const TiredText *base, const char *unit, size_t length,
                        const TiredVerifyUserPaths *user, unsigned timeout_ms,
                        TiredVerification **verification, TiredError *error);
bool tired_verify_step(TiredVerification *verification);
void tired_verify_cancel(TiredVerification *verification);
TiredVerifyResult tired_verify_result(const TiredVerification *verification);
/* Retry failed cleanup only after terminal. Destroy releases resources, never
 * silently deletes replacements; report retained_directory before destroying. */
bool tired_verify_cleanup(TiredVerification *verification, TiredError *error);
void tired_verify_destroy(TiredVerification *verification);
#endif
