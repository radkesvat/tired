#ifndef TIRED_HELPER_H
#define TIRED_HELPER_H
#include "tired/mutation.h"
#include <signal.h>
/* Fixed administrative executable; authentication never consumes protocol stdin.
 * The helper owns an approved transaction after the complete request arrives. */
bool tired_helper_call(const TiredMutation *mutation, bool interactive, TiredText *output,
                       TiredStatus *status, TiredError *error);
bool tired_helper_recover_call(bool user_scope, const char *uuid, bool finish, bool interactive,
                               TiredText *output, TiredStatus *status, TiredError *error);
/* Internal worker dispatch after a complete frame; scope is independently fixed
 * by the authorized administrative executable or current-user worker. */
bool tired_helper_dispatch(const TiredText *request, bool user_scope, bool interactive,
                           uid_t actor_uid, TiredText *output, TiredError *error);
bool tired_helper_linger_call(uid_t uid, bool interactive, bool *uncertain, TiredError *error);
bool tired_helper_record(const char *name, bool hydrate, uid_t actor, TiredText *output,
                         TiredError *error);
bool tired_helper_record_call(const char *name, bool hydrate, bool interactive,
                              TiredMutation *mutation, TiredError *error);
bool tired_helper_error_output(const TiredError *failure, TiredText *output, TiredError *error);
/* Bounded length-prefixed protocol. Reads/writes handle EINTR and short I/O.
 * timeout covers a complete frame; no caller-provided destination path. */
bool tired_protocol_read(int fd, unsigned timeout_ms, TiredText *output, TiredError *error);
bool tired_protocol_write(int fd, const TiredText *bytes, unsigned timeout_ms, TiredError *error);
bool tired_protocol_read_interruptible(int fd, unsigned timeout_ms,
                                       const volatile sig_atomic_t *cancel, TiredText *output,
                                       TiredError *error);
bool tired_protocol_write_interruptible(int fd, const TiredText *bytes, unsigned timeout_ms,
                                        const volatile sig_atomic_t *cancel, TiredError *error);
bool tired_protocol_ready(int fd, TiredError *error);
#endif
