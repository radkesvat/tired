#ifndef TIRED_IDENTITY_H
#define TIRED_IDENTITY_H
#include "tired/value.h"
#include <sys/types.h>

typedef struct
{
    gid_t gid;
    TiredText name;
} TiredGroup;
typedef struct
{
    uid_t uid;
    TiredText name;
    TiredText home;
    TiredGroup primary_group;
} TiredAccount;

/* Resolve through the system account database with bounded reentrant buffers.
 * Selectors are account names or decimal IDs. No environment hints, identity
 * changes, authorization, or shell execution occur. Outputs own their strings,
 * start zeroed, and remain unchanged on failure. */
bool tired_account_by_uid(uid_t uid, TiredAccount *account, TiredError *error);
bool tired_account_resolve(const char *selector, size_t length, TiredAccount *account,
                           TiredError *error);
bool tired_group_by_gid(gid_t gid, TiredGroup *group, TiredError *error);
bool tired_group_resolve(const char *selector, size_t length, TiredGroup *group, TiredError *error);
void tired_account_destroy(TiredAccount *account);
void tired_group_destroy(TiredGroup *group);
#endif
