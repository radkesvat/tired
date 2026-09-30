#include "tired/layout.h"
#include "tired/capture.h"
#include "tired/encode.h"
#include "tired/identity.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool normalized(const char *input, TiredText *output, TiredError *error)
{
    size_t length = input == NULL ? 0 : strnlen(input, 4097);
    if (length == 0 || length > 4096 || input[0] != '/' ||
        !tired_validate_text(input, length, true, error))
        return tired_error_set(error, TIRED_INVALID, "layout-path",
                               "Storage locations must be bounded absolute paths.", 0);
    while (length > 1 && input[length - 1] == '/')
        --length;
    for (size_t start = 1; start < length;)
    {
        size_t end = start;
        while (end < length && input[end] != '/')
            ++end;
        size_t count = end - start;
        if (count == 0 || count > 255 || (count == 1 && input[start] == '.') ||
            (count == 2 && input[start] == '.' && input[start + 1] == '.'))
            return tired_error_set(error, TIRED_INVALID, "layout-components",
                                   "Storage path components must be normalized.", 0);
        start = end + 1;
    }
    return tired_text_set(output, input, length, 4096, error);
}
static bool append(const TiredText *root, const char *suffix, TiredText *output, TiredError *error)
{
    TiredBuffer buffer;
    tired_buffer_init(&buffer, 4096);
    size_t length = root->length == 1 && root->data[0] == '/' ? 0 : root->length;
    bool ok = tired_buffer_append(&buffer, root->data, length, error) &&
              tired_buffer_append(&buffer, suffix, strlen(suffix), error) &&
              tired_buffer_take(&buffer, output, error);
    tired_buffer_destroy(&buffer);
    return ok;
}
static bool home_default(const char *configured, const char *home, const char *suffix,
                         TiredText *output, TiredError *error)
{
    if (configured != NULL && configured[0] != '\0')
        return normalized(configured, output, error);
    TiredText root = {0};
    bool ok = normalized(home, &root, error) && append(&root, suffix, output, error);
    tired_text_destroy(&root);
    return ok;
}
void tired_layout_destroy(TiredLayout *layout)
{
    if (layout == NULL)
        return;
    for (size_t i = 0; i < TIRED_PATH_COUNT; ++i)
        tired_text_destroy(&layout->paths[i]);
    *layout = (TiredLayout){0};
}
bool tired_layout_resolve(bool user_scope, const char *home, const char *config, const char *state,
                          const char *runtime, TiredLayout *output, TiredError *error)
{
    assert(output != NULL);
    TiredLayout layout = {.user_scope = user_scope};
    TiredText config_root = {0}, state_root = {0}, runtime_root = {0};
    bool ok = false;
    if (!user_scope)
    {
        static const char *const paths[TIRED_PATH_COUNT] = {
            "/etc/tired/config.json",      "/etc/tired/profiles.d",    "/etc/tired/services",
            "/etc/systemd/system",         "/var/lib/tired/services",  "/var/lib/tired/history",
            "/var/lib/tired/transactions", "/run/tired/operation.lock"};
        for (size_t i = 0; i < TIRED_PATH_COUNT; ++i)
            if (!tired_text_set(&layout.paths[i], paths[i], strlen(paths[i]), 4096, error))
                goto done;
    }
    else
    {
        if (runtime == NULL || runtime[0] == '\0')
        {
            tired_error_set(error, TIRED_NOT_FOUND, "layout-runtime",
                            "User scope requires XDG_RUNTIME_DIR from an existing user session.",
                            0);
            goto done;
        }
        if (!home_default(config, home, "/.config", &config_root, error) ||
            !home_default(state, home, "/.local/state", &state_root, error) ||
            !normalized(runtime, &runtime_root, error) ||
            !append(&config_root, "/tired/config.json", &layout.paths[TIRED_PATH_CONFIG], error) ||
            !append(&config_root, "/tired/profiles.d", &layout.paths[TIRED_PATH_PROFILES], error) ||
            !append(&config_root, "/tired/services", &layout.paths[TIRED_PATH_ENVIRONMENT_SERVICES],
                    error) ||
            !append(&config_root, "/systemd/user", &layout.paths[TIRED_PATH_UNITS], error) ||
            !append(&state_root, "/tired/services", &layout.paths[TIRED_PATH_RECORDS], error) ||
            !append(&state_root, "/tired/history", &layout.paths[TIRED_PATH_HISTORY], error) ||
            !append(&state_root, "/tired/transactions", &layout.paths[TIRED_PATH_TRANSACTIONS],
                    error) ||
            !append(&runtime_root, "/tired/operation.lock",
                    &layout.paths[TIRED_PATH_OPERATION_LOCK], error))
            goto done;
    }
    tired_layout_destroy(output);
    *output = layout;
    layout = (TiredLayout){0};
    tired_error_clear(error);
    ok = true;
done:
    tired_layout_destroy(&layout);
    tired_text_destroy(&config_root);
    tired_text_destroy(&state_root);
    tired_text_destroy(&runtime_root);
    return ok;
}
bool tired_layout_discover(bool user_scope, TiredLayout *layout, TiredError *error)
{
    assert(layout != NULL);
    if (!user_scope)
        return tired_layout_resolve(false, NULL, NULL, NULL, NULL, layout, error);
    if (getuid() != geteuid() || getgid() != getegid())
        return tired_error_set(
            error, TIRED_AUTHORIZATION, "layout-identity",
            "User storage discovery requires matching real and effective identities.", 0);
    TiredAccount account = {0};
    const char *config = getenv("XDG_CONFIG_HOME"), *state = getenv("XDG_STATE_HOME");
    if ((config == NULL || config[0] == '\0' || state == NULL || state[0] == '\0') &&
        !tired_account_by_uid(getuid(), &account, error))
        return false;
    bool ok = tired_layout_resolve(true, account.home.data, config, state,
                                   getenv("XDG_RUNTIME_DIR"), layout, error);
    tired_account_destroy(&account);
    return ok;
}
bool tired_layout_discover_profiles(bool user_scope, TiredLayout *output, TiredError *error)
{
    if (!user_scope)
        return tired_layout_discover(false, output, error);
    if (getuid() != geteuid() || getgid() != getegid())
        return tired_error_set(error, TIRED_AUTHORIZATION, "layout-identity",
                               "Profile discovery requires matching process identities.", 0);
    TiredAccount account = {0};
    TiredText root = {0};
    TiredLayout layout = {.user_scope = true};
    const char *config = getenv("XDG_CONFIG_HOME");
    bool ok =
        (config != NULL && config[0] != '\0') || tired_account_by_uid(getuid(), &account, error);
    ok = ok && home_default(config, account.home.data, "/.config", &root, error) &&
         append(&root, "/tired/config.json", &layout.paths[TIRED_PATH_CONFIG], error) &&
         append(&root, "/tired/profiles.d", &layout.paths[TIRED_PATH_PROFILES], error);
    if (ok)
    {
        tired_layout_destroy(output);
        *output = layout;
        layout = (TiredLayout){0};
    }
    tired_layout_destroy(&layout);
    tired_account_destroy(&account);
    tired_text_destroy(&root);
    return ok;
}
bool tired_layout_check_unit_path(const TiredLayout *layout, const TiredTextList *load_paths,
                                  TiredError *error)
{
    assert(layout != NULL && load_paths != NULL);
    if (!layout->user_scope)
    {
        tired_error_clear(error);
        return true;
    }
    const TiredText *units = &layout->paths[TIRED_PATH_UNITS];
    if (units->data != NULL)
        for (size_t i = 0; i < load_paths->count; ++i)
        {
            const TiredText *path = &load_paths->items[i];
            size_t length = path->length;
            while (length > 1 && path->data[length - 1] == '/')
                --length;
            if (length == units->length && memcmp(path->data, units->data, length) == 0)
            {
                tired_error_clear(error);
                return true;
            }
        }
    return tired_error_set(error, TIRED_CONFLICT, "user-unit-path-mismatch",
                           "The user unit destination is absent from the manager load path.", 0);
}
