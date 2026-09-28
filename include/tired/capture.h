#ifndef TIRED_CAPTURE_H
#define TIRED_CAPTURE_H

#include "tired/value.h"
#include <sys/types.h>

typedef struct
{
    TiredTextList argv; /* Original spelling and argument boundaries. */
    TiredText directory;
    TiredText executable;      /* Absolute lexical execution path, not canonicalized. */
    TiredText resolved_target; /* Current canonical target, diagnostic only. */
    uid_t uid;
    gid_t gid;
    dev_t device;
    ino_t inode;
} TiredInvocation;

/* Validate persistent UTF-8 text; path mode additionally rejects C0/DEL controls.
 * Arguments may contain controls but never NUL or malformed UTF-8. */
bool tired_validate_text(const char *data, size_t length, bool path, TiredError *error);
/* Capture before elevation. No environment is persisted. path is the invoking
 * PATH, or NULL when absent (bare commands then fail). All inputs are borrowed;
 * result owns copies, and is unchanged on failure. Initialize result to zero.
 * This is passive inspection, not authorization or commit-time validation. */
bool tired_invocation_capture(const TiredTextList *arguments, const char *path,
                              TiredInvocation *result, TiredError *error);
void tired_invocation_destroy(TiredInvocation *invocation);

#endif
