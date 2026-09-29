#include "tired/environment.h"
#include "tired/capture.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool name_valid(const char *name, size_t length)
{
    if (length == 0 || length > 255)
        return false;
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char c = (unsigned char)name[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')
            continue;
        if (i != 0 && c >= '0' && c <= '9')
            continue;
        return false;
    }
    return true;
}

bool tired_environment_name_sensitive(const char *name, size_t length)
{
    assert(name != NULL && length > 0 && length <= 255);
    char upper[256];
    for (size_t i = 0; i < length; ++i)
        upper[i] = name[i] >= 'a' && name[i] <= 'z' ? (char)(name[i] - 'a' + 'A') : name[i];
    upper[length] = '\0';
    const char *tokens[] = {"TOKEN",       "SECRET",     "PASSWORD",   "PASSWD", "API_KEY",
                            "PRIVATE_KEY", "CREDENTIAL", "ACCESS_KEY", "AUTH"};
    for (size_t i = 0; i < sizeof(tokens) / sizeof(tokens[0]); ++i)
        if (strstr(upper, tokens[i]) != NULL)
            return true;
    return false;
}

const TiredEnvironmentEntry *tired_environment_find(const TiredEnvironment *environment,
                                                    const char *name, size_t length)
{
    assert(environment != NULL && (name != NULL || length == 0));
    for (size_t i = 0; i < environment->count; ++i)
        if (environment->items[i].name.length == length &&
            memcmp(environment->items[i].name.data, name, length) == 0)
            return &environment->items[i];
    return NULL;
}

bool tired_environment_set(TiredEnvironment *environment, const char *assignment, size_t length,
                           TiredEnvironmentOrigin origin, bool sensitive, TiredError *error)
{
    assert(environment != NULL && (assignment != NULL || length == 0));
    if (length == 0 || length >= TIRED_INPUT_LIMIT || origin < TIRED_ENV_DEFAULT ||
        origin > TIRED_ENV_EDITED)
        return tired_error_set(error, TIRED_INVALID, "environment-input",
                               "Environment assignment exceeds its limit or has an invalid origin.",
                               0);
    const char *equal = memchr(assignment, '=', length);
    if (equal == NULL || !name_valid(assignment, (size_t)(equal - assignment)))
        return tired_error_set(error, TIRED_INVALID, "environment-name",
                               "Expected an ASCII environment name followed by '='.", 0);
    size_t name_length = (size_t)(equal - assignment);
    size_t value_length = length - name_length - 1;
    if (!tired_validate_text(equal + 1, value_length, false, error))
        return false;
    const TiredEnvironmentEntry *previous =
        tired_environment_find(environment, assignment, name_length);
    if (previous != NULL && previous->origin > origin)
    {
        tired_error_clear(error);
        return true;
    }
    size_t old_bytes = previous == NULL ? 0 : previous->name.length + previous->value.length + 2;
    size_t new_bytes = length + 1;
    if (new_bytes > TIRED_INPUT_LIMIT - (environment->bytes - old_bytes) ||
        (previous == NULL && environment->count >= TIRED_ENVIRONMENT_COUNT_LIMIT))
        return tired_error_set(error, TIRED_INVALID, "environment-limit",
                               "Environment exceeds its count or byte limit.", 0);
    TiredEnvironmentEntry item = {
        .origin = origin,
        .sensitive = sensitive || tired_environment_name_sensitive(assignment, name_length) ||
                     (previous != NULL && previous->sensitive)};
    if (!tired_text_set(&item.name, assignment, name_length, 255, error) ||
        !tired_text_set(&item.value, equal + 1, value_length, TIRED_INPUT_LIMIT, error))
    {
        tired_text_destroy(&item.name);
        tired_text_destroy(&item.value);
        return false;
    }
    if (previous != NULL)
    {
        size_t index = (size_t)(previous - environment->items);
        tired_text_destroy(&environment->items[index].name);
        tired_text_destroy(&environment->items[index].value);
        environment->items[index] = item;
    }
    else
    {
        TiredEnvironmentEntry *items =
            realloc(environment->items, (environment->count + 1) * sizeof(*items));
        if (items == NULL)
        {
            int saved_errno = errno;
            tired_text_destroy(&item.name);
            tired_text_destroy(&item.value);
            return tired_error_set(error, TIRED_INTERNAL, "allocation",
                                   "Cannot grow environment assignments.", saved_errno);
        }
        environment->items = items;
        environment->items[environment->count++] = item;
    }
    environment->bytes = environment->bytes - old_bytes + new_bytes;
    tired_error_clear(error);
    return true;
}

