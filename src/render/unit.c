#include "tired/capture.h"
#include "tired/encode.h"
#include "tired/render.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool present(const TiredFieldValue *v) { return v->origin >= TIRED_ORIGIN_DEFAULT; }
static bool append(TiredBuffer *b, const char *s, TiredError *e)
{
    return tired_buffer_append(b, s, strlen(s), e);
}
static bool encoded(TiredBuffer *b, const TiredText *text, bool token, TiredError *e)
{
    TiredText value = {0};
    bool ok = token ? tired_encode_token(text->data, text->length, &value, e)
                    : tired_encode_directive(text->data, text->length, &value, e);
    if (ok)
        ok = tired_buffer_append(b, value.data, value.length, e);
    tired_text_destroy(&value);
    return ok;
}

static bool number(TiredBuffer *b, uint64_t n, TiredError *e)
{
    char value[32];
    int size = snprintf(value, sizeof(value), "%" PRIu64, n);
    assert(size > 0 && (size_t)size < sizeof(value));
    return tired_buffer_append(b, value, (size_t)size, e);
}
static bool limit(TiredBuffer *b, TiredLimit value, TiredError *e)
{
    return value.infinity ? append(b, "infinity", e) : number(b, value.value, e);
}

static bool scalar(TiredBuffer *b, const TiredField *field, const TiredFieldValue *v, TiredError *e)
{
    char text[64];
    int size;
    switch (field->kind)
    {
    case TIRED_FIELD_TEXT:
        return encoded(b, &v->value.text, false, e);
    case TIRED_FIELD_BOOL:
        return append(b, v->value.boolean ? "yes" : "no", e);
    case TIRED_FIELD_CHOICE:
    {
        const char *start = field->choices;
        for (size_t i = 0; i < v->value.choice; ++i)
        {
            start = strchr(start, '|');
            if (start == NULL)
                return tired_error_set(e, TIRED_INVALID, "model-choice", "Invalid typed choice.",
                                       0);
            ++start;
        }
        const char *end = strchr(start, '|');
        return tired_buffer_append(b, start, end == NULL ? strlen(start) : (size_t)(end - start),
                                   e);
    }
    case TIRED_FIELD_DURATION:
        return number(b, v->value.microseconds, e) && append(b, "us", e);
    case TIRED_FIELD_INTEGER:
        size = snprintf(text, sizeof(text), "%" PRId64, v->value.integer);
        break;
    case TIRED_FIELD_SIGNAL:
        size = snprintf(text, sizeof(text), "%d", v->value.signal_number);
        break;
    case TIRED_FIELD_MODE:
        size = snprintf(text, sizeof(text), "%04" PRIo32, v->value.mode);
        break;
    case TIRED_FIELD_LIMIT:
        return limit(b, v->value.limit, e);
    case TIRED_FIELD_QUOTA:
        size = snprintf(text, sizeof(text), "%" PRIu64 ".%02" PRIu64 "%%", v->value.quota / 100,
                        v->value.quota % 100);
        break;
    case TIRED_FIELD_LIST:
        return tired_error_set(e, TIRED_INTERNAL, "model-kind",
                               "Collection passed to scalar renderer.", 0);
    default:
        return tired_error_set(e, TIRED_INVALID, "model-kind", "Unknown field type.", 0);
    }
    if (size < 0 || (size_t)size >= sizeof(text))
        return tired_error_set(e, TIRED_INTERNAL, "number-format", "Cannot format numeric field.",
                               0);
    return tired_buffer_append(b, text, (size_t)size, e);
}

