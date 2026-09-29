#include "inspection_live.h"
#include "show_effective.h"
#include "tired/capture.h"
#include "tired/effective_files.h"
#include "tired/encode.h"
#include "tired/file_fingerprint.h"
#include "tired/file_target.h"
#include "tired/io.h"
#include "tired/name.h"
#include "tired/service_inventory.h"
#include "tired/show_frontend.h"
#include "tired/unit_redaction.h"
#include <assert.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool add(struct json_object *object, const char *key, struct json_object *value)
{
    if (value == NULL)
        return false;
    if (json_object_object_add(object, key, value) == 0)
        return true;
    json_object_put(value);
    return false;
}
static void clear(TiredText *text)
{
    if (text->data != NULL)
        OPENSSL_cleanse(text->data, text->length);
    tired_text_destroy(text);
}
static bool text(TiredBuffer *buffer, const char *value, TiredError *error)
{
    return tired_buffer_append(buffer, value, strlen(value), error);
}
/* JSON already escapes C0; escape raw Unicode C1 before terminal display too. */
static bool json_text(TiredBuffer *buffer, struct json_object *document, TiredError *error)
{
    const char *encoded = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PRETTY);
    if (encoded == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot encode show output.",
                               0);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; encoded[i] != '\0'; ++i)
        if ((unsigned char)encoded[i] == 0xc2 && (unsigned char)encoded[i + 1] >= 0x80 &&
            (unsigned char)encoded[i + 1] <= 0x9f)
        {
            unsigned char c = (unsigned char)encoded[++i];
            char escape[] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
            if (!tired_buffer_append(buffer, escape, sizeof(escape), error))
                return false;
        }
        else if (!tired_buffer_append(buffer, encoded + i, 1, error))
            return false;
    return text(buffer, "\n", error);
}
static bool unit_text(const TiredText *unit, bool terminal, TiredBuffer *buffer, TiredError *error)
{
    if (!terminal)
        return tired_buffer_append(buffer, unit->data, unit->length, error);
    if (!tired_validate_text(unit->data, unit->length, false, error))
        return false;
    bool escaped = false;
    for (size_t i = 0; i < unit->length; ++i)
    {
        unsigned char c = (unsigned char)unit->data[i];
        escaped |= (c < 32 && c != '\n' && c != '\t') || c == 127 ||
                   (c == 0xc2 && i + 1 < unit->length && (unsigned char)unit->data[i + 1] >= 0x80 &&
                    (unsigned char)unit->data[i + 1] <= 0x9f);
    }
    if (escaped && !text(buffer, "# Terminal-escaped, non-installable view.\n", error))
        return false;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < unit->length; ++i)
    {
        unsigned char c = (unsigned char)unit->data[i];
        if (c == 0xc2 && i + 1 < unit->length && (unsigned char)unit->data[i + 1] >= 0x80 &&
            (unsigned char)unit->data[i + 1] <= 0x9f)
        {
            c = (unsigned char)unit->data[++i];
            char escape[] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
            if (!tired_buffer_append(buffer, escape, sizeof(escape), error))
                return false;
        }
        else if ((c < 32 && c != '\n' && c != '\t') || c == 127)
        {
            char escape[] = {'\\', 'x', hex[c >> 4], hex[c & 15]};
            if (!tired_buffer_append(buffer, escape, sizeof(escape), error))
                return false;
        }
        else if (!tired_buffer_append(buffer, unit->data + i, 1, error))
            return false;
    }
    return true;
}
static bool read_unit(const TiredLayout *layout, const TiredServiceRecord *record,
                      TiredFileFingerprint *fingerprint, TiredText *bytes, TiredError *error)
{
    TiredFileTarget target = {.role = TIRED_FILE_TARGET_UNIT,
                              .unit_name = record->metadata.unit_name};
    memcpy(target.service_uuid, record->metadata.service_uuid, 37);
    TiredResolvedFile resolved = {0};
    TiredDirectory *directory = NULL;
    bool ok = tired_file_target_resolve(layout, &target, &resolved, error) &&
              tired_directory_open(resolved.directory.data, layout->user_scope ? geteuid() : 0,
                                   false, &directory, error) &&
              tired_file_snapshot(directory, resolved.name.data, TIRED_UNIT_LIMIT, fingerprint,
                                  bytes, error);
    if (ok && !fingerprint->exists)
        ok = tired_error_set(error, TIRED_NOT_FOUND, "show-unit-missing",
                             "Installed unit file is missing.", 0);
    tired_directory_destroy(directory);
    tired_resolved_file_destroy(&resolved);
    return ok;
}
struct json_object *tired_show_effective_json(const TiredEffectiveFiles *files,
                                              const TiredUnitBatchItem *live, bool include_text)
{
    struct json_object *object = json_object_new_object(), *rows = json_object_new_array();
    if (object == NULL || rows == NULL)
        goto failed;
    if (!add(object, "complete", json_object_new_boolean(files->complete)) ||
        !add(object, "live", tired_inspection_live_json(live)) ||
        !add(object, "configuration_load_queued",
             json_object_new_boolean(live->query.configuration_load_queued)) ||
        !add(object, "configuration_load_acknowledged",
             json_object_new_boolean(live->query.configuration_load_acknowledged)) ||
        (files->error.code != NULL &&
         !add(object, "error", json_object_new_string(files->error.code))))
        goto failed;
    for (size_t i = 0; i < files->count; ++i)
    {
        const TiredEffectiveFile *file = &files->files[i];
        struct json_object *row = json_object_new_object();
        bool ok = row != NULL &&
                  add(row, "role", json_object_new_string(i == 0 ? "fragment" : "drop-in")) &&
                  add(row, "status",
                      json_object_new_string(file->error.status == TIRED_OK          ? "observed"
                                             : file->error.status == TIRED_NOT_FOUND ? "missing"
                                                                                     : "unknown"));
        if (ok && file->reported_path.data != NULL)
            ok = add(row, "path", json_object_new_string(file->reported_path.data));
        if (ok && file->error.status == TIRED_OK)
            ok = add(row, "resolved_path",
                     json_object_new_string(file->source.resolved_path.data)) &&
                 add(row, "sha256", json_object_new_string(file->source.fingerprint.sha256)) &&
                 add(row, "redacted", json_object_new_boolean(file->redaction.redacted)) &&
                 (!include_text ||
                  add(row, "text",
                      json_object_new_string_len(file->display.data, (int)file->display.length)));
        else if (ok && file->error.code != NULL)
            ok = add(row, "error", json_object_new_string(file->error.code));
        if (!ok || json_object_array_add(rows, row) != 0)
        {
            json_object_put(row);
            goto failed;
        }
    }
    bool inserted = add(object, "files", rows);
    rows = NULL;
    if (!inserted)
        goto failed;
    return object;
failed:
    json_object_put(object);
    json_object_put(rows);
    return NULL;
}
bool tired_show_effective_text(const TiredEffectiveFiles *files, bool terminal, TiredBuffer *buffer,
                               TiredError *error)
{
    if (!text(buffer, "# Effective file inspection; not a merged installable unit.\n", error))
        return false;
    if (files->error.message != NULL &&
        (!text(buffer, "# ", error) || !text(buffer, files->error.message, error) ||
         !text(buffer, "\n", error)))
        return false;
    for (size_t i = 0; i < files->count; ++i)
    {
        const TiredEffectiveFile *file = &files->files[i];
        TiredText path = {0};
        const char *raw = file->reported_path.data == NULL ? "unknown" : file->reported_path.data;
        bool ok = tired_encode_display(raw, strlen(raw), &path, error) &&
                  text(buffer, i == 0 ? "\n# Fragment: " : "\n# Drop-in: ", error) &&
                  text(buffer, path.data, error) && text(buffer, "\n", error);
        tired_text_destroy(&path);
        if (!ok)
            return false;
        if (file->error.status != TIRED_OK)
        {
            if (!text(buffer, "# Unavailable: ", error) || !text(buffer, file->error.code, error) ||
                !text(buffer, "\n", error))
                return false;
        }
        else if (!unit_text(&file->display, terminal, buffer, error) ||
                 (file->display.length != 0 &&
                  file->display.data[file->display.length - 1] != '\n' &&
                  !text(buffer, "\n", error)))
            return false;
    }
    return true;
}
static struct json_object *environment_json(const TiredEnvironment *environment, bool sensitive)
{
    static const char *const origins[] = {"default", "profile",  "config", "imported",
                                          "passed",  "explicit", "edited"};
    struct json_object *array = json_object_new_array();
    if (array == NULL)
        return NULL;
    for (size_t i = 0; i < environment->count; ++i)
    {
        const TiredEnvironmentEntry *entry = &environment->items[i];
        struct json_object *row = json_object_new_object();
        if (row == NULL || !add(row, "name", json_object_new_string(entry->name.data)) ||
            !add(row, "sensitive", json_object_new_boolean(entry->sensitive)) ||
            !add(row, "origin", json_object_new_string(origins[entry->origin])) ||
            !add(row, "value",
                 json_object_new_string(sensitive ? entry->value.data
                                                  : tired_environment_display(entry))) ||
            json_object_array_add(array, row) != 0)
        {
            json_object_put(row);
            json_object_put(array);
            return NULL;
        }
    }
    return array;
}
static struct json_object *profile_json(const TiredServiceRecord *record)
{
    static const char *const origins[] = {"bundled", "administrator", "user"};
    struct json_object *profile = json_object_new_object();
    if (profile == NULL)
        return NULL;
    if (!add(profile, "present", json_object_new_boolean(record->has_profile)))
        goto fail;
    if (record->has_profile &&
        (!add(profile, "id", json_object_new_string(record->profile.profile.id)) ||
         !add(profile, "revision", json_object_new_uint64(record->profile.profile.revision)) ||
         !add(profile, "source", json_object_new_string(record->profile.source_path.data)) ||
         !add(profile, "source_sha256", json_object_new_string(record->profile.source_sha256)) ||
         !add(profile, "source_origin",
              json_object_new_string(origins[record->profile.source_origin])) ||
         !add(profile, "explicit_selection",
              json_object_new_boolean(record->profile.explicit_selection))))
        goto fail;
    return profile;
fail:
    json_object_put(profile);
    return NULL;
}
bool tired_show_command(const TiredRequest *request, TiredText *output, TiredStatus *result,
                        TiredError *error)
{
    assert(request != NULL && output != NULL && result != NULL);
    if (request->command != TIRED_COMMAND_SHOW || request->arguments.count != 1)
        return tired_error_set(error, TIRED_INVALID, "show-command", "Expected show NAME.", 0);
    bool authorized = false;
    for (size_t i = 0; i < request->allowed_risks.count; ++i)
        authorized |= strcmp(request->allowed_risks.items[i].data, "sensitive-export") == 0;
    if (request->include_sensitive && (request->output.data == NULL || !authorized))
        return tired_error_set(
            error, TIRED_INVALID, "sensitive-export",
            "Sensitive output requires a new private file and sensitive-export acknowledgment.", 0);
    TiredLayout layout = {0};
    TiredServiceInventory inventory = {0};
    TiredServiceRecord record = {0};
    TiredServiceSpec display = {0};
    TiredText base = {0}, name = {0}, bytes = {0}, unit = {0}, encoded = {0};
    TiredFileFingerprint fingerprint = {0};
    TiredError unit_error = {0};
    TiredRedaction model_redaction = {0}, unit_redaction = {0};
    TiredInspectionLive live = {0};
    TiredEffectiveFiles effective = {0};
    TiredUnitBatchItem observation = {0};
    TiredBuffer buffer;
    tired_buffer_init(&buffer, 32U * TIRED_INPUT_LIMIT);
    struct json_object *document = NULL, *metadata = NULL, *model = NULL, *file = NULL;
    bool ok = false, have_unit = false;
    TiredStatus status = TIRED_OK;
    const TiredServiceMetadata *selected = NULL;
    const TiredText *argument = &request->arguments.items[0];
    if (!tired_name_explicit(argument->data, argument->length, &base, error) ||
        !tired_name_candidate(&base, 1, &name, error) ||
        !tired_layout_discover(tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user"),
                               &layout, error) ||
        !tired_service_inventory_load(&layout, &inventory, error) ||
        !tired_service_inventory_find(&inventory, name.data, &selected, error) ||
        !tired_service_record_load(&layout, selected->service_uuid, &record, error))
        goto done;
    if (strcmp(record.metadata.unit_name.data, name.data) != 0)
    {
        tired_error_set(error, TIRED_CONFLICT, "show-record-changed",
                        "Selected record changed during inspection.", 0);
        goto done;
    }
    have_unit = read_unit(&layout, &record, &fingerprint, &bytes, &unit_error);
    if (have_unit)
    {
        if (request->include_sensitive)
        {
            unit = bytes;
            bytes = (TiredText){0};
        }
        else
            have_unit = tired_unit_redact(bytes.data, bytes.length,
                                          &record.spec.fields[TIRED_FIELD_ARGV].value.list,
                                          record.review.sensitive_arguments, &record.environment,
                                          &unit, &unit_redaction, &unit_error);
        if (have_unit && (request->json || !request->unit || request->output.data == NULL))
            have_unit = tired_validate_text(unit.data, unit.length, false, &unit_error);
    }
    if (request->effective)
    {
        tired_inspection_configuration_collect(layout.user_scope, &name, &live);
        observation = tired_inspection_live_item(&live, 0);
        if (!tired_effective_files_collect(&record, &observation.query, request->include_sensitive,
                                           &effective, error))
            goto done;
        if (!effective.complete)
            status = TIRED_RECOVERY_REQUIRED;
    }
    if (request->unit && request->effective)
    {
        if (!tired_show_effective_text(&effective, request->output.data == NULL, &buffer, error))
            goto done;
    }
    else if (request->unit)
    {
        if (!have_unit)
        {
            if (error != NULL)
                *error = unit_error;
            goto done;
        }
        if (!unit_text(&unit, request->output.data == NULL, &buffer, error))
            goto done;
    }
    else
    {
        if (!have_unit && unit_error.status != TIRED_NOT_FOUND)
            status = TIRED_RECOVERY_REQUIRED;
        if (!tired_spec_display(&record.spec, record.review.sensitive_arguments,
                                request->include_sensitive, &display, &model_redaction, error) ||
            !tired_spec_encode(&display, &encoded, error) ||
            !tired_json_parse(encoded.data, encoded.length, TIRED_INPUT_LIMIT, &model, error))
            goto done;
        clear(&encoded);
        if (!tired_service_metadata_encode(&record.metadata, &encoded, error) ||
            !tired_json_parse(encoded.data, encoded.length, 4096, &metadata, error))
            goto done;
        document = json_object_new_object();
        file = json_object_new_object();
        if (document == NULL || file == NULL)
            goto allocation;
        bool inserted = add(document, "metadata", metadata);
        metadata = NULL;
        if (!inserted)
            goto allocation;
        inserted = add(document, "model", model);
        model = NULL;
        if (!inserted || !add(document, "schema_version", json_object_new_int(1)) ||
            !add(document, "command", json_object_new_string("show")) ||
            !add(document, "ok", json_object_new_boolean(status == TIRED_OK)) ||
            !add(document, "exit_code", json_object_new_int(status)) ||
            !add(document, "replayable", json_object_new_boolean(false)) ||
            !add(document, "model_redacted", json_object_new_boolean(model_redaction.redacted)) ||
            !add(document, "environment",
                 environment_json(&record.environment, request->include_sensitive)) ||
            !add(document, "profile", profile_json(&record)) ||
            !add(file, "path", json_object_new_string(record.unit_path.data)) ||
            !add(file, "status",
                 json_object_new_string(have_unit                              ? "observed"
                                        : unit_error.status == TIRED_NOT_FOUND ? "missing"
                                                                               : "unknown")))
            goto allocation;
        if (have_unit)
        {
            char marker[80];
            (void)snprintf(marker, sizeof(marker), "# Managed by tired; id=%s; schema=1\n",
                           record.metadata.service_uuid);
            const TiredText *original = request->include_sensitive ? &unit : &bytes;
            if (!add(file, "sha256", json_object_new_string(fingerprint.sha256)) ||
                !add(file, "digest_matches",
                     json_object_new_boolean(
                         strcmp(fingerprint.sha256, record.metadata.unit_sha256) == 0)) ||
                !add(file, "owner_matches",
                     json_object_new_boolean(fingerprint.uid == record.metadata.owner_uid)) ||
                !add(file, "mode_matches", json_object_new_boolean(fingerprint.mode == 0644)) ||
                !add(
                    file, "marker_matches",
                    json_object_new_boolean(original->length >= strlen(marker) &&
                                            memcmp(original->data, marker, strlen(marker)) == 0)) ||
                !add(file, "redacted", json_object_new_boolean(unit_redaction.redacted)) ||
                (request->json && !request->effective &&
                 !add(file, "text", json_object_new_string_len(unit.data, (int)unit.length))))
                goto allocation;
        }
        else if (!add(file, "error", json_object_new_string(unit_error.code)))
            goto allocation;
        inserted = add(document, "unit", file);
        file = NULL;
        if (!inserted)
            goto allocation;
        if (request->effective &&
            !add(document, "effective",
                 tired_show_effective_json(&effective, &observation, request->json)))
            goto allocation;
        if ((!request->json &&
             !text(&buffer, "# Saved service and current file evidence\n", error)) ||
            !json_text(&buffer, document, error))
            goto done;
        if (!request->json && request->effective &&
            !tired_show_effective_text(&effective, request->output.data == NULL, &buffer, error))
            goto done;
        if (!request->json && !request->effective && have_unit &&
            (!text(&buffer, "\n# Installed unit bytes\n", error) ||
             !unit_text(&unit, request->output.data == NULL, &buffer, error)))
            goto done;
    }
    if (request->output.data != NULL)
    {
        if (!tired_write_private_new(request->output.data, buffer.data, buffer.length, error))
            goto done;
        OPENSSL_cleanse(buffer.data, buffer.length);
        buffer.length = 0;
        if (request->json)
        {
            char receipt[128];
            (void)snprintf(
                receipt, sizeof(receipt),
                "{\"schema_version\":1,\"command\":\"show\",\"exported\":true,\"exit_code\":%d}\n",
                status);
            if (!text(&buffer, receipt, error))
                goto done;
        }
        else if (!request->quiet && !text(&buffer, "Wrote a private service inspection.\n", error))
            goto done;
    }
    if (!tired_buffer_take(&buffer, output, error))
        goto done;
    *result = status;
    tired_error_clear(error);
    ok = true;
    goto done;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate service inspection.", 0);
done:
    tired_effective_files_destroy(&effective);
    tired_inspection_live_destroy(&live);
    json_object_put(document);
    json_object_put(metadata);
    json_object_put(model);
    json_object_put(file);
    if (buffer.data != NULL)
        OPENSSL_cleanse(buffer.data, buffer.length);
    tired_buffer_destroy(&buffer);
    clear(&encoded);
    clear(&unit);
    clear(&bytes);
    tired_text_destroy(&base);
    tired_text_destroy(&name);
    tired_spec_destroy(&display);
    tired_service_record_destroy(&record);
    tired_service_inventory_destroy(&inventory);
    tired_layout_destroy(&layout);
    return ok;
}
