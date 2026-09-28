#include "tired/model.h"
#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static bool set(TiredServiceSpec *spec, TiredFieldId id, const char *value, TiredError *error)
{
    return tired_spec_set(spec, id, value, strlen(value), TIRED_ORIGIN_USER, true, error);
}

int main(void)
{
    TiredServiceSpec spec = {0};
    TiredError error = {0};
    CHECK(tired_spec_defaults(&spec, &error));
    CHECK(tired_spec_validate_scalars(&spec, &error));
    CHECK(tired_spec_choice_is(&spec, TIRED_FIELD_TYPE, "exec"));
    CHECK(spec.fields[TIRED_FIELD_RESTART_SEC].value.microseconds == 5000000);
    CHECK(spec.fields[TIRED_FIELD_NO_NEW_PRIVILEGES].origin == TIRED_ORIGIN_INHERITED);
    CHECK(set(&spec, TIRED_FIELD_NO_NEW_PRIVILEGES, "false", &error));
    CHECK(spec.fields[TIRED_FIELD_NO_NEW_PRIVILEGES].origin == TIRED_ORIGIN_USER);
    CHECK(!spec.fields[TIRED_FIELD_NO_NEW_PRIVILEGES].value.boolean);
    CHECK(set(&spec, TIRED_FIELD_DESCRIPTION, "", &error));
    CHECK(spec.fields[TIRED_FIELD_DESCRIPTION].value.text.data != NULL);
    CHECK(
        !tired_spec_set(&spec, TIRED_FIELD_DESCRIPTION, "x", 1, TIRED_ORIGIN_USER, false, &error));
    CHECK(strcmp(error.code, "duplicate-field") == 0);
    CHECK(spec.fields[TIRED_FIELD_DESCRIPTION].value.text.length == 0);
    CHECK(set(&spec, TIRED_FIELD_NICE, "-20", &error));
    CHECK(!set(&spec, TIRED_FIELD_NICE, "20", &error));
    CHECK(spec.fields[TIRED_FIELD_NICE].value.integer == -20);
    CHECK(!set(&spec, TIRED_FIELD_WORKING_DIRECTORY, "relative", &error));
    CHECK(set(&spec, TIRED_FIELD_WORKING_DIRECTORY, "/space 'quote", &error));
    CHECK(!set(&spec, TIRED_FIELD_WORKING_DIRECTORY, "/line\nbreak", &error));
    CHECK(!set(&spec, TIRED_FIELD_RESTART, "sometimes", &error));
    CHECK(set(&spec, TIRED_FIELD_TYPE, "oneshot", &error));
    CHECK(set(&spec, TIRED_FIELD_RESTART, "always", &error));
    CHECK(!tired_spec_validate_scalars(&spec, &error));
    CHECK(strcmp(error.code, "oneshot-restart") == 0);
    CHECK(set(&spec, TIRED_FIELD_TYPE, "forking", &error));
    CHECK(!tired_spec_validate_scalars(&spec, &error));
    CHECK(set(&spec, TIRED_FIELD_PID_FILE, "/run/relay.pid", &error));
    CHECK(tired_spec_validate_scalars(&spec, &error));
    CHECK(set(&spec, TIRED_FIELD_RESTART_SEC, "0", &error));
    CHECK(!tired_spec_validate_scalars(&spec, &error));
    CHECK(set(&spec, TIRED_FIELD_RESTART_SEC, "1ms", &error));
    CHECK(set(&spec, TIRED_FIELD_START_LIMIT_INTERVAL, "5min", &error));
    CHECK(!tired_spec_validate_scalars(&spec, &error));
    CHECK(set(&spec, TIRED_FIELD_START_LIMIT_INTERVAL, "0", &error));
    CHECK(set(&spec, TIRED_FIELD_SCOPE, "user", &error));
    CHECK(set(&spec, TIRED_FIELD_NETWORK, "online", &error));
    CHECK(!tired_spec_validate_scalars(&spec, &error));
    CHECK(set(&spec, TIRED_FIELD_NETWORK, "none", &error));
    CHECK(tired_spec_validate_scalars(&spec, &error));
    for (unsigned i = 0; i < TIRED_FIELD_COUNT; ++i)
    {
        const TiredField *field = tired_field_get((TiredFieldId)i);
        CHECK(field != NULL && field->id == (TiredFieldId)i);
        CHECK(tired_field_find(field->name, strlen(field->name)) == field);
    }
    CHECK(tired_field_find("ExecStartPre", 12) == NULL);
    CHECK(tired_field_get((TiredFieldId)-1) == NULL);
    uint64_t duration = 0;
    CHECK(tired_parse_duration("1.5min", 6, &duration, &error) && duration == 90000000);
    CHECK(tired_parse_duration("0.000001s", 9, &duration, &error) && duration == 1);
    CHECK(tired_parse_duration("18446744073709551615us", 22, &duration, &error) &&
          duration == UINT64_MAX);
    const char *bad[] = {"",
                         "-1",
                         "+1",
                         "1 ",
                         "1e3",
                         "1.2.3",
                         ".5",
                         "1.",
                         "0.1us",
                         "1.1234567s",
                         "18446744073709551616us",
                         "18446744073710s"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        CHECK(!tired_parse_duration(bad[i], strlen(bad[i]), &duration, &error));
    CHECK(duration == UINT64_MAX);
    CHECK(tired_spec_defaults(&spec, &error));
    CHECK(set(&spec, TIRED_FIELD_RETRY_POLICY, "limited", &error));
    CHECK(tired_spec_resolve_retry(&spec, &error));
    CHECK(spec.fields[TIRED_FIELD_START_LIMIT_INTERVAL].value.microseconds == 300000000);
    CHECK(spec.fields[TIRED_FIELD_START_LIMIT_BURST].value.integer == 10);
    CHECK(set(&spec, TIRED_FIELD_START_LIMIT_BURST, "7", &error));
    CHECK(tired_spec_resolve_retry(&spec, &error));
    CHECK(spec.fields[TIRED_FIELD_START_LIMIT_BURST].value.integer == 7);
    CHECK(set(&spec, TIRED_FIELD_START_LIMIT_INTERVAL, "0", &error));
    CHECK(!tired_spec_resolve_retry(&spec, &error));
    CHECK(spec.fields[TIRED_FIELD_START_LIMIT_INTERVAL].origin == TIRED_ORIGIN_USER);
    CHECK(tired_spec_defaults(&spec, &error));
    CHECK(set(&spec, TIRED_FIELD_RETRY_POLICY, "limited", &error));
    CHECK(tired_spec_resolve_retry(&spec, &error));
    CHECK(set(&spec, TIRED_FIELD_RETRY_POLICY, "persistent", &error));
    CHECK(tired_spec_resolve_retry(&spec, &error));
    CHECK(spec.fields[TIRED_FIELD_START_LIMIT_INTERVAL].value.microseconds == 0);
    CHECK(spec.fields[TIRED_FIELD_START_LIMIT_BURST].origin == TIRED_ORIGIN_INHERITED);
    CHECK(tired_spec_append(&spec, TIRED_FIELD_ARGV, "", 0, TIRED_ORIGIN_CAPTURE, &error));
    CHECK(tired_spec_append(&spec, TIRED_FIELD_ARGV, "a\nb", 3, TIRED_ORIGIN_CAPTURE, &error));
    CHECK(spec.fields[TIRED_FIELD_ARGV].value.list.count == 2);
    CHECK(!tired_spec_append(&spec, TIRED_FIELD_ARGV, "x", 1, TIRED_ORIGIN_USER, &error));
    CHECK(spec.fields[TIRED_FIELD_ARGV].value.list.count == 2);
    CHECK(tired_spec_clear_list(&spec, TIRED_FIELD_ARGV, TIRED_ORIGIN_USER, &error));
    CHECK(spec.fields[TIRED_FIELD_ARGV].origin == TIRED_ORIGIN_USER);
    CHECK(spec.fields[TIRED_FIELD_ARGV].value.list.count == 0);
    CHECK(tired_spec_append(&spec, TIRED_FIELD_AFTER, "network.target", 14, TIRED_ORIGIN_USER,
                            &error));
    CHECK(!tired_spec_append(&spec, TIRED_FIELD_AFTER, "x\nExecStart=bad", 15, TIRED_ORIGIN_USER,
                             &error));
    CHECK(!tired_spec_append(&spec, TIRED_FIELD_AFTER, "x.invalid", 9, TIRED_ORIGIN_USER, &error));
    CHECK(!tired_spec_append(&spec, TIRED_FIELD_AFTER, "../x.service", 12, TIRED_ORIGIN_USER,
                             &error));
    CHECK(tired_spec_append(&spec, TIRED_FIELD_ENVIRONMENT_FILES, "/file with spaces", 17,
                            TIRED_ORIGIN_USER, &error));
    CHECK(!tired_spec_append(&spec, TIRED_FIELD_ENVIRONMENT_FILES, "relative", 8, TIRED_ORIGIN_USER,
                             &error));
    CHECK(tired_spec_append(&spec, TIRED_FIELD_STATE_DIRECTORY, "relay/data", 10, TIRED_ORIGIN_USER,
                            &error));
    CHECK(!tired_spec_append(&spec, TIRED_FIELD_STATE_DIRECTORY, "relay/../other", 14,
                             TIRED_ORIGIN_USER, &error));
    CHECK(!tired_spec_append(&spec, TIRED_FIELD_STATE_DIRECTORY, "/absolute", 9, TIRED_ORIGIN_USER,
                             &error));
    CHECK(!tired_spec_append(&spec, TIRED_FIELD_STATE_DIRECTORY, "relay//data", 11,
                             TIRED_ORIGIN_USER, &error));
    CHECK(!set(&spec, TIRED_FIELD_AFTER, "network.target", &error));
    CHECK(tired_spec_defaults(&spec, &error));
    CHECK(set(&spec, TIRED_FIELD_NOFILE_SOFT, "1024", &error));
    CHECK(!tired_spec_validate_scalars(&spec, &error));
    CHECK(set(&spec, TIRED_FIELD_NOFILE_HARD, "512", &error));
    CHECK(!tired_spec_validate_scalars(&spec, &error));
    CHECK(set(&spec, TIRED_FIELD_NOFILE_HARD, "infinity", &error));
    CHECK(tired_spec_validate_scalars(&spec, &error));
    CHECK(set(&spec, TIRED_FIELD_NOFILE_SOFT, "infinity", &error));
    CHECK(tired_spec_validate_scalars(&spec, &error));
    CHECK(set(&spec, TIRED_FIELD_NOFILE_HARD, "4096", &error));
    CHECK(!tired_spec_validate_scalars(&spec, &error));
    CHECK(set(&spec, TIRED_FIELD_MEMORY_MAX, "2G", &error));
    CHECK(spec.fields[TIRED_FIELD_MEMORY_MAX].value.limit.value == UINT64_C(2147483648));
    CHECK(!set(&spec, TIRED_FIELD_MEMORY_MAX, "16E", &error));
    CHECK(!set(&spec, TIRED_FIELD_MEMORY_MAX, "18446744073709551615", &error));
    CHECK(!set(&spec, TIRED_FIELD_TASKS_MAX, "0", &error));
    CHECK(!set(&spec, TIRED_FIELD_TASKS_MAX, "1K", &error));
    CHECK(set(&spec, TIRED_FIELD_TASKS_MAX, "infinity", &error));
    CHECK(set(&spec, TIRED_FIELD_CPU_QUOTA, "250.25%", &error));
    CHECK(spec.fields[TIRED_FIELD_CPU_QUOTA].value.quota == 25025);
    CHECK(!set(&spec, TIRED_FIELD_CPU_QUOTA, "0%", &error));
    CHECK(!set(&spec, TIRED_FIELD_CPU_QUOTA, "1.001%", &error));
    CHECK(!set(&spec, TIRED_FIELD_CPU_QUOTA, "100", &error));
    CHECK(set(&spec, TIRED_FIELD_CPU_QUOTA, "0.01%", &error));
    CHECK(spec.fields[TIRED_FIELD_CPU_QUOTA].value.quota == 1);
    CHECK(set(&spec, TIRED_FIELD_UMASK, "0077", &error));
    CHECK(spec.fields[TIRED_FIELD_UMASK].value.mode == 077);
    CHECK(!set(&spec, TIRED_FIELD_UMASK, "1000", &error));
    CHECK(!set(&spec, TIRED_FIELD_UMASK, "0088", &error));
    CHECK(set(&spec, TIRED_FIELD_STATE_DIRECTORY_MODE, "2750", &error));
    CHECK(spec.fields[TIRED_FIELD_STATE_DIRECTORY_MODE].value.mode == 02750);
    CHECK(!set(&spec, TIRED_FIELD_STATE_DIRECTORY_MODE, "77777", &error));
    tired_spec_destroy(&spec);
    tired_spec_destroy(&spec);
    return 0;
}
