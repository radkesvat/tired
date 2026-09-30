#include "tired/ui.h"
#include <stdio.h>
#include <string.h>
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
    TiredError error = {0};
    TiredText output = {0};
    struct json_object *entry = NULL;
    const char *fixture =
        "{\"run_as\":\"relay\",\"service_uid\":1001,\"profile\":\"backhaul\",\"transactions\":"
        "\"pending\","
        "\"unit_file\":{\"state\":\"drifted\"},\"environment_file\":{\"state\":\"missing\"},"
        "\"live\":{\"file_state\":\"enabled\",\"properties\":{\"ActiveState\":\"active\","
        "\"SubState\":\"running\"}}}";
    CHECK(tired_json_parse(fixture, strlen(fixture), TIRED_INPUT_LIMIT, &entry, &error));
    CHECK(tired_ui_dashboard_details(entry, &output, &error));
    CHECK(strcmp(output.data, "runs as relay | profile backhaul\nactive/running | boot enabled\n"
                              "unit drifted | env missing | tx pending") == 0);
    json_object_object_del(entry, "run_as");
    CHECK(tired_ui_dashboard_details(entry, &output, &error));
    CHECK(strstr(output.data, "runs as UID 1001") != NULL);
    CHECK(json_object_object_add(entry, "transactions", json_object_new_string("unknown")) == 0);
    CHECK(tired_ui_dashboard_details(entry, &output, &error));
    CHECK(strstr(output.data, "tx unknown") != NULL);
    json_object_put(entry);
    entry = json_object_new_object();
    CHECK(entry != NULL && tired_ui_dashboard_details(entry, &output, &error));
    CHECK(strstr(output.data, "runs as unknown") != NULL &&
          strstr(output.data, "unknown/unknown | boot unknown") != NULL &&
          strstr(output.data, "unit unknown | env unknown | tx unknown") != NULL);
    result = 0;
cleanup:
    json_object_put(entry);
    tired_text_destroy(&output);
    return result;
}
