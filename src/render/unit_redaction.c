#include "tired/unit_redaction.h"
#include "tired/encode.h"
#include "tired/memory.h"
#include "tired/unit_document.h"
#include "tired/unit_words.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool command(const char *key)
{
    const char *keys[] = {"ExecCondition", "ExecStartPre", "ExecStart",   "ExecStartPost",
                          "ExecReload",    "ExecStop",     "ExecStopPost"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (strcmp(key, keys[i]) == 0)
            return true;
    return false;
}
static TiredText payload(TiredText text)
{
    const char *equal = memchr(text.data, '=', text.length);
    if (equal != NULL)
    {
        size_t skip = (size_t)(equal - text.data) + 1;
        text.data += skip;
        text.length -= skip;
    }
    return text;
}
static int compare(const void *left, const void *right)
{
    const TiredText *a = left, *b = right;
    return a->length < b->length   ? -1
           : a->length > b->length ? 1
                                   : memcmp(a->data, b->data, a->length);
}
static bool saved_match(const TiredText *word, const TiredText *secrets, size_t count)
{
    if (count == 0)
        return false;
    TiredText value = payload(*word);
    return (word->length != 0 &&
            bsearch(word, secrets, count, sizeof(*secrets), compare) != NULL) ||
           (value.length != 0 &&
            bsearch(&value, secrets, count, sizeof(*secrets), compare) != NULL);
}
static bool environment_mask(const TiredText *word, const TiredEnvironment *environment, bool *mask,
                             TiredError *error)
{
    const char *equal = memchr(word->data, '=', word->length);
    size_t name_length = equal == NULL ? 0 : (size_t)(equal - word->data);
    if (name_length == 0 || name_length > 255)
        return tired_error_set(error, TIRED_INVALID, "unit-environment-syntax",
                               "Cannot safely classify an inline environment assignment.", 0);
    for (size_t i = 0; i < name_length; ++i)
    {
        char c = word->data[i];
        if (!(c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (i != 0 && c >= '0' && c <= '9')))
            return tired_error_set(error, TIRED_INVALID, "unit-environment-syntax",
                                   "Cannot safely classify an inline environment assignment.", 0);
    }
    const TiredEnvironmentEntry *entry =
        environment == NULL ? NULL : tired_environment_find(environment, word->data, name_length);
    *mask = tired_environment_name_sensitive(word->data, name_length) ||
            (entry != NULL && entry->sensitive);
    return true;
}
static bool replace(TiredBuffer *buffer, const char *data, size_t *copied, size_t start, size_t end,
                    TiredError *error)
{
    assert(start >= *copied && end > start);
    if (!tired_buffer_append(buffer, data + *copied, start - *copied, error) ||
        !tired_buffer_append(buffer, "\"[redacted]\"", 12, error))
        return false;
    *copied = end;
    return true;
}
bool tired_unit_redact(const char *data, size_t length, const TiredTextList *saved_argv,
                       const bool *classified, const TiredEnvironment *environment,
                       TiredText *output, TiredRedaction *redaction, TiredError *error)
{
    assert((data != NULL || length == 0) && output != NULL && redaction != NULL);
    TiredUnitDocument document = {0};
    TiredUnitWords words = {0};
    TiredBuffer buffer, labeled;
    tired_buffer_init(&buffer, 2U * TIRED_UNIT_LIMIT);
    tired_buffer_init(&labeled, 2U * TIRED_UNIT_LIMIT);
    bool saved_masks[TIRED_ARGUMENT_LIMIT] = {0};
    TiredText *secrets = NULL;
    size_t secret_count = 0;
    TiredRedaction result = {0};
    size_t copied = 0;
    bool ok = false;
    if (!tired_unit_document_parse(data, length, &document, error))
        goto done;
    if (saved_argv != NULL)
        result.sensitive = tired_argv_classify(saved_argv, classified, saved_masks);
    assert(environment == NULL || environment->count <= TIRED_ENVIRONMENT_COUNT_LIMIT);
    size_t capacity = (saved_argv == NULL ? 0 : saved_argv->count * 2) +
                      (environment == NULL ? 0 : environment->count);
    if (capacity != 0)
    {
        secrets = malloc(capacity * sizeof(*secrets));
        if (secrets == NULL)
        {
            tired_error_set(error, TIRED_INTERNAL, "allocation",
                            "Cannot allocate unit redaction index.", 0);
            goto done;
        }
    }
    if (saved_argv != NULL)
        for (size_t i = 1; i < saved_argv->count; ++i)
            if (saved_masks[i])
            {
                TiredText value = payload(saved_argv->items[i]);
                if (saved_argv->items[i].length != 0)
                    secrets[secret_count++] = saved_argv->items[i];
                if (value.length != 0 && value.length != saved_argv->items[i].length)
                    secrets[secret_count++] = value;
            }
    if (environment != NULL)
        for (size_t i = 0; i < environment->count; ++i)
            if (environment->items[i].sensitive && environment->items[i].value.length != 0)
                secrets[secret_count++] = environment->items[i].value;
    if (secret_count != 0)
        qsort(secrets, secret_count, sizeof(*secrets), compare);
    for (size_t i = 0; i < document.count; ++i)
    {
        const TiredUnitAssignment *assignment = &document.assignments[i];
        if (strcmp(assignment->section.data, "Service") != 0 || assignment->value.length == 0)
            continue;
        const char *key = assignment->key.data;
        if (strcmp(key, "SetCredential") == 0 || strcmp(key, "SetCredentialEncrypted") == 0)
        {
            result.sensitive = result.redacted = true;
            if (!replace(&buffer, data, &copied, assignment->value_offsets[0],
                         assignment->value_offsets[assignment->value.length - 1] + 1, error))
                goto done;
            continue;
        }
        bool exec = command(key), env = strcmp(key, "Environment") == 0;
        if (!exec && !env)
            continue;
        if (!tired_unit_words_parse(assignment->value.data, assignment->value.length, &words,
                                    error))
            goto done;
        TiredText arguments[TIRED_ARGUMENT_LIMIT];
        for (size_t j = 0; j < words.count; ++j)
        {
            /* Collapse escaped literal percent signs for comparison with captured
             * argv. Other specifiers remain literal; nothing is expanded. */
            TiredText *value = &words.words[j].value;
            size_t to = 0;
            for (size_t from = 0; from < value->length; ++from)
            {
                value->data[to++] = value->data[from];
                if (value->data[from] == '%' && from + 1 < value->length &&
                    value->data[from + 1] == '%')
                    ++from;
            }
            tired_memory_clear(value->data + to, value->length - to);
            value->length = to;
            value->data[to] = '\0';
            arguments[j] = *value;
        }
        TiredTextList list = {.items = arguments, .count = words.count};
        bool masks[TIRED_ARGUMENT_LIMIT] = {0};
        if (exec)
            result.sensitive |= tired_argv_classify(
                &list, strcmp(key, "ExecStart") == 0 ? classified : NULL, masks);
        for (size_t j = 0; j < words.count; ++j)
        {
            if (env && !environment_mask(&arguments[j], environment, &masks[j], error))
                goto done;
            if ((env || j != 0) && saved_match(&arguments[j], secrets, secret_count))
                masks[j] = true;
            if (!masks[j])
                continue;
            const TiredUnitWord *word = &words.words[j];
            result.sensitive = result.redacted = true;
            if (!replace(&buffer, data, &copied, assignment->value_offsets[word->start],
                         assignment->value_offsets[word->end - 1] + 1, error))
                goto done;
        }
        tired_unit_words_destroy(&words);
    }
    if (length != 0 && !tired_buffer_append(&buffer, data + copied, length - copied, error))
        goto done;
    if (result.redacted)
    {
        const char label[] = "# Redacted, non-installable view; classified values are masked.\n";
        if (!tired_buffer_append(&labeled, label, sizeof(label) - 1, error) ||
            !tired_buffer_append(&labeled, buffer.data, buffer.length, error) ||
            !tired_buffer_take(&labeled, output, error))
            goto done;
    }
    else if (!tired_buffer_take(&buffer, output, error))
        goto done;
    *redaction = result;
    tired_error_clear(error);
    ok = true;
done:
    free(secrets);
    if (buffer.data != NULL)
        tired_memory_clear(buffer.data, buffer.length);
    if (labeled.data != NULL)
        tired_memory_clear(labeled.data, labeled.length);
    tired_buffer_destroy(&buffer);
    tired_buffer_destroy(&labeled);
    tired_unit_words_destroy(&words);
    tired_unit_document_destroy(&document);
    return ok;
}
