#include "tired/config_frontend.h"
#include "tired/encode.h"
#include "tired/identity.h"
#include "tired/io.h"
#include "tired/settings.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool tired_config_discover(const TiredRequest *request, TiredSettings *settings, TiredError *error)
{
    assert(request != NULL && settings != NULL);
    if (getuid() != geteuid() || getgid() != getegid())
        return tired_error_set(
            error, TIRED_AUTHORIZATION, "config-identity",
            "Settings discovery requires matching real and effective identities.", 0);
    TiredSettings loaded = {0};
    TiredAccount account = {0};
    TiredText path = {0};
    TiredBuffer buffer;
    tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
    bool ok = false;
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg != NULL && xdg[0] != '\0')
    {
        size_t length = strnlen(xdg, TIRED_INPUT_LIMIT + 1);
        if (length > TIRED_INPUT_LIMIT || xdg[0] != '/')
        {
            tired_error_set(error, TIRED_INVALID, "xdg-path",
                            "User configuration directory must be a bounded absolute path.", 0);
            goto done;
        }
        /* A directory may have a trailing slash. The storage reader still rejects
         * dot components and repeated interior separators. */
        while (length > 1 && xdg[length - 1] == '/')
            --length;
        if (length > 1 && !tired_buffer_append(&buffer, xdg, length, error))
            goto done;
    }
    else
    {
        if (!tired_account_by_uid(getuid(), &account, error))
            goto done;
        if (account.home.length == 0 || account.home.data[0] != '/')
        {
            tired_error_set(error, TIRED_INVALID, "config-home",
                            "Account home directory must be absolute for settings discovery.", 0);
            goto done;
        }
        size_t length = account.home.length;
        while (length > 0 && account.home.data[length - 1] == '/')
            --length;
        if (!tired_buffer_append(&buffer, account.home.data, length, error) ||
            !tired_buffer_append(&buffer, "/.config", 8, error))
            goto done;
    }
    if (!tired_buffer_append(&buffer, "/tired/config.json", sizeof("/tired/config.json") - 1,
                             error) ||
        !tired_buffer_take(&buffer, &path, error) ||
        !tired_settings_load("/etc/tired/config.json", path.data, getuid(), &loaded, error))
        goto done;
    if (request->color.data != NULL)
    {
        loaded.color = strcmp(request->color.data, "always") == 0  ? 1
                       : strcmp(request->color.data, "never") == 0 ? 2
                                                                   : 0;
        loaded.origins[TIRED_SETTING_COLOR] = TIRED_SETTINGS_CLI;
        loaded.supplied[TIRED_SETTING_COLOR] = true;
    }
    if (request->no_tui)
    {
        loaded.tui = false;
        loaded.origins[TIRED_SETTING_TUI] = TIRED_SETTINGS_CLI;
        loaded.supplied[TIRED_SETTING_TUI] = true;
    }
    tired_settings_destroy(settings);
    *settings = loaded;
    loaded = (TiredSettings){0};
    tired_error_clear(error);
    ok = true;
done:
    tired_settings_destroy(&loaded);
    tired_account_destroy(&account);
    tired_text_destroy(&path);
    tired_buffer_destroy(&buffer);
    return ok;
}

bool tired_config_command(const TiredRequest *request, TiredText *output, TiredError *error)
{
    assert(request != NULL && output != NULL);
    if (request->arguments.count == 0)
        return tired_error_set(error, TIRED_INVALID, "config-command",
                               "Expected config show or config validate FILE.", 0);
    const char *operation = request->arguments.items[0].data;
    if (strcmp(operation, "show") == 0)
    {
        if (request->arguments.count != 1)
            return tired_error_set(error, TIRED_INVALID, "config-arguments",
                                   "Expected config show without extra operands.", 0);
        TiredSettings settings = {0};
        bool ok = tired_config_discover(request, &settings, error) &&
                  tired_settings_output(&settings, request->json, output, error);
        tired_settings_destroy(&settings);
        return ok;
    }
    if (strcmp(operation, "validate") != 0 || request->arguments.count != 2)
        return tired_error_set(error, TIRED_INVALID, "config-arguments",
                               "Expected config validate FILE.", 0);

    TiredText contents = {0};
    TiredSettings input = {0}, effective = {0};
    tired_settings_defaults(&effective);
    bool user = tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user");
    bool ok =
        tired_read_file(request->arguments.items[1].data, TIRED_INPUT_LIMIT, &contents, error) &&
        tired_settings_parse(contents.data, contents.length, &input, error) &&
        tired_settings_merge(&effective, &input, user ? TIRED_SETTINGS_USER : TIRED_SETTINGS_ADMIN,
                             error);
    if (ok)
    {
        const char *message =
            request->json ? (user ? "{\"schema_version\":1,\"command\":\"config "
                                    "validate\",\"ok\":true,\"exit_code\":0,\"scope\":\"user\","
                                    "\"base\":\"built_in_defaults\",\"trust_checked\":false}\n"
                                  : "{\"schema_version\":1,\"command\":\"config "
                                    "validate\",\"ok\":true,\"exit_code\":0,\"scope\":\"system\","
                                    "\"base\":\"built_in_defaults\",\"trust_checked\":false}\n")
                          : "Valid settings over built-in defaults. File and profile-directory "
                            "trust was not checked.\n";
        ok = tired_text_set(output, message, strlen(message), TIRED_INPUT_LIMIT, error);
    }
    tired_text_destroy(&contents);
    tired_settings_destroy(&input);
    tired_settings_destroy(&effective);
    if (ok)
        tired_error_clear(error);
    return ok;
}
