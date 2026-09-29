#ifndef TIRED_EXECUTABLE_EVIDENCE_H
#define TIRED_EXECUTABLE_EVIDENCE_H
#include "tired/capture.h"
typedef struct
{
    TiredText lexical_path, resolved_path;
    dev_t device;
    ino_t inode;
} TiredExecutableEvidence;
/* Historical identity only; decoding never resolves paths, reads or executes the
 * file, and proves no current accessibility or privileged-code trust. Callers
 * re-inspect before mutation. Owned outputs start zeroed and fail atomically. */
bool tired_executable_evidence_capture(const TiredInvocation *invocation,
                                       TiredExecutableEvidence *output, TiredError *error);
bool tired_executable_evidence_encode(const TiredExecutableEvidence *evidence, TiredText *output,
                                      TiredError *error);
bool tired_executable_evidence_parse(const char *data, size_t length,
                                     TiredExecutableEvidence *output, TiredError *error);
void tired_executable_evidence_destroy(TiredExecutableEvidence *evidence);
#endif
