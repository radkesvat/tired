#include "tired/config_frontend.h"
#include "tired/io.h"
#include "tired/settings.h"
#include <assert.h>
#include <string.h>

bool tired_config_command(const TiredRequest *request, TiredText *output, TiredError *error)
{
    assert(request != NULL && output != NULL);
    if (request->arguments.count == 0)
        return tired_error_set(error, TIRED_INVALID, "config-command",
                               "Expected config show or config validate FILE.", 0);
    const char *operation = request->arguments.items[0].data;
    if (strcmp(operation, "show") == 0)
        return tired_error_set(
            error, TIRED_UNSUPPORTED, "implementation-incomplete",
            "Effective settings discovery and config show are not yet implemented.", 0);
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