bool tired_environment_pass(TiredEnvironment *environment, const char *name, size_t length,
                            TiredError *error)
{
    assert(environment != NULL && (name != NULL || length == 0));
    if (!name_valid(name, length))
        return tired_error_set(error, TIRED_INVALID, "environment-name",
                               "Invalid exported variable name.", 0);
    char key[256];
    memcpy(key, name, length);
    key[length] = '\0';
    const char *value = getenv(key);
    if (value == NULL)
        return tired_error_set(error, TIRED_INVALID, "environment-missing",
                               "The requested exported variable is absent.", 0);
    size_t value_length = strnlen(value, TIRED_INPUT_LIMIT);
    if (value_length >= TIRED_INPUT_LIMIT - length - 1)
        return tired_error_set(error, TIRED_INVALID, "environment-limit",
                               "Exported value exceeds the input limit.", 0);
    size_t size = length + 1 + value_length;
    char *assignment = malloc(size + 1);
    if (assignment == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot capture exported variable.", errno);
    memcpy(assignment, name, length);
    assignment[length] = '=';
    memcpy(assignment + length + 1, value, value_length + 1);
    bool ok = tired_environment_set(environment, assignment, size, TIRED_ENV_PASSED, false, error);
    free(assignment);
    return ok;
}

const char *tired_environment_display(const TiredEnvironmentEntry *entry)
{
    assert(entry != NULL);
    return entry->sensitive ? "[redacted]" : entry->value.data;
}

void tired_environment_destroy(TiredEnvironment *environment)
{
    if (environment == NULL)
        return;
    for (size_t i = 0; i < environment->count; ++i)
    {
        tired_text_destroy(&environment->items[i].name);
        tired_text_destroy(&environment->items[i].value);
    }
    free(environment->items);
    *environment = (TiredEnvironment){0};
}

bool tired_credentials_add(TiredCredentials *credentials, const char *assignment, size_t length,
                           TiredError *error)
{
    assert(credentials != NULL && (assignment != NULL || length == 0));
    if (length == 0 || length >= TIRED_INPUT_LIMIT || credentials->count >= 256 ||
        length + 1 > TIRED_INPUT_LIMIT - credentials->bytes)
        return tired_error_set(error, TIRED_INVALID, "credential-limit",
                               "Credential references exceed their input limit.", 0);
    const char *equal = memchr(assignment, '=', length);
    if (equal == NULL)
        goto invalid;
    size_t name_length = (size_t)(equal - assignment);
    size_t path_length = length - name_length - 1;
    if (name_length == 0 || name_length > 255 || path_length == 0 || equal[1] != '/')
        goto invalid;
    for (size_t i = 0; i < name_length; ++i)
    {
        char c = assignment[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            goto invalid;
    }
    if (!tired_validate_text(equal + 1, path_length, true, error))
        return false;
    for (size_t i = 0; i < credentials->count; ++i)
        if (credentials->items[i].name.length == name_length &&
            memcmp(credentials->items[i].name.data, assignment, name_length) == 0)
            return tired_error_set(error, TIRED_INVALID, "credential-duplicate",
                                   "Credential name was assigned more than once.", 0);
    TiredCredential item = {0};
    if (!tired_text_set(&item.name, assignment, name_length, 255, error) ||
        !tired_text_set(&item.path, equal + 1, path_length, TIRED_INPUT_LIMIT, error))
    {
        tired_text_destroy(&item.name);
        tired_text_destroy(&item.path);
        return false;
    }
    TiredCredential *items = realloc(credentials->items, (credentials->count + 1) * sizeof(*items));
    if (items == NULL)
    {
        int saved_errno = errno;
        tired_text_destroy(&item.name);
        tired_text_destroy(&item.path);
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot grow credential references.", saved_errno);
    }
    credentials->items = items;
    credentials->items[credentials->count++] = item;
    credentials->bytes += length + 1;
    tired_error_clear(error);
    return true;
invalid:
    return tired_error_set(error, TIRED_INVALID, "credential-reference",
                           "Expected a credential name and absolute source path.", 0);
}

void tired_credentials_destroy(TiredCredentials *credentials)
{
    if (credentials == NULL)
        return;
    for (size_t i = 0; i < credentials->count; ++i)
    {
        tired_text_destroy(&credentials->items[i].name);
        tired_text_destroy(&credentials->items[i].path);
    }
    free(credentials->items);
    *credentials = (TiredCredentials){0};
}
