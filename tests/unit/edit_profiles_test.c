#include "tired/frontend.h"
#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s [%s]\n", __FILE__, __LINE__, #expression,                   \
                    error.code == NULL ? "none" : error.code);                                     \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)

int main(int argc, char **argv)
{
    int result = 1;
    TiredMutation mutation = {0};
    TiredRequest request = {0};
    TiredSettings settings = {0};
    TiredBackend backend = {.features.systemd_version = 249};
    TiredError error = {0};
    tired_settings_defaults(&settings);
    CHECK(argc == 2);
    CHECK(tired_spec_defaults(&mutation.proposed.spec, &error));
    CHECK(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_NAME, "fixture", 7,
                         TIRED_ORIGIN_CAPTURE, true, &error));
    CHECK(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_EXECUTABLE, "/opt/frpc", 9,
                         TIRED_ORIGIN_CAPTURE, true, &error));
    CHECK(tired_spec_append(&mutation.proposed.spec, TIRED_FIELD_ARGV, "/opt/frpc", 9,
                            TIRED_ORIGIN_CAPTURE, &error));
    CHECK(tired_text_set(&mutation.proposed.executable.lexical_path, "/opt/frpc", 9, 4096, &error));
    CHECK(tired_text_set(&request.profile, "auto", 4, 80, &error));
    CHECK(tired_edit_inputs(&request, &mutation, &backend, &settings, argv[1], &error));
    CHECK(mutation.proposed.has_profile &&
          strcmp(mutation.proposed.profile.profile.id, "frpc") == 0);
    CHECK(tired_spec_choice_is(&mutation.proposed.spec, TIRED_FIELD_RESTART, "always"));
    CHECK(tired_text_list_append(&request.replacement, "/bin/true", 9, 256, 65536, &error));
    CHECK(tired_edit_inputs(&request, &mutation, &backend, &settings, argv[1], &error));
    CHECK(strcmp(mutation.proposed.spec.fields[TIRED_FIELD_EXECUTABLE].value.text.data,
                 "/bin/true") == 0);
    CHECK(mutation.proposed.has_profile &&
          strcmp(mutation.proposed.profile.profile.id, "generic") == 0);
    CHECK(tired_spec_choice_is(&mutation.proposed.spec, TIRED_FIELD_RESTART, "on-failure"));
    /* Explicit selection remains independent of executable basename matching. */
    CHECK(tired_text_set(&request.profile, "frps", 4, 80, &error));
    CHECK(tired_edit_inputs(&request, &mutation, &backend, &settings, argv[1], &error));
    CHECK(mutation.proposed.has_profile &&
          strcmp(mutation.proposed.profile.profile.id, "frps") == 0);
    CHECK(mutation.proposed.profile.explicit_selection);
    result = 0;
cleanup:
    tired_mutation_destroy(&mutation);
    tired_request_destroy(&request);
    tired_settings_destroy(&settings);
    return result;
}