static int compare_text(const void *a, const void *b)
{
    const TiredText *const *left = a;
    const TiredText *const *right = b;
    return strcmp((*left)->data, (*right)->data);
}
static bool list(TiredBuffer *b, const TiredField *field, const TiredTextList *values,
                 TiredError *e)
{
    if (values->count == 0)
        return append(b, field->directive, e) && append(b, "=\n", e);
    const TiredText **order = malloc(values->count * sizeof(*order));
    if (order == NULL)
        return tired_error_set(e, TIRED_INTERNAL, "allocation", "Cannot order unit values.", 0);
    for (size_t i = 0; i < values->count; ++i)
        order[i] = &values->items[i];
    bool external = field->id == TIRED_FIELD_ENVIRONMENT_FILES;
    if (!external)
        qsort(order, values->count, sizeof(*order), compare_text);
    bool ok = append(b, field->directive, e) && append(b, "=", e);
    for (size_t i = 0; ok && i < values->count; ++i)
    {
        if (i != 0)
            ok = external ? append(b, "\nEnvironmentFile=", e) : append(b, " ", e);
        if (!ok)
            break;
        const TiredText *item = order[i];
        if (field->id == TIRED_FIELD_SUCCESS_EXIT_STATUS ||
            field->id == TIRED_FIELD_RESTART_PREVENT_EXIT_STATUS)
        {
            if (item->length != 0 && !(item->data[0] >= '0' && item->data[0] <= '9') &&
                !(item->length >= 3 && memcmp(item->data, "SIG", 3) == 0))
                ok = append(b, "SIG", e);
        }
        if (ok)
            ok = encoded(b, item, !external && field->absolute_path, e);
    }
    if (ok)
        ok = append(b, "\n", e);
    free(order);
    return ok;
}

static bool valid_uuid(const char *uuid)
{
    if (uuid == NULL || strlen(uuid) != 36)
        return false;
    for (size_t i = 0; i < 36; ++i)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (uuid[i] != '-')
                return false;
        }
        else if (!((uuid[i] >= '0' && uuid[i] <= '9') || (uuid[i] >= 'a' && uuid[i] <= 'f')))
            return false;
    }
    return true;
}

static bool command(TiredBuffer *b, const TiredServiceSpec *spec, TiredError *e)
{
    const TiredTextList *argv = &spec->fields[TIRED_FIELD_ARGV].value.list;
    if (!append(b, "ExecStart=:", e) ||
        !encoded(b, &spec->fields[TIRED_FIELD_EXECUTABLE].value.text, true, e))
        return false;
    for (size_t i = 1; i < argv->count; ++i)
        if (!append(b, " ", e) || !encoded(b, &argv->items[i], true, e))
            return false;
    return append(b, "\n", e);
}

