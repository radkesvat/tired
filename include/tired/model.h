#ifndef TIRED_MODEL_H
#define TIRED_MODEL_H

#include "tired/value.h"

typedef enum
{
    TIRED_FIELD_TEXT,
    TIRED_FIELD_CHOICE,
    TIRED_FIELD_BOOL,
    TIRED_FIELD_INTEGER,
    TIRED_FIELD_DURATION
} TiredFieldKind;
typedef enum
{
    TIRED_ORIGIN_UNSET,
    TIRED_ORIGIN_INHERITED,
    TIRED_ORIGIN_DEFAULT,
    TIRED_ORIGIN_PROFILE,
    TIRED_ORIGIN_USER,
    TIRED_ORIGIN_CAPTURE
} TiredFieldOrigin;
typedef enum
{
    TIRED_FIELD_NAME,
    TIRED_FIELD_DESCRIPTION,
    TIRED_FIELD_SCOPE,
    TIRED_FIELD_RUN_AS,
    TIRED_FIELD_GROUP,
    TIRED_FIELD_EXECUTABLE,
    TIRED_FIELD_WORKING_DIRECTORY,
    TIRED_FIELD_TYPE,
    TIRED_FIELD_PID_FILE,
    TIRED_FIELD_REMAIN_AFTER_EXIT,
    TIRED_FIELD_RESTART,
    TIRED_FIELD_RESTART_SEC,
    TIRED_FIELD_RETRY_POLICY,
    TIRED_FIELD_START_LIMIT_INTERVAL,
    TIRED_FIELD_START_LIMIT_BURST,
    TIRED_FIELD_TIMEOUT_START,
    TIRED_FIELD_TIMEOUT_STOP,
    TIRED_FIELD_KILL_MODE,
    TIRED_FIELD_STANDARD_INPUT,
    TIRED_FIELD_STANDARD_OUTPUT,
    TIRED_FIELD_STANDARD_ERROR,
    TIRED_FIELD_SYSLOG_IDENTIFIER,
    TIRED_FIELD_NICE,
    TIRED_FIELD_NO_NEW_PRIVILEGES,
    TIRED_FIELD_PRIVATE_TMP,
    TIRED_FIELD_PROTECT_SYSTEM,
    TIRED_FIELD_PROTECT_HOME,
    TIRED_FIELD_NETWORK,
    TIRED_FIELD_START,
    TIRED_FIELD_ENABLE,
    TIRED_FIELD_ENABLE_LINGER,
    TIRED_FIELD_COUNT
} TiredFieldId;

typedef struct
{
    TiredFieldId id;
    const char *name;
    const char *section;
    const char *directive;
    TiredFieldKind kind;
    const char *choices;       /* Pipe-separated canonical choice tokens. */
    const char *default_value; /* NULL means inherited. */
    int64_t minimum;
    int64_t maximum;
    bool absolute_path;
} TiredField;

typedef struct
{
    TiredFieldOrigin origin;
    union
    {
        TiredText text;
        size_t choice;
        bool boolean;
        int64_t integer;
        uint64_t microseconds;
    } value;
} TiredFieldValue;

typedef struct
{
    TiredFieldValue fields[TIRED_FIELD_COUNT];
} TiredServiceSpec;

const TiredField *tired_field_get(TiredFieldId id);
const TiredField *tired_field_find(const char *name, size_t length);
/* Duration syntax: decimal value with up to six fractional digits, optional
 * suffix us/ms/s/min/h/d. No suffix means seconds. Exact microsecond precision
 * required; no floating point, rounding, infinity, signs, or whitespace. */
bool tired_parse_duration(const char *text, size_t length, uint64_t *microseconds,
                          TiredError *error);
/* Initialize all model objects to zero. Copies text. Failed assignments preserve
 * the model. replace=false rejects duplicate user assignments. Semantic and host
 * validation must follow; setting a field does not authorize any operation. */
bool tired_spec_set(TiredServiceSpec *spec, TiredFieldId id, const char *text, size_t length,
                    TiredFieldOrigin origin, bool replace, TiredError *error);
bool tired_spec_defaults(TiredServiceSpec *spec, TiredError *error);
void tired_spec_destroy(TiredServiceSpec *spec);
/* Validate implemented scalar combinations; missing captured fields are allowed
 * while constructing a proposal. Full install validation is a separate layer. */
bool tired_spec_validate_scalars(const TiredServiceSpec *spec, TiredError *error);
bool tired_spec_choice_is(const TiredServiceSpec *spec, TiredFieldId id, const char *choice);

#endif
