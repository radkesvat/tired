#include "tired/redaction.h"
#include <assert.h>
#include <string.h>

static bool secret_flag(const TiredText *arg)
{
    if (arg->length == 0 || arg->data[0] != '-')
        return false;
    const char *equal = memchr(arg->data, '=', arg->length);
    size_t length = equal == NULL ? arg->length : (size_t)(equal - arg->data);
    static const char *const words[] = {"password", "passwd", "token", "secret", "api-key"};
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
    {
        size_t size = strlen(words[i]);
        for (size_t start = 0; start + size <= length; ++start)
        {
            size_t matched = 0;
            for (; matched < size; ++matched)
            {
                char c = arg->data[start + matched];
                if (c >= 'A' && c <= 'Z')
                    c = (char)(c - 'A' + 'a');
                if (c != words[i][matched])
                    break;
            }
            if (matched == size)
                return true;
        }
    }
    return false;
}

bool tired_spec_display(const TiredServiceSpec *source, const bool *classified,
                        bool include_sensitive, TiredServiceSpec *output, TiredRedaction *redaction,
                        TiredError *error)
{
    assert(source != NULL && output != NULL && redaction != NULL);
    TiredServiceSpec display = {0};
    TiredRedaction result = {0};
    for (unsigned i = 0; i < TIRED_FIELD_COUNT; ++i)
        if (!tired_spec_copy_field(&display, source, (TiredFieldId)i, error))
            goto fail;
    const TiredFieldValue *field = &source->fields[TIRED_FIELD_ARGV];
    if (tired_field_has_value(field))
    {
        const TiredTextList *original = &field->value.list;
        assert(original->count <= TIRED_ARGUMENT_LIMIT);
        if (!tired_spec_clear_list(&display, TIRED_FIELD_ARGV, field->origin, error))
            goto fail;
        bool hide_next = false;
        for (size_t i = 0; i < original->count; ++i)
        {
            const TiredText *arg = &original->items[i];
            bool flag = i != 0 && secret_flag(arg);
            bool attached = flag && memchr(arg->data, '=', arg->length) != NULL;
            bool hide = i != 0 && (hide_next || attached || (classified != NULL && classified[i]));
            result.sensitive |= hide || flag;
            bool mask = hide && !include_sensitive;
            result.redacted |= mask;
            const char *value = mask ? "[redacted]" : arg->data;
            size_t length = mask ? sizeof("[redacted]") - 1 : arg->length;
            if (!tired_spec_append(&display, TIRED_FIELD_ARGV, value, length, field->origin, error))
                goto fail;
            hide_next = flag && !attached;
        }
    }
    tired_spec_destroy(output);
    *output = display;
    *redaction = result;
    tired_error_clear(error);
    return true;
fail:
    tired_spec_destroy(&display);
    return false;
}
