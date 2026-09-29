#ifndef TIRED_EFFECTIVE_FILES_H
#define TIRED_EFFECTIVE_FILES_H
#include "tired/observed_file.h"
#include "tired/redaction.h"
#include "tired/service_record.h"
#include "tired/unit_query.h"
typedef struct
{
    TiredText reported_path, display;
    TiredObservedFile source;
    TiredRedaction redaction;
    TiredError error;
} TiredEffectiveFile;
typedef struct
{
    TiredEffectiveFile *files; /* Fragment at zero, then drop-ins in manager order. */
    size_t count;
    bool complete;
    TiredError error;
} TiredEffectiveFiles;
/* Collect only from a completed successful selected-manager query bound to the
 * record name. Query/path/file failures remain explicit and preserve valid
 * neighbors. <=257 files, 16 MiB shared reads, <=4 MiB each. Private source bytes
 * and display strings are owned. Sensitive display requires frontend export
 * authorization. No directive merging, mutation or loaded-byte equality claim. */
bool tired_effective_files_collect(const TiredServiceRecord *record,
                                   const TiredUnitQueryResult *query, bool include_sensitive,
                                   TiredEffectiveFiles *output, TiredError *error);
void tired_effective_files_destroy(TiredEffectiveFiles *files);
#endif