bool tired_render_unit(const TiredServiceSpec *spec, const char *uuid,
                       const TiredText *managed_environment, const TiredCredentials *credentials,
                       TiredText *output, TiredError *error)
{
    assert(spec != NULL && output != NULL);
    if (!valid_uuid(uuid))
        return tired_error_set(error, TIRED_INVALID, "unit-id",
                               "Expected a canonical service UUID.", 0);
    TiredFieldId required[] = {
        TIRED_FIELD_NAME,  TIRED_FIELD_EXECUTABLE, TIRED_FIELD_WORKING_DIRECTORY, TIRED_FIELD_TYPE,
        TIRED_FIELD_SCOPE, TIRED_FIELD_ARGV,       TIRED_FIELD_WANTED_BY};
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i)
        if (!present(&spec->fields[required[i]]))
            return tired_error_set(error, TIRED_INVALID, "incomplete-model",
                                   "Capture required fields and resolve defaults before rendering.",
                                   0);
    bool user = tired_spec_choice_is(spec, TIRED_FIELD_SCOPE, "user");
    if ((!user && (!present(&spec->fields[TIRED_FIELD_RUN_AS]) ||
                   !present(&spec->fields[TIRED_FIELD_GROUP]))) ||
        spec->fields[TIRED_FIELD_ARGV].value.list.count == 0)
        return tired_error_set(error, TIRED_INVALID, "incomplete-model",
                               "A system identity and nonempty command are required.", 0);
    if (!tired_spec_validate_scalars(spec, error))
        return false;
    if (managed_environment != NULL &&
        (managed_environment->length == 0 || managed_environment->data[0] != '/' ||
         !tired_validate_text(managed_environment->data, managed_environment->length, true, error)))
        return tired_error_set(error, TIRED_INVALID, "managed-environment-path",
                               "Managed environment path must be absolute and valid.", 0);
    TiredBuffer result;
    tired_buffer_init(&result, TIRED_UNIT_LIMIT);
    if (!append(&result, "# Managed by tired; id=", error) || !append(&result, uuid, error) ||
        !append(&result, "; schema=1\n", error))
        goto fail;
    const char *sections[] = {"Unit", "Service", "Install"};
    for (size_t section = 0; section < 3; ++section)
    {
        if (!append(&result, "\n[", error) || !append(&result, sections[section], error) ||
            !append(&result, "]\n", error))
            goto fail;
        for (unsigned i = 0; i < TIRED_FIELD_COUNT; ++i)
        {
            const TiredField *field = tired_field_get((TiredFieldId)i);
            const TiredFieldValue *value = &spec->fields[i];
            if (!present(value) || field->section == NULL ||
                strcmp(field->section, sections[section]) != 0)
                continue;
            if (i == TIRED_FIELD_ARGV || i == TIRED_FIELD_NOFILE_HARD)
                continue;
            if (user && (i == TIRED_FIELD_RUN_AS || i == TIRED_FIELD_GROUP ||
                         i == TIRED_FIELD_SUPPLEMENTARY_GROUPS))
                continue;
            if (i == TIRED_FIELD_EXECUTABLE)
            {
                if (!command(&result, spec, error))
                    goto fail;
                continue;
            }
            if (field->kind == TIRED_FIELD_LIST)
            {
                if (!list(&result, field, &value->value.list, error))
                    goto fail;
                continue;
            }
            if (!append(&result, field->directive, error) || !append(&result, "=", error) ||
                !scalar(&result, field, value, error))
                goto fail;
            if (i == TIRED_FIELD_NOFILE_SOFT &&
                (!append(&result, ":", error) ||
                 !limit(&result, spec->fields[TIRED_FIELD_NOFILE_HARD].value.limit, error)))
                goto fail;
            if (!append(&result, "\n", error))
                goto fail;
        }
        if (section == 0)
        {
            if (tired_spec_choice_is(spec, TIRED_FIELD_NETWORK, "network") &&
                !append(&result, "After=network.target\n", error))
                goto fail;
            if (tired_spec_choice_is(spec, TIRED_FIELD_NETWORK, "online") &&
                !append(&result, "After=network-online.target\nWants=network-online.target\n",
                        error))
                goto fail;
        }
        if (section == 1)
        {
            if (managed_environment != NULL &&
                (!append(&result, "EnvironmentFile=", error) ||
                 !encoded(&result, managed_environment, false, error) ||
                 !append(&result, "\n", error)))
                goto fail;
            if (credentials != NULL)
            {
                const char *previous = NULL;
                for (size_t i = 0; i < credentials->count; ++i)
                {
                    const TiredCredential *entry = NULL;
                    for (size_t j = 0; j < credentials->count; ++j)
                    {
                        const TiredCredential *candidate = &credentials->items[j];
                        if ((previous == NULL || strcmp(candidate->name.data, previous) > 0) &&
                            (entry == NULL || strcmp(candidate->name.data, entry->name.data) < 0))
                            entry = candidate;
                    }
                    assert(entry != NULL);
                    if (!append(&result, "LoadCredential=", error) ||
                        !encoded(&result, &entry->name, false, error) ||
                        !append(&result, ":", error) ||
                        !encoded(&result, &entry->path, false, error) ||
                        !append(&result, "\n", error))
                        goto fail;
                    previous = entry->name.data;
                }
            }
        }
    }
    /* systemd's physical line limit is smaller than the complete file budget. */
    size_t line = 0;
    for (size_t i = 0; i < result.length; ++i)
    {
        if (result.data[i] == '\n')
            line = 0;
        else if (++line >= TIRED_INPUT_LIMIT)
        {
            tired_error_set(error, TIRED_INVALID, "unit-line-limit",
                            "A generated directive exceeds the line limit.", 0);
            goto fail;
        }
    }
    return tired_buffer_take(&result, output, error);
fail:
    tired_buffer_destroy(&result);
    return false;
}
