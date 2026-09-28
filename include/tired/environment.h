#ifndef TIRED_ENVIRONMENT_H
#define TIRED_ENVIRONMENT_H
#include "tired/value.h"

#define TIRED_ENVIRONMENT_COUNT_LIMIT 1024U
#define TIRED_ENVIRONMENT_FILE_LIMIT (2U * TIRED_INPUT_LIMIT)

typedef enum
{
    TIRED_ENV_DEFAULT,
    TIRED_ENV_PROFILE,
    TIRED_ENV_CONFIG,
    TIRED_ENV_IMPORTED,
    TIRED_ENV_PASSED,
    TIRED_ENV_EXPLICIT,
    TIRED_ENV_EDITED
} TiredEnvironmentOrigin;

typedef struct
{
    TiredText name;
    TiredText value;
    TiredEnvironmentOrigin origin;
    bool sensitive;
} TiredEnvironmentEntry;

typedef struct
{
    TiredEnvironmentEntry *items;
    size_t count;
    size_t bytes;
} TiredEnvironment;

/* All containers are zero-initialized, owned, and non-copyable by assignment.
 * Set copies input and preserves the container on failure. Higher origin wins;
 * equal-origin assignments use the last value. Lower-origin inputs are validated
 * but do not replace the value. No process environment is read by set. */
bool tired_environment_set(TiredEnvironment *environment, const char *assignment, size_t length,
                           TiredEnvironmentOrigin origin, bool sensitive, TiredError *error);
/* Only capture an explicitly named exported variable; missing values fail. */
bool tired_environment_pass(TiredEnvironment *environment, const char *name, size_t length,
                            TiredError *error);
const TiredEnvironmentEntry *tired_environment_find(const TiredEnvironment *environment,
                                                    const char *name, size_t length);
/* Default presentation accessor: returned text is borrowed. Never use this
 * accessor to serialize a private environment file. */
const char *tired_environment_display(const TiredEnvironmentEntry *entry);
void tired_environment_destroy(TiredEnvironment *environment);
/* Import a bounded in-memory file transactionally. Supported grammar is
 * documented in docs/environment.md; no shell evaluation or file I/O occurs. */
bool tired_environment_import(TiredEnvironment *environment, const char *data, size_t length,
                              TiredError *error);
/* Deterministic private-file contents, including sensitive values. Caller must
 * protect persistence and never send this directly to ordinary display output. */
bool tired_environment_encode(const TiredEnvironment *environment, TiredText *output,
                              TiredError *error);

typedef struct
{
    TiredText name;
    TiredText path;
} TiredCredential;
typedef struct
{
    TiredCredential *items;
    size_t count;
    size_t bytes;
} TiredCredentials;
/* NAME=/absolute/path only. Never reads credential contents or changes argv.
 * Duplicate credential names fail instead of order-dependent replacement. */
bool tired_credentials_add(TiredCredentials *credentials, const char *assignment, size_t length,
                           TiredError *error);
void tired_credentials_destroy(TiredCredentials *credentials);
#endif
