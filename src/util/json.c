#include "tired/json.h"
#include "tired/capture.h"
#include <assert.h>
#include <math.h>
#include <string.h>

#define JSON_DEPTH_LIMIT 32U
#define JSON_NODE_LIMIT 16384U

typedef struct
{
    const char *data;
    size_t size, at, nodes;
    TiredError *error;
} Scan;
static bool invalid(Scan *s)
{
    return tired_error_set(s->error, TIRED_INVALID, "json-syntax",
                           "Invalid or unsupported JSON syntax.", 0);
}
static void whitespace(Scan *s)
{
    while (s->at < s->size && (s->data[s->at] == ' ' || s->data[s->at] == '\t' ||
                               s->data[s->at] == '\n' || s->data[s->at] == '\r'))
        ++s->at;
}
static bool hex4(Scan *s, uint32_t *value)
{
    if (s->size - s->at < 4)
        return invalid(s);
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        char c = s->data[s->at++];
        unsigned digit;
        if (c >= '0' && c <= '9')
            digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = (unsigned)(c - 'A' + 10);
        else
            return invalid(s);
        result = result * 16 + digit;
    }
    *value = result;
    return true;
}

static bool string(Scan *s, TiredText *decoded)
{
    size_t start = s->at;
    if (s->at == s->size || s->data[s->at++] != '"')
        return invalid(s);
    bool closed = false;
    while (s->at < s->size)
    {
        unsigned char c = (unsigned char)s->data[s->at++];
        if (c == '"')
        {
            closed = true;
            break;
        }
        if (c < 32)
            return invalid(s);
        if (c != '\\')
            continue;
        if (s->at == s->size)
            return invalid(s);
        c = (unsigned char)s->data[s->at++];
        if (c == 'u')
        {
            uint32_t value;
            if (!hex4(s, &value))
                return false;
            if (value == 0 || (value >= 0xdc00 && value <= 0xdfff))
                return invalid(s);
            if (value >= 0xd800 && value <= 0xdbff)
            {
                if (s->size - s->at < 2 || s->data[s->at] != '\\' || s->data[s->at + 1] != 'u')
                    return invalid(s);
                s->at += 2;
                if (!hex4(s, &value))
                    return false;
                if (value < 0xdc00 || value > 0xdfff)
                    return invalid(s);
            }
        }
        else if (c != '"' && c != '\\' && c != '/' && c != 'b' && c != 'f' && c != 'n' &&
                 c != 'r' && c != 't')
            return invalid(s);
    }
    if (!closed)
        return invalid(s);
    if (decoded == NULL)
        return true;
    struct json_tokener *tok = json_tokener_new_ex(2);
    if (tok == NULL)
        return tired_error_set(s->error, TIRED_INTERNAL, "allocation",
                               "Cannot allocate JSON string decoder.", 0);
    json_tokener_set_flags(tok, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    struct json_object *value = json_tokener_parse_ex(tok, s->data + start, (int)(s->at - start));
    bool ok = json_tokener_get_error(tok) == json_tokener_success &&
              json_object_is_type(value, json_type_string);
    if (ok)
        ok = tired_text_set(decoded, json_object_get_string(value),
                            (size_t)json_object_get_string_len(value), TIRED_INPUT_LIMIT, s->error);
    else
        invalid(s);
    json_object_put(value);
    json_tokener_free(tok);
    return ok;
}

static bool value(Scan *s, unsigned depth);
static bool container(Scan *s, unsigned depth, bool object)
{
    ++s->at;
    char close = object ? '}' : ']';
    TiredTextList keys = {0};
    whitespace(s);
    if (s->at < s->size && s->data[s->at] == close)
    {
        ++s->at;
        return true;
    }
    size_t count = 0;
    while (s->at < s->size)
    {
        if (++count > 4096)
        {
            tired_error_set(s->error, TIRED_INVALID, "json-count",
                            "JSON collection exceeds 4096 entries.", 0);
            goto fail;
        }
        if (object)
        {
            TiredText key = {0};
            if (!string(s, &key))
                goto fail;
            bool duplicate = false;
            for (size_t i = 0; i < keys.count; ++i)
                if (keys.items[i].length == key.length &&
                    memcmp(keys.items[i].data, key.data, key.length) == 0)
                    duplicate = true;
            if (duplicate)
            {
                tired_text_destroy(&key);
                tired_error_set(s->error, TIRED_INVALID, "json-duplicate-key",
                                "JSON object contains a duplicate decoded key.", 0);
                goto fail;
            }
            bool ok = tired_text_list_append(&keys, key.data, key.length, 1024, TIRED_INPUT_LIMIT,
                                             s->error);
            tired_text_destroy(&key);
            if (!ok)
                goto fail;
            whitespace(s);
            if (s->at == s->size || s->data[s->at++] != ':')
            {
                invalid(s);
                goto fail;
            }
        }
        if (!value(s, depth + 1))
            goto fail;
        whitespace(s);
        if (s->at < s->size && s->data[s->at] == close)
        {
            ++s->at;
            tired_text_list_destroy(&keys);
            return true;
        }
        if (s->at == s->size || s->data[s->at++] != ',')
        {
            invalid(s);
            goto fail;
        }
        whitespace(s);
    }
    invalid(s);
fail:
    tired_text_list_destroy(&keys);
    return false;
}

static bool number(Scan *s)
{
    size_t start = s->at;
    bool negative = s->data[s->at] == '-';
    if (negative)
        ++s->at;
    size_t digits = s->at;
    while (s->at < s->size && s->data[s->at] >= '0' && s->data[s->at] <= '9')
        ++s->at;
    if (s->at == digits || (s->at - digits > 1 && s->data[digits] == '0'))
        return invalid(s);
    bool fractional = false;
    if (s->at < s->size && s->data[s->at] == '.')
    {
        fractional = true;
        digits = ++s->at;
        while (s->at < s->size && s->data[s->at] >= '0' && s->data[s->at] <= '9')
            ++s->at;
        if (s->at == digits)
            return invalid(s);
    }
    if (s->at < s->size && (s->data[s->at] == 'e' || s->data[s->at] == 'E'))
    {
        fractional = true;
        ++s->at;
        if (s->at < s->size && (s->data[s->at] == '+' || s->data[s->at] == '-'))
            ++s->at;
        digits = s->at;
        while (s->at < s->size && s->data[s->at] >= '0' && s->data[s->at] <= '9')
            ++s->at;
        if (s->at == digits)
            return invalid(s);
    }
    if (!fractional)
    {
        uint64_t unsigned_value;
        int64_t signed_value;
        return negative ? tired_parse_i64(s->data + start, s->at - start, INT64_MIN, INT64_MAX,
                                          &signed_value, s->error)
                        : tired_parse_u64(s->data + start, s->at - start, 0, UINT64_MAX,
                                          &unsigned_value, s->error);
    }
    return true;
}
static bool value(Scan *s, unsigned depth)
{
    if (depth > JSON_DEPTH_LIMIT || ++s->nodes > JSON_NODE_LIMIT)
        return tired_error_set(s->error, TIRED_INVALID, "json-complexity",
                               "JSON exceeds nesting or node limits.", 0);
    whitespace(s);
    if (s->at == s->size)
        return invalid(s);
    char c = s->data[s->at];
    if (c == '{' || c == '[')
        return container(s, depth, c == '{');
    if (c == '"')
        return string(s, NULL);
    if (c == '-' || (c >= '0' && c <= '9'))
        return number(s);
    const char *literals[] = {"true", "false", "null"};
    for (size_t i = 0; i < 3; ++i)
    {
        size_t size = strlen(literals[i]);
        if (s->size - s->at >= size && memcmp(s->data + s->at, literals[i], size) == 0)
        {
            s->at += size;
            return true;
        }
    }
    return invalid(s);
}

static bool finite_values(struct json_object *object)
{
    if (json_object_is_type(object, json_type_double))
        return isfinite(json_object_get_double(object));
    if (json_object_is_type(object, json_type_array))
        for (size_t i = 0; i < json_object_array_length(object); ++i)
            if (!finite_values(json_object_array_get_idx(object, i)))
                return false;
    if (json_object_is_type(object, json_type_object))
    {
        json_object_object_foreach(object, key, child)
        {
            (void)key;
            if (!finite_values(child))
                return false;
        }
    }
    return true;
}

bool tired_json_parse(const char *data, size_t length, size_t byte_limit,
                      struct json_object **output, TiredError *error)
{
    assert(output != NULL && (data != NULL || length == 0));
    if (length > byte_limit || length > TIRED_INPUT_LIMIT)
        return tired_error_set(error, TIRED_INVALID, "json-size",
                               "JSON exceeds its input byte limit.", 0);
    if (!tired_validate_text(data, length, false, error))
        return false;
    Scan scan = {.data = data, .size = length, .error = error};
    if (!value(&scan, 1))
        return false;
    whitespace(&scan);
    if (scan.at != length)
        return invalid(&scan);
    TiredText terminated = {0};
    if (!tired_text_set(&terminated, data, length, byte_limit, error))
        return false;
    struct json_tokener *tok = json_tokener_new_ex(JSON_DEPTH_LIMIT + 1);
    if (tok == NULL)
    {
        tired_text_destroy(&terminated);
        return tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate JSON decoder.",
                               0);
    }
    json_tokener_set_flags(tok, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    struct json_object *parsed = json_tokener_parse_ex(tok, terminated.data, (int)length + 1);
    bool ok = json_tokener_get_error(tok) == json_tokener_success && finite_values(parsed);
    json_tokener_free(tok);
    tired_text_destroy(&terminated);
    if (!ok)
    {
        json_object_put(parsed);
        return invalid(&scan);
    }
    json_object_put(*output);
    *output = parsed;
    tired_error_clear(error);
    return true;
}

bool tired_json_u64(struct json_object *value, uint64_t minimum, uint64_t maximum, uint64_t *output,
                    TiredError *error)
{
    assert(output != NULL && minimum <= maximum);
    if (!json_object_is_type(value, json_type_int) || json_object_get_int64(value) < 0)
        return tired_error_set(error, TIRED_INVALID, "json-integer",
                               "Expected a nonnegative JSON integer.", 0);
    uint64_t number = json_object_get_uint64(value);
    if (number < minimum || number > maximum)
        return tired_error_set(error, TIRED_INVALID, "json-integer-range",
                               "JSON integer is outside the allowed range.", 0);
    *output = number;
    tired_error_clear(error);
    return true;
}
