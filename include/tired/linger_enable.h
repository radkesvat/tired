#ifndef TIRED_LINGER_ENABLE_H
#define TIRED_LINGER_ENABLE_H
#include "tired/linger_query.h"
typedef struct TiredLingerEnable TiredLingerEnable;
typedef struct
{
    bool done, submitted, acknowledged, observed, enabled;
    uid_t uid;
    TiredError error;
} TiredLingerEnableResult;
/* Explicitly enable account lingering, then independently query UID/Linger.
 * Caller records approved account-level intent and obtains required authority
 * before entry. interactive_authorization must itself be explicitly allowed by
 * the caller; false never requests interactive authorization. No bus activation.
 * Ready LOGIN identity must outlive operation. One deadline covers method/auth
 * wait and verification. This API cannot disable lingering, including on failure.
 * Timeout/cancellation after submission leaves the mutation outcome uncertain;
 * acknowledgement alone is not success. Does not imply account-setting ownership,
 * workload health, boot availability, or installation of any service. */
bool tired_linger_enable_start(TiredManagerIdentity *identity, uid_t uid,
                               bool interactive_authorization, unsigned timeout_ms,
                               TiredLingerEnable **operation, TiredError *error);
bool tired_linger_enable_step(TiredLingerEnable *operation);
bool tired_linger_enable_poll(TiredLingerEnable *operation, struct pollfd *descriptor,
                              uint64_t *deadline_usec, TiredError *error);
void tired_linger_enable_cancel(TiredLingerEnable *operation);
TiredLingerEnableResult tired_linger_enable_result(const TiredLingerEnable *operation);
void tired_linger_enable_destroy(TiredLingerEnable *operation);
#endif
