#include "tired/identity.h"
#include "tired/capture.h"
#include <assert.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void tired_group_destroy(TiredGroup *group)
{
    if (group == NULL)
        return;
    tired_text_destroy(&group->name);
    *group = (TiredGroup){0};
}
void tired_account_destroy(TiredAccount *account)
{
    if (account == NULL)
        return;
    tired_text_destroy(&account->name);
    tired_text_destroy(&account->home);
    tired_group_destroy(&account->primary_group);
    *account = (TiredAccount){0};
}

static bool copy_record_text(TiredText *result, const char *text, TiredError *error)
{
    if (text == NULL)
        return tired_error_set(error, TIRED_INVALID, "account-record",
                               "Account database returned an incomplete record.", 0);
    size_t length = strnlen(text, TIRED_INPUT_LIMIT + 1);
    if (length > TIRED_INPUT_LIMIT)
        return tired_error_set(error, TIRED_INVALID, "account-limit",
                               "Account record exceeds its byte limit.", 0);
    return tired_validate_text(text, length, true, error) &&
           tired_text_set(result, text, length, TIRED_INPUT_LIMIT, error);
}

static bool group_lookup(const char *name, gid_t gid, TiredGroup *group, TiredError *error)
{
    for (size_t size = 1024; size <= TIRED_INPUT_LIMIT; size *= 2)
    {
        char *buffer = malloc(size);
        if (buffer == NULL)
            return tired_error_set(error, TIRED_INTERNAL, "allocation",
                                   "Cannot allocate group lookup buffer.", errno);
        struct group record, *found = NULL;
        int rc = name == NULL ? getgrgid_r(gid, &record, buffer, size, &found)
                              : getgrnam_r(name, &record, buffer, size, &found);
        if (rc == ERANGE)
        {
            free(buffer);
            continue;
        }
        if (rc != 0 || found == NULL)
        {
            free(buffer);
            return tired_error_set(error, rc == 0 ? TIRED_INVALID : TIRED_INTERNAL,
                                   rc == 0 ? "group-not-found" : "group-lookup",
                                   "Cannot resolve the selected group.", rc);
        }
        TiredGroup result = {.gid = record.gr_gid};
        bool ok = copy_record_text(&result.name, record.gr_name, error);
        free(buffer);
        if (!ok)
        {
            tired_group_destroy(&result);
            return false;
        }
        tired_group_destroy(group);
        *group = result;
        return true;
    }
    return tired_error_set(error, TIRED_INVALID, "account-limit",
                           "Group record exceeds its lookup limit.", 0);
}

static bool account_lookup(const char *name, uid_t uid, TiredAccount *account, TiredError *error)
{
    for (size_t size = 1024; size <= TIRED_INPUT_LIMIT; size *= 2)
    {
        char *buffer = malloc(size);
        if (buffer == NULL)
            return tired_error_set(error, TIRED_INTERNAL, "allocation",
                                   "Cannot allocate account lookup buffer.", errno);
        struct passwd record, *found = NULL;
        int rc = name == NULL ? getpwuid_r(uid, &record, buffer, size, &found)
                              : getpwnam_r(name, &record, buffer, size, &found);
        if (rc == ERANGE)
        {
            free(buffer);
            continue;
        }
        if (rc != 0 || found == NULL)
        {
            free(buffer);
            return tired_error_set(error, rc == 0 ? TIRED_INVALID : TIRED_INTERNAL,
                                   rc == 0 ? "account-not-found" : "account-lookup",
                                   "Cannot resolve the selected account.", rc);
        }
        TiredAccount result = {.uid = record.pw_uid};
        bool ok = copy_record_text(&result.name, record.pw_name, error) &&
                  copy_record_text(&result.home, record.pw_dir, error) &&
                  group_lookup(NULL, record.pw_gid, &result.primary_group, error);
        free(buffer);
        if (!ok)
        {
            tired_account_destroy(&result);
            return false;
        }
        tired_account_destroy(account);
        *account = result;
        return true;
    }
    return tired_error_set(error, TIRED_INVALID, "account-limit",
                           "Account record exceeds its lookup limit.", 0);
}

bool tired_account_by_uid(uid_t uid, TiredAccount *account, TiredError *error)
{
    assert(account != NULL);
    return account_lookup(NULL, uid, account, error);
}
bool tired_group_by_gid(gid_t gid, TiredGroup *group, TiredError *error)
{
    assert(group != NULL);
    return group_lookup(NULL, gid, group, error);
}

static bool selector_copy(const char *selector, size_t length, char *name, bool *numeric,
                          uint64_t *id, TiredError *error)
{
    if (length == 0 || length > 255 || !tired_validate_text(selector, length, true, error))
        return tired_error_set(error, TIRED_INVALID, "identity-selector",
                               "Expected a bounded account/group name or decimal ID.", 0);
    *numeric = true;
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char c = (unsigned char)selector[i];
        if (c < '0' || c > '9')
            *numeric = false;
        if (c == ' ' || c == ':' || c == '/' || c == '\\')
            return tired_error_set(error, TIRED_INVALID, "identity-selector",
                                   "Account/group selector contains an unsupported character.", 0);
    }
    if (*numeric && !tired_parse_u64(selector, length, 0, UINT32_MAX - 1, id, error))
        return false;
    memcpy(name, selector, length);
    name[length] = '\0';
    return true;
}

bool tired_account_resolve(const char *selector, size_t length, TiredAccount *account,
                           TiredError *error)
{
    assert(account != NULL && (selector != NULL || length == 0));
    char name[256];
    bool numeric;
    uint64_t id = 0;
    if (!selector_copy(selector, length, name, &numeric, &id, error))
        return false;
    return account_lookup(numeric ? NULL : name, (uid_t)id, account, error);
}
bool tired_group_resolve(const char *selector, size_t length, TiredGroup *group, TiredError *error)
{
    assert(group != NULL && (selector != NULL || length == 0));
    char name[256];
    bool numeric;
    uint64_t id = 0;
    if (!selector_copy(selector, length, name, &numeric, &id, error))
        return false;
    return group_lookup(numeric ? NULL : name, (gid_t)id, group, error);
}
bool tired_invoking_account(bool user_scope, TiredAccount *account, TiredError *error)
{
    const char *uid_hint = getenv("SUDO_UID"), *name_hint = getenv("SUDO_USER");
    if (user_scope || getuid() != 0 || (uid_hint == NULL && name_hint == NULL))
        return tired_account_by_uid(getuid(), account, error);
    TiredAccount original = {0};
    uint64_t uid;
    bool ok = uid_hint != NULL && name_hint != NULL &&
              tired_parse_u64(uid_hint, strlen(uid_hint), 0, UINT32_MAX - 1, &uid, error) &&
              tired_account_resolve(name_hint, strlen(name_hint), &original, error) &&
              original.uid == (uid_t)uid;
    if (ok)
    {
        tired_account_destroy(account);
        *account = original;
        original = (TiredAccount){0};
    }
    tired_account_destroy(&original);
    return ok || tired_error_set(error, TIRED_INVALID, "sudo-origin",
                                 "Sudo account hints do not match the account database.", 0);
}
