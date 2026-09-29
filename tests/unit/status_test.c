#include "tired/io.h"
#include "tired/json.h"
#include "tired/list_frontend.h"
#include "tired/status_frontend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
int main(void)
{
    int result = 1;
    TiredText output = {0};
    TiredError error = {0};
    TiredStatus status = TIRED_INTERNAL;
    TiredServiceRecord record = {0};
    TiredUnitObservation observation = {0};
    TiredStatusView view = {.unit_name = "relay.service",
                            .user_scope = true,
                            .record = &record,
                            .files = {.unit.state = TIRED_SERVICE_FILE_MATCH,
                                      .environment.state = TIRED_SERVICE_FILE_NOT_REQUIRED},
                            .live = {.attempted = true,
                                     .completed_realtime_usec = 123,
                                     .query = {.done = true,
                                               .file_found = true,
                                               .object_found = true,
                                               .file_state = "enabled",
                                               .observation = &observation}}};
    struct json_object *document = NULL, *value = NULL, *live = NULL, *properties = NULL;
    TiredRequest request = {0};
    char fixture[] = "status-XXXXXX";
    char *created = NULL, *cwd = NULL;
    TiredText path = {0};
    memcpy(record.metadata.service_uuid, "01234567-89ab-4cde-8fab-0123456789ab", 37);
    record.unit_path = (TiredText){.data = "/units/relay.service", .length = 20};
    observation.fields[TIRED_OBS_ACTIVE_STATE] =
        (TiredObservedValue){.known = true, .value.text = {.data = "failed", .length = 6}};
    observation.fields[TIRED_OBS_FRAGMENT_PATH] =
        (TiredObservedValue){.known = true, .value.text = record.unit_path};
    observation.fields[TIRED_OBS_MAIN_PID] = (TiredObservedValue){.known = true};
    CHECK(tired_status_output(&view, true, false, &output, &status, &error));
    CHECK(status == TIRED_OK);
    CHECK(tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &document, &error));
    CHECK(json_object_object_get_ex(document, "fragment", &value) &&
          strcmp(json_object_get_string(value), "match") == 0);
    CHECK(json_object_object_get_ex(document, "live", &live));
    CHECK(json_object_object_get_ex(live, "properties", &properties));
    CHECK(json_object_object_get_ex(properties, "MainPID", &value) &&
          json_object_get_int(value) == 0);
    CHECK(!json_object_object_get_ex(properties, "Result", &value));
    CHECK(!json_object_object_get_ex(properties, "DropInPaths", &value));
    CHECK(!json_object_object_get_ex(properties, "NeedDaemonReload", &value));
    CHECK(tired_status_output(&view, false, false, &output, &status, &error));
    CHECK(strstr(output.data, "NeedDaemonReload: unknown") != NULL);
    for (unsigned boolean = 0; boolean < 2; ++boolean)
    {
        observation.fields[TIRED_OBS_NEED_DAEMON_RELOAD] =
            (TiredObservedValue){.known = true, .value.boolean = boolean != 0};
        CHECK(tired_status_output(&view, true, false, &output, &status, &error));
        CHECK(status == TIRED_OK);
        CHECK(tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &document, &error));
        CHECK(json_object_object_get_ex(document, "live", &live) &&
              json_object_object_get_ex(live, "properties", &properties) &&
              json_object_object_get_ex(properties, "NeedDaemonReload", &value));
        CHECK(json_object_get_type(value) == json_type_boolean &&
              json_object_get_boolean(value) == (boolean != 0));
        CHECK(tired_status_output(&view, false, false, &output, &status, &error));
        CHECK(strstr(output.data, boolean ? "NeedDaemonReload: yes" : "NeedDaemonReload: no") !=
              NULL);
    }
    CHECK(!json_object_object_get_ex(document, "spec", &value));
    TiredText dropins[] = {
        {.data = "/etc/systemd/system/relay.service.d/custom.conf", .length = 47}};
    observation.fields[TIRED_OBS_DROP_IN_PATHS] =
        (TiredObservedValue){.known = true, .value.list = {.items = dropins, .count = 1}};
    dropins[0].length = strlen(dropins[0].data);
    CHECK(tired_status_output(&view, true, false, &output, &status, &error));
    CHECK(tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &document, &error));
    CHECK(json_object_object_get_ex(document, "live", &live) &&
          json_object_object_get_ex(live, "properties", &properties) &&
          json_object_object_get_ex(properties, "DropInPaths", &value) &&
          json_object_array_length(value) == 1);
    CHECK(tired_status_output(&view, false, false, &output, &status, &error));
    CHECK(strstr(output.data, "DropInPaths: /etc/systemd/system/relay.service.d/custom.conf") !=
          NULL);
    observation.fields[TIRED_OBS_DROP_IN_PATHS].value.list.count = 0;
    CHECK(tired_status_output(&view, false, false, &output, &status, &error));
    CHECK(strstr(output.data, "DropInPaths: none") != NULL);
    CHECK(tired_status_output(&view, true, false, &output, &status, &error));
    CHECK(tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &document, &error));
    CHECK(json_object_object_get_ex(document, "live", &live) &&
          json_object_object_get_ex(live, "properties", &properties) &&
          json_object_object_get_ex(properties, "DropInPaths", &value) &&
          json_object_array_length(value) == 0);
    CHECK(tired_status_output(&view, true, true, &output, &status, &error));
    CHECK(status == TIRED_RUNTIME_FAILED && strstr(output.data, "\"exit_code\":7") != NULL);
    observation.fields[TIRED_OBS_ACTIVE_STATE].value.text =
        (TiredText){.data = "active", .length = 6};
    CHECK(tired_status_output(&view, true, true, &output, &status, &error));
    CHECK(status == TIRED_OK);
    observation.fields[TIRED_OBS_ACTIVE_STATE].known = false;
    CHECK(tired_status_output(&view, true, true, &output, &status, &error));
    CHECK(status == TIRED_RUNTIME_FAILED);
    observation.fields[TIRED_OBS_FRAGMENT_PATH].value.text =
        (TiredText){.data = "/foreign/\x1b[31m.service", .length = 21};
    CHECK(tired_status_output(&view, false, false, &output, &status, &error));
    CHECK(strstr(output.data, "Fragment agreement: different") != NULL &&
          strstr(output.data, "ActiveState: unknown") != NULL &&
          memchr(output.data, 27, output.length) == NULL);
    view.live.query.error =
        (TiredError){.status = TIRED_AUTHORIZATION, .code = "test-denied", .message = "Denied."};
    CHECK(tired_status_output(&view, true, true, &output, &status, &error));
    CHECK(status == TIRED_AUTHORIZATION && strstr(output.data, "MainPID") == NULL);
    CHECK(strstr(output.data, "\"fragment\":\"unknown\"") != NULL);
    view.live.query.error = (TiredError){0};
    view.live.query.observation = NULL;
    view.live.query.file_found = view.live.query.object_found = false;
    view.live.query.file_state = NULL;
    CHECK(tired_status_output(&view, true, false, &output, &status, &error));
    CHECK(status == TIRED_OK); /* A saved service may currently be missing. */
    view.record = NULL;
    view.record_error = (TiredError){.status = TIRED_NOT_FOUND, .code = "record-not-found"};
    CHECK(tired_status_output(&view, true, false, &output, &status, &error));
    CHECK(status == TIRED_NOT_FOUND);
    view.live.query.file_found = true;
    CHECK(tired_status_output(&view, true, false, &output, &status, &error));
    CHECK(status == TIRED_OK && strstr(output.data, "service_uuid") == NULL);
    view.record_error = (TiredError){.status = TIRED_CONFLICT, .code = "record-conflict"};
    view.transactions_error = (TiredError){.status = TIRED_RECOVERY_REQUIRED, .code = "tx-unknown"};
    CHECK(tired_status_output(&view, true, false, &output, &status, &error));
    CHECK(strstr(output.data, "record-conflict") != NULL &&
          strstr(output.data, "tx-unknown") != NULL);

    /* Production command uses an isolated user layout without a manager socket. */
    cwd = getcwd(NULL, 0);
    CHECK(cwd != NULL && (created = mkdtemp(fixture)) != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &path, &error));
    CHECK(setenv("XDG_STATE_HOME", path.data, 1) == 0 &&
          setenv("XDG_CONFIG_HOME", path.data, 1) == 0 &&
          setenv("XDG_RUNTIME_DIR", path.data, 1) == 0);
    const char *args[] = {"tired", "status", "relay.service", "--user", "--json"};
    CHECK(tired_cli_parse((int)(sizeof(args) / sizeof(args[0])), args, &request, &error));
    CHECK(tired_status_command(&request, &output, &status, &error));
    CHECK(status != TIRED_OK && strstr(output.data, "\"record\":\"missing\"") != NULL &&
          strstr(output.data, "\"status\":\"unknown\"") != NULL);
    CHECK(access(path.data, F_OK) == 0);
    const char *list_args[] = {"tired", "list", "--user", "--json"};
    CHECK(tired_cli_parse((int)(sizeof(list_args) / sizeof(list_args[0])), list_args, &request,
                          &error));
    CHECK(tired_list_command(&request, &output, &status, &error));
    CHECK(status == TIRED_OK && strstr(output.data, "\"command\":\"list\"") != NULL);
    CHECK(tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &document, &error));
    CHECK(json_object_object_get_ex(document, "services", &value) &&
          json_object_array_length(value) == 0);
    CHECK(rmdir(created) == 0); /* Read-only discovery did not create state beneath it. */
    created = NULL;
    result = 0;
cleanup:
    if (created != NULL)
        (void)rmdir(created);
    free(cwd);
    tired_text_destroy(&path);
    tired_text_destroy(&output);
    tired_request_destroy(&request);
    json_object_put(document);
    return result;
}
