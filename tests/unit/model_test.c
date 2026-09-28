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
    tired_spec_destroy(&spec);
    tired_spec_destroy(&spec);
    return 0;
}
