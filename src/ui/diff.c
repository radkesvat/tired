#include "tired/encode.h"
#include "tired/file_fingerprint.h"
#include "tired/json.h"
#include "tired/private_file.h"
#include "tired/render.h"
#include "tired/service_record_storage.h"
#include "tired/ui.h"
#include "tired/ui_diff.h"
#include "tired/unit_redaction.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void argument_masks(const TiredServiceRecord *record, const TiredServiceRecord *other,
                           bool masks[TIRED_ARGUMENT_LIMIT])
{
    const TiredTextList *arguments = &record->spec.fields[TIRED_FIELD_ARGV].value.list;
    (void)tired_argv_classify(arguments, record->review.sensitive_arguments, masks);
    if (other == NULL)
        return;
    const TiredTextList *previous = &other->spec.fields[TIRED_FIELD_ARGV].value.list;
    bool previous_masks[TIRED_ARGUMENT_LIMIT];
    (void)tired_argv_classify(previous, other->review.sensitive_arguments, previous_masks);
    for (size_t i = 1; i < arguments->count; ++i)
    {
        /* Preserve classification at the edited slot and when a classified
         * saved word has moved. Reordering cannot reveal its previous value. */
        masks[i] |= i < previous->count && previous_masks[i];
        for (size_t j = 1; !masks[i] && j < previous->count; ++j)
            masks[i] = previous_masks[j] &&
                       arguments->items[i].length == previous->items[j].length &&
                       memcmp(arguments->items[i].data, previous->items[j].data,
                              arguments->items[i].length) == 0;
    }
}
static bool display_unit(const TiredServiceRecord *record, const TiredServiceRecord *other,
                         TiredText *unit, TiredError *error)
{
    TiredServiceSpec display = {0};
    TiredRedaction redaction = {0};
    bool masks[TIRED_ARGUMENT_LIMIT];
    argument_masks(record, other, masks);
    bool ok = tired_spec_display(&record->spec, masks, false, &display, &redaction, error) &&
              tired_render_unit(&display, record->metadata.service_uuid,
                                record->has_environment ? &record->environment_path : NULL,
                                &record->credentials, unit, error);
    tired_spec_destroy(&display);
    return ok;
}
static bool append(TiredBuffer *buffer, const char *bytes, TiredError *error)
{
    return tired_buffer_append(buffer, bytes, strlen(bytes), error);
}
typedef struct
{
    const char *data;
    size_t length;
    bool newline;
} Line;
typedef struct
{
    char kind;
    size_t old_line, new_line;
} Edit;
static bool lines(const TiredText *text, Line **output, size_t *count, TiredError *error)
{
    size_t size = 0;
    for (size_t i = 0; i < text->length; ++i)
        size += text->data[i] == '\n';
    size += text->length != 0 && text->data[text->length - 1] != '\n';
    if (size > 65536)
        return tired_error_set(error, TIRED_INVALID, "diff-too-large",
                               "Revision comparison has too many lines. Inspect the unit views "
                               "separately before reviewing this change.",
                               0);
    Line *result = size == 0 ? NULL : calloc(size, sizeof(*result));
    if (size != 0 && result == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot allocate revision comparison.", 0);
    size_t index = 0, offset = 0;
    while (offset < text->length)
    {
        const char *end = memchr(text->data + offset, '\n', text->length - offset);
        size_t length = end == NULL ? text->length - offset : (size_t)(end - text->data - offset);
        result[index++] =
            (Line){.data = text->data + offset, .length = length, .newline = end != NULL};
        offset += length + (end != NULL);
    }
    *output = result;
    *count = size;
    return true;
}
static bool same_line(const Line *a, const Line *b)
{
    return a->length == b->length && a->newline == b->newline &&
           memcmp(a->data, b->data, a->length) == 0;
}
bool tired_ui_unified_diff(const char *before_name, const char *after_name, const TiredText *before,
                           const TiredText *after, TiredText *output, TiredError *error)
{
    assert(before_name != NULL && after_name != NULL && before != NULL && after != NULL);
    Line *old = NULL, *current = NULL;
    Edit *edits = NULL;
    uint32_t *lcs = NULL;
    size_t a_count = 0, b_count = 0, edit_count = 0;
    TiredBuffer buffer;
    tired_buffer_init(&buffer, 32U * TIRED_INPUT_LIMIT);
    bool ok = false;
    if (!lines(before, &old, &a_count, error) || !lines(after, &current, &b_count, error))
        goto done;
    size_t prefix = 0, suffix = 0;
    while (prefix < a_count && prefix < b_count && same_line(&old[prefix], &current[prefix]))
        ++prefix;
    while (suffix < a_count - prefix && suffix < b_count - prefix &&
           same_line(&old[a_count - suffix - 1], &current[b_count - suffix - 1]))
        ++suffix;
    size_t rows = a_count - prefix - suffix + 1, columns = b_count - prefix - suffix + 1;
    if (rows > 4U * TIRED_INPUT_LIMIT / columns)
    {
        tired_error_set(error, TIRED_INVALID, "diff-too-large",
                        "Changed revisions exceed the bounded comparison budget. Inspect the "
                        "unit views separately before reviewing this change.",
                        0);
        goto done;
    }
    lcs = calloc(rows * columns, sizeof(*lcs));
    edits = calloc(a_count + b_count + 1, sizeof(*edits));
    if (lcs == NULL || edits == NULL)
    {
        tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate revision comparison.",
                        0);
        goto done;
    }
    for (size_t a = rows - 1; a-- > 0;)
        for (size_t b = columns - 1; b-- > 0;)
            lcs[a * columns + b] = same_line(&old[prefix + a], &current[prefix + b])
                                       ? 1 + lcs[(a + 1) * columns + b + 1]
                                   : lcs[(a + 1) * columns + b] >= lcs[a * columns + b + 1]
                                       ? lcs[(a + 1) * columns + b]
                                       : lcs[a * columns + b + 1];
    size_t a = 0, b = 0;
    while (a < a_count || b < b_count)
    {
        char kind;
        if (a < a_count && b < b_count && same_line(&old[a], &current[b]))
            kind = ' ';
        else if (a == a_count)
            kind = '+';
        else if (b == b_count)
            kind = '-';
        else if (a >= a_count - suffix)
            kind = '+';
        else if (b >= b_count - suffix)
            kind = '-';
        else
            kind = lcs[(a - prefix + 1) * columns + b - prefix] >=
                           lcs[(a - prefix) * columns + b - prefix + 1]
                       ? '-'
                       : '+';
        edits[edit_count++] = (Edit){.kind = kind, .old_line = a, .new_line = b};
        a += kind != '+';
        b += kind != '-';
    }
    if (!append(&buffer, "--- ", error) || !append(&buffer, before_name, error) ||
        !append(&buffer, "\n+++ ", error) || !append(&buffer, after_name, error) ||
        !append(&buffer, "\n", error))
        goto done;
    size_t position = 0;
    bool changed = false;
    while (position < edit_count)
    {
        size_t first = position;
        while (first < edit_count && edits[first].kind == ' ')
            ++first;
        if (first == edit_count)
            break;
        changed = true;
        size_t start = first - position > 3 ? first - 3 : position, last = first;
        for (size_t i = first + 1; i < edit_count; ++i)
        {
            if (edits[i].kind != ' ')
            {
                if (i - last > 7)
                    break;
                last = i;
            }
        }
        size_t end = last + 4 < edit_count ? last + 4 : edit_count;
        size_t old_count = 0, new_count = 0;
        for (size_t i = start; i < end; ++i)
        {
            old_count += edits[i].kind != '+';
            new_count += edits[i].kind != '-';
        }
        char header[128];
        (void)snprintf(header, sizeof(header), "@@ -%zu,%zu +%zu,%zu @@\n",
                       edits[start].old_line + (old_count != 0), old_count,
                       edits[start].new_line + (new_count != 0), new_count);
        if (!append(&buffer, header, error))
            goto done;
        for (size_t i = start; i < end; ++i)
        {
            const Edit *edit = &edits[i];
            const Line *line = edit->kind == '+' ? &current[edit->new_line] : &old[edit->old_line];
            if (!tired_buffer_append(&buffer, &edit->kind, 1, error) ||
                !tired_buffer_append(&buffer, line->data, line->length, error) ||
                !append(&buffer, "\n", error) ||
                (!line->newline && !append(&buffer, "\\ No newline at end of file\n", error)))
                goto done;
        }
        position = end;
    }
    ok = (changed || append(&buffer, "(No unit text changes.)\n", error)) &&
         tired_buffer_take(&buffer, output, error);
done:
    free(old);
    free(current);
    free(edits);
    free(lcs);
    tired_buffer_destroy(&buffer);
    return ok;
}
static bool same_text(const TiredText *a, const TiredText *b)
{
    return a->length == b->length && (a->length == 0 || memcmp(a->data, b->data, a->length) == 0);
}
static bool same_field(TiredFieldId id, const TiredFieldValue *a, const TiredFieldValue *b)
{
    if (a->origin != b->origin || a->inherit != b->inherit)
        return false;
    if (!tired_field_has_value(a))
        return true;
    switch (tired_field_get(id)->kind)
    {
    case TIRED_FIELD_TEXT:
        return same_text(&a->value.text, &b->value.text);
    case TIRED_FIELD_CHOICE:
        return a->value.choice == b->value.choice;
    case TIRED_FIELD_BOOL:
        return a->value.boolean == b->value.boolean;
    case TIRED_FIELD_INTEGER:
        return a->value.integer == b->value.integer;
    case TIRED_FIELD_DURATION:
        return a->value.microseconds == b->value.microseconds;
    case TIRED_FIELD_TIMEOUT:
        return a->value.timeout.infinity == b->value.timeout.infinity &&
               (a->value.timeout.infinity || a->value.timeout.value == b->value.timeout.value);
    case TIRED_FIELD_LIMIT:
        return a->value.limit.infinity == b->value.limit.infinity &&
               (a->value.limit.infinity || a->value.limit.value == b->value.limit.value);
    case TIRED_FIELD_MODE:
        return a->value.mode == b->value.mode;
    case TIRED_FIELD_QUOTA:
        return a->value.quota == b->value.quota;
    case TIRED_FIELD_SIGNAL:
        return a->value.signal_number == b->value.signal_number;
    case TIRED_FIELD_LIST:
        if (a->value.list.count != b->value.list.count)
            return false;
        for (size_t i = 0; i < a->value.list.count; ++i)
            if (!same_text(&a->value.list.items[i], &b->value.list.items[i]))
                return false;
        return true;
    }
    return false;
}
static bool display_model(const TiredServiceRecord *record, const TiredServiceRecord *other,
                          struct json_object **output, TiredError *error)
{
    bool masks[TIRED_ARGUMENT_LIMIT];
    argument_masks(record, other, masks);
    TiredServiceSpec display = {0};
    TiredRedaction redaction = {0};
    TiredText encoded = {0};
    bool ok = tired_spec_display(&record->spec, masks, false, &display, &redaction, error) &&
              tired_spec_encode(&display, &encoded, error) &&
              tired_json_parse(encoded.data, encoded.length, TIRED_INPUT_LIMIT, output, error);
    tired_spec_destroy(&display);
    tired_text_destroy(&encoded);
    return ok;
}
static bool field_value(TiredBuffer *buffer, const TiredField *field, struct json_object *entry,
                        TiredError *error)
{
    struct json_object *value = NULL, *origin = NULL, *inherit = NULL;
    if (!json_object_object_get_ex(entry, "value", &value) ||
        !json_object_object_get_ex(entry, "origin", &origin) ||
        !json_object_object_get_ex(entry, "inherit", &inherit))
        return false;
    const char *text = value == NULL ? json_object_get_boolean(inherit) ? "inherit" : "unset"
                       : field->kind == TIRED_FIELD_TEXT || field->kind == TIRED_FIELD_LIST
                           ? json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN)
                           : json_object_get_string(value);
    return text != NULL && append(buffer, text, error) && append(buffer, " (", error) &&
           append(buffer, json_object_get_string(origin), error) && append(buffer, ")", error);
}
static bool environment_value(TiredBuffer *buffer, const TiredEnvironmentEntry *entry, bool loaded,
                              bool sensitive, TiredError *error)
{
    if (entry == NULL)
        return append(buffer, "absent", error);
    struct json_object *text =
        loaded && !sensitive
            ? json_object_new_string_len(entry->value.data, (int)entry->value.length)
            : NULL;
    const char *encoded = !loaded     ? "unknown saved value"
                          : sensitive ? "[redacted]"
                          : text == NULL
                              ? NULL
                              : json_object_to_json_string_ext(text, JSON_C_TO_STRING_PLAIN);
    static const char *origins[] = {"default", "profile",  "config", "imported",
                                    "passed",  "explicit", "edited"};
    bool ok = encoded != NULL && append(buffer, encoded, error) && append(buffer, " (", error) &&
              append(buffer, origins[entry->origin], error) && append(buffer, ")", error);
    json_object_put(text);
    return ok;
}
bool tired_ui_field_diff(const TiredServiceRecord *before, const TiredServiceRecord *after,
                         TiredText *output, TiredError *error)
{
    assert(before != NULL && after != NULL && output != NULL);
    struct json_object *old = NULL, *current = NULL, *old_fields = NULL, *new_fields = NULL;
    TiredBuffer buffer;
    tired_buffer_init(&buffer, 8U * TIRED_INPUT_LIMIT);
    bool ok = display_model(before, after, &old, error) &&
              display_model(after, before, &current, error) &&
              json_object_object_get_ex(old, "fields", &old_fields) &&
              json_object_object_get_ex(current, "fields", &new_fields) &&
              append(&buffer, "Typed fields; saved -> proposed:\n", error);
    bool changed = false;
    static const char *kinds[] = {"text", "choice", "boolean", "integer", "duration", "timeout",
                                  "list", "limit",  "mode",    "quota",   "signal"};
    for (unsigned i = 0; ok && i < TIRED_FIELD_COUNT; ++i)
    {
        const TiredField *field = tired_field_get((TiredFieldId)i);
        bool same = same_field(field->id, &before->spec.fields[i], &after->spec.fields[i]);
        if (i == TIRED_FIELD_ARGV)
            same &= memcmp(before->review.sensitive_arguments, after->review.sensitive_arguments,
                           sizeof(before->review.sensitive_arguments)) == 0;
        if (same)
            continue;
        changed = true;
        struct json_object *a = NULL, *b = NULL;
        ok = json_object_object_get_ex(old_fields, field->name, &a) &&
             json_object_object_get_ex(new_fields, field->name, &b) &&
             append(&buffer, field->name, error) && append(&buffer, " [", error) &&
             append(&buffer, kinds[field->kind], error) && append(&buffer, "]: ", error) &&
             field_value(&buffer, field, a, error) && append(&buffer, " -> ", error) &&
             field_value(&buffer, field, b, error) &&
             (!json_object_equal(a, b) ||
              append(&buffer, " (classified value or classification changed)", error)) &&
             append(&buffer, "\n", error);
    }
    for (unsigned side = 0; ok && side < 2; ++side)
    {
        const TiredEnvironment *environment =
            side == 0 ? &before->environment : &after->environment;
        for (size_t i = 0; ok && i < environment->count; ++i)
        {
            const TiredEnvironmentEntry *entry = &environment->items[i];
            const TiredEnvironmentEntry *a = tired_environment_find(&before->environment,
                                                                    entry->name.data,
                                                                    entry->name.length),
                                        *b = tired_environment_find(&after->environment,
                                                                    entry->name.data,
                                                                    entry->name.length);
            if ((side != 0 && a != NULL) ||
                (a != NULL && b != NULL && before->environment_loaded &&
                 after->environment_loaded && same_text(&a->value, &b->value) &&
                 a->origin == b->origin && a->sensitive == b->sensitive))
                continue;
            changed = true;
            bool sensitive = (a != NULL && a->sensitive) || (b != NULL && b->sensitive) ||
                             tired_environment_name_sensitive(entry->name.data, entry->name.length);
            ok = append(&buffer, "environment.", error) &&
                 tired_buffer_append(&buffer, entry->name.data, entry->name.length, error) &&
                 append(&buffer, " [text]: ", error) &&
                 environment_value(&buffer, a, before->environment_loaded, sensitive, error) &&
                 append(&buffer, " -> ", error) &&
                 environment_value(&buffer, b, after->environment_loaded, sensitive, error) &&
                 append(&buffer, sensitive ? " (classified value or metadata changed)\n" : "\n",
                        error);
        }
    }
    ok = ok && (changed || append(&buffer, "(No typed field changes.)\n", error)) &&
         tired_buffer_take(&buffer, output, error);
    json_object_put(old);
    json_object_put(current);
    tired_buffer_destroy(&buffer);
    return ok;
}
static bool comparison(TiredBuffer *buffer, const char *before_name, const char *after_name,
                       const TiredText *before, const TiredText *after, TiredError *error)
{
    TiredText diff = {0};
    bool ok = tired_ui_unified_diff(before_name, after_name, before, after, &diff, error) &&
              tired_buffer_append(buffer, diff.data, diff.length, error);
    tired_text_destroy(&diff);
    return ok;
}
void tired_ui_diff(const TiredMutation *mutation, const TiredLayout *layout, TiredError *error)
{
    TiredServiceRecord previous = {0};
    TiredText saved = {0}, actual = {0}, proposed = {0}, bytes = {0}, report = {0}, fields = {0},
              redacted_disk = {0};
    TiredFileFingerprint fingerprint = {0};
    TiredDirectory *directory = NULL;
    TiredRedaction redaction = {0};
    TiredBuffer buffer;
    tired_buffer_init(&buffer, 32U * TIRED_INPUT_LIMIT);
    bool ok;
    if (mutation->operation == TIRED_TRANSACTION_CREATE)
        ok = display_unit(&mutation->proposed, NULL, &proposed, error) &&
             append(&buffer, "New service; no previous managed revision.\n", error) &&
             comparison(&buffer, "empty", "proposed-unit", &saved, &proposed, error);
    else
    {
        ok =
            tired_service_record_load(layout, mutation->proposed.metadata.service_uuid, &previous,
                                      error) &&
            tired_service_record_hydrate(&previous, layout, error) &&
            display_unit(&previous, &mutation->proposed, &saved, error) &&
            display_unit(&mutation->proposed, &previous, &proposed, error) &&
            tired_directory_open(layout->paths[TIRED_PATH_UNITS].data, geteuid(), false, &directory,
                                 error) &&
            tired_file_snapshot(directory, previous.metadata.unit_name.data, 4U * TIRED_INPUT_LIMIT,
                                &fingerprint, &bytes, error) &&
            fingerprint.exists &&
            tired_unit_redact(bytes.data, bytes.length,
                              &previous.spec.fields[TIRED_FIELD_ARGV].value.list,
                              previous.review.sensitive_arguments, &previous.environment, &actual,
                              &redaction, error) &&
            tired_unit_redact(actual.data, actual.length,
                              &mutation->proposed.spec.fields[TIRED_FIELD_ARGV].value.list,
                              mutation->proposed.review.sensitive_arguments,
                              &mutation->proposed.environment, &redacted_disk, &redaction, error) &&
            tired_ui_field_diff(&previous, &mutation->proposed, &fields, error) &&
            tired_buffer_append(&buffer, fields.data, fields.length, error) &&
            append(&buffer, "\n", error) &&
            comparison(&buffer, "saved-managed-unit", "current-disk-unit", &saved, &redacted_disk,
                       error) &&
            append(&buffer, "\n", error) &&
            comparison(&buffer, "saved-managed-unit", "proposed-unit", &saved, &proposed, error) &&
            append(&buffer, "\n", error) &&
            comparison(&buffer, "current-disk-unit", "proposed-unit", &redacted_disk, &proposed,
                       error);
    }
    if (ok && tired_buffer_take(&buffer, &report, error))
        tired_ui_text_view("Revision comparison; sensitive command data is redacted", report.data);
    else
        tired_ui_text_view("Cannot compare revisions",
                           error->message == NULL ? "Stored or disk revision is unavailable."
                                                  : error->message);
    tired_service_record_destroy(&previous);
    tired_directory_destroy(directory);
    tired_text_destroy(&saved);
    tired_text_destroy(&actual);
    tired_text_destroy(&proposed);
    tired_text_destroy(&bytes);
    tired_text_destroy(&report);
    tired_text_destroy(&fields);
    tired_text_destroy(&redacted_disk);
    tired_buffer_destroy(&buffer);
}
