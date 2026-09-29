#include "tired/executable_evidence.h"
#include "tired/json.h"
#include <assert.h>
#include <string.h>
_Static_assert((dev_t)-1 > 0 && sizeof(dev_t) <= sizeof(uint64_t), "Unsigned device identity");
_Static_assert((ino_t)-1 > 0 && sizeof(ino_t) <= sizeof(uint64_t), "Unsigned inode identity");
static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "executable-evidence",
                           "Invalid stored executable identity.", 0);
}
static bool valid_path(const TiredText *path, TiredError *error)
{
    return path->data != NULL && path->length > 0 && path->length <= 4096 && path->data[0] == '/' &&
           tired_validate_text(path->data, path->length, true, error);
}
void tired_executable_evidence_destroy(TiredExecutableEvidence *evidence)
{
    if (evidence == NULL)
        return;
    tired_text_destroy(&evidence->lexical_path);
    tired_text_destroy(&evidence->resolved_path);
    *evidence = (TiredExecutableEvidence){0};
}
bool tired_executable_evidence_capture(const TiredInvocation *invocation,
                                       TiredExecutableEvidence *output, TiredError *error)
{
    assert(invocation != NULL && output != NULL);
    if (!valid_path(&invocation->executable, error) ||
        !valid_path(&invocation->resolved_target, error))
        return invalid(error);
    TiredExecutableEvidence evidence = {.device = invocation->device, .inode = invocation->inode};
    if (!tired_text_set(&evidence.lexical_path, invocation->executable.data,
                        invocation->executable.length, 4096, error) ||
        !tired_text_set(&evidence.resolved_path, invocation->resolved_target.data,
                        invocation->resolved_target.length, 4096, error))
    {
        tired_executable_evidence_destroy(&evidence);
        return false;
    }
    tired_executable_evidence_destroy(output);
    *output = evidence;
    tired_error_clear(error);
    return true;
}
static bool add(struct json_object *object, const char *key, struct json_object *value)
{
    if (value == NULL)
        return false;
    if (json_object_object_add(object, key, value) == 0)
        return true;
    json_object_put(value);
    return false;
}
bool tired_executable_evidence_encode(const TiredExecutableEvidence *evidence, TiredText *output,
                                      TiredError *error)
{
    assert(evidence != NULL && output != NULL);
    if (!valid_path(&evidence->lexical_path, error) || !valid_path(&evidence->resolved_path, error))
        return invalid(error);
    struct json_object *object = json_object_new_object();
    bool ok = false;
    if (object == NULL || !add(object, "schema_version", json_object_new_int(1)) ||
        !add(object, "lexical_path",
             json_object_new_string_len(evidence->lexical_path.data,
                                        (int)evidence->lexical_path.length)) ||
        !add(object, "resolved_path",
             json_object_new_string_len(evidence->resolved_path.data,
                                        (int)evidence->resolved_path.length)) ||
        !add(object, "device", json_object_new_uint64(evidence->device)) ||
        !add(object, "inode", json_object_new_uint64(evidence->inode)))
        goto allocation;
    const char *bytes = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    if (bytes == NULL)
        goto allocation;
    ok = tired_text_set(output, bytes, strlen(bytes), 65536, error);
    goto done;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot encode executable evidence.", 0);
done:
    json_object_put(object);
    return ok;
}
bool tired_executable_evidence_parse(const char *data, size_t length,
                                     TiredExecutableEvidence *output, TiredError *error)
{
    assert(output != NULL);
    struct json_object *object = NULL, *version = NULL, *lexical = NULL, *resolved = NULL,
                       *device = NULL, *inode = NULL;
    TiredExecutableEvidence evidence = {0};
    uint64_t schema, dev, ino;
    bool ok = false;
    if (!tired_json_parse(data, length, 65536, &object, error))
        goto done;
    if (!json_object_is_type(object, json_type_object) || json_object_object_length(object) != 5 ||
        !json_object_object_get_ex(object, "schema_version", &version) ||
        !tired_json_u64(version, 1, 1, &schema, error) ||
        !json_object_object_get_ex(object, "lexical_path", &lexical) ||
        !json_object_is_type(lexical, json_type_string) ||
        !json_object_object_get_ex(object, "resolved_path", &resolved) ||
        !json_object_is_type(resolved, json_type_string) ||
        !json_object_object_get_ex(object, "device", &device) ||
        !tired_json_u64(device, 0, (uint64_t)(dev_t)-1, &dev, error) ||
        !json_object_object_get_ex(object, "inode", &inode) ||
        !tired_json_u64(inode, 0, (uint64_t)(ino_t)-1, &ino, error))
        goto bad;
    if (!tired_text_set(&evidence.lexical_path, json_object_get_string(lexical),
                        (size_t)json_object_get_string_len(lexical), 4096, error) ||
        !tired_text_set(&evidence.resolved_path, json_object_get_string(resolved),
                        (size_t)json_object_get_string_len(resolved), 4096, error))
        goto done;
    if (!valid_path(&evidence.lexical_path, error) || !valid_path(&evidence.resolved_path, error))
        goto bad;
    evidence.device = (dev_t)dev;
    evidence.inode = (ino_t)ino;
    tired_executable_evidence_destroy(output);
    *output = evidence;
    evidence = (TiredExecutableEvidence){0};
    tired_error_clear(error);
    ok = true;
    goto done;
bad:
    invalid(error);
done:
    json_object_put(object);
    tired_executable_evidence_destroy(&evidence);
    return ok;
}
