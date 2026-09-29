#include "tired/file_fingerprint.h"
#include "tired/json.h"
#include "tired/private_file.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

_Static_assert((dev_t)-1 > 0 && sizeof(dev_t) <= sizeof(uint64_t), "Unsigned device identity");
_Static_assert((ino_t)-1 > 0 && sizeof(ino_t) <= sizeof(uint64_t), "Unsigned inode identity");
_Static_assert((uid_t)-1 > 0 && sizeof(uid_t) <= sizeof(uint64_t), "Unsigned owner identity");
_Static_assert((gid_t)-1 > 0 && sizeof(gid_t) <= sizeof(uint64_t), "Unsigned group identity");
static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "fingerprint-schema",
                           "File fingerprint contains missing, unknown or invalid fields.", 0);
}
static bool valid(const TiredFileFingerprint *fingerprint, TiredError *error)
{
    if (!fingerprint->exists)
        return true;
    if ((fingerprint->mode & ~(mode_t)07777) != 0 || fingerprint->size > TIRED_PRIVATE_FILE_LIMIT ||
        strnlen(fingerprint->sha256, sizeof(fingerprint->sha256)) != 64)
        return invalid(error);
    for (size_t i = 0; i < 64; ++i)
        if (!((fingerprint->sha256[i] >= '0' && fingerprint->sha256[i] <= '9') ||
              (fingerprint->sha256[i] >= 'a' && fingerprint->sha256[i] <= 'f')))
            return invalid(error);
    return true;
}
bool tired_file_fingerprint_parse(const char *data, size_t length, TiredFileFingerprint *output,
                                  TiredError *error)
{
    assert(output != NULL);
    struct json_object *document = NULL, *field = NULL;
    TiredFileFingerprint parsed = {0};
    uint64_t version;
    bool ok = false;
    if (!tired_json_parse(data, length, 4096, &document, error))
        goto done;
    if (!json_object_is_type(document, json_type_object) ||
        !json_object_object_get_ex(document, "schema_version", &field) ||
        !tired_json_u64(field, 1, 1, &version, error) ||
        !json_object_object_get_ex(document, "exists", &field) ||
        !json_object_is_type(field, json_type_boolean))
        goto schema_error;
    parsed.exists = json_object_get_boolean(field);
    if (json_object_object_length(document) != (parsed.exists ? 9 : 2))
        goto schema_error;
    if (parsed.exists)
    {
        static const char *const names[] = {"device", "inode", "uid", "gid", "mode", "size"};
        const uint64_t maxima[] = {(uint64_t)((dev_t)-1),
                                   (uint64_t)((ino_t)-1),
                                   (uint64_t)((uid_t)-1),
                                   (uint64_t)((gid_t)-1),
                                   07777,
                                   TIRED_PRIVATE_FILE_LIMIT};
        uint64_t values[6];
        for (size_t i = 0; i < 6; ++i)
            if (!json_object_object_get_ex(document, names[i], &field) ||
                !tired_json_u64(field, 0, maxima[i], &values[i], error))
                goto schema_error;
        parsed.device = (dev_t)values[0];
        parsed.inode = (ino_t)values[1];
        parsed.uid = (uid_t)values[2];
        parsed.gid = (gid_t)values[3];
        parsed.mode = (mode_t)values[4];
        parsed.size = values[5];
        if (!json_object_object_get_ex(document, "sha256", &field) ||
            !json_object_is_type(field, json_type_string) ||
            json_object_get_string_len(field) != 64)
            goto schema_error;
        memcpy(parsed.sha256, json_object_get_string(field), 64);
        if (!valid(&parsed, error))
            goto done;
    }
    *output = parsed;
    tired_error_clear(error);
    ok = true;
    goto done;
schema_error:
    invalid(error);
done:
    json_object_put(document);
    return ok;
}
bool tired_file_fingerprint_encode(const TiredFileFingerprint *fingerprint, TiredText *output,
                                   TiredError *error)
{
    assert(fingerprint != NULL && output != NULL);
    if (!valid(fingerprint, error))
        return false;
    char buffer[512];
    int length;
    if (!fingerprint->exists)
        length = snprintf(buffer, sizeof(buffer), "{\"schema_version\":1,\"exists\":false}\n");
    else
        length = snprintf(buffer, sizeof(buffer),
                          "{\"schema_version\":1,\"exists\":true,\"device\":%" PRIu64
                          ",\"inode\":%" PRIu64 ",\"uid\":%" PRIu64 ",\"gid\":%" PRIu64
                          ",\"mode\":%u,\"size\":%" PRIu64 ",\"sha256\":\"%s\"}\n",
                          (uint64_t)fingerprint->device, (uint64_t)fingerprint->inode,
                          (uint64_t)fingerprint->uid, (uint64_t)fingerprint->gid,
                          (unsigned)fingerprint->mode, fingerprint->size, fingerprint->sha256);
    if (length < 0 || (size_t)length >= sizeof(buffer))
        return tired_error_set(error, TIRED_INTERNAL, "fingerprint-format",
                               "Cannot format file fingerprint.", 0);
    bool ok = tired_text_set(output, buffer, (size_t)length, sizeof(buffer), error);
    if (ok)
        tired_error_clear(error);
    return ok;
}
