#ifndef TIRED_ENVIRONMENT_SNAPSHOT_H
#define TIRED_ENVIRONMENT_SNAPSHOT_H
#include "tired/environment.h"
#define TIRED_ENVIRONMENT_SNAPSHOT_LIMIT (8U * TIRED_INPUT_LIMIT)
/* Private schema-1 environment values/origins/sensitivity and credential paths.
 * Never reads credential files or process environment. No redaction: private
 * persistence only. Exact duplicate names and sensitivity downgrades are rejected.
 * Encode API-validated containers. Both owned parse outputs are atomic on failure. */
bool tired_environment_snapshot_encode(const TiredEnvironment *environment,
                                       const TiredCredentials *credentials, TiredText *output,
                                       TiredError *error);
bool tired_environment_snapshot_parse(const char *data, size_t length,
                                      TiredEnvironment *environment, TiredCredentials *credentials,
                                      TiredError *error);
#endif
