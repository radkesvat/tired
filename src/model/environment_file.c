#include "tired/capture.h"
#include "tired/environment.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool horizontal(char c) { return c == ' ' || c == '\t' || c == '\r'; }

static bool clone_environment(const TiredEnvironment *source, TiredEnvironment *result,
                              char *scratch, TiredError *error)
{
    for (size_t i = 0; i < source->count; ++i)
    {
        const TiredEnvironmentEntry *entry = &source->items[i];
        size_t size = entry->name.length + 1 + entry->value.length;
        memcpy(scratch, entry->name.data, entry->name.length);
        scratch[entry->name.length] = '=';
        memcpy(scratch + entry->name.length + 1, entry->value.data, entry->value.length);
        if (!tired_environment_set(result, scratch, size, entry->origin, entry->sensitive, error))
            return false;
    }
    return true;
}

bool tired_environment_import(TiredEnvironment *environment, const char *data, size_t length,
                              TiredError *error)
{
    assert(environment != NULL && (data != NULL || length == 0));
    if (length > TIRED_ENVIRONMENT_FILE_LIMIT)
        return tired_error_set(error, TIRED_INVALID, "environment-file-limit",
                               "Environment file exceeds 2 MiB.", 0);
    if (!tired_validate_text(data, length, false, error))
        return false;
    size_t capacity = length > TIRED_INPUT_LIMIT ? length : TIRED_INPUT_LIMIT;
    char *assignment = malloc(capacity + 1);
    if (assignment == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot allocate environment parser buffer.", errno);
    TiredEnvironment staged = {0};
    if (!clone_environment(environment, &staged, assignment, error))
        goto fail;
    size_t cursor = 0;
    while (cursor < length)
    {
        while (cursor < length && (horizontal(data[cursor]) || data[cursor] == '\n'))
            ++cursor;
        if (cursor == length)
            break;
        if (data[cursor] == '#' || data[cursor] == ';')
        {
            while (cursor < length)
            {
                char c = data[cursor++];
                if (c == '\\' && cursor < length)
                    ++cursor;
                else if (c == '\n')
                    break;
            }
            continue;
        }
        size_t start = cursor;
        while (cursor < length && data[cursor] != '=' && data[cursor] != '\n')
            ++cursor;
        if (cursor == length || data[cursor] != '=')
            goto syntax;
        size_t name_end = cursor;
        while (name_end > start && horizontal(data[name_end - 1]))
            --name_end;
        size_t used = name_end - start;
        memcpy(assignment, data + start, used);
        assignment[used++] = '=';
        ++cursor;
        while (cursor < length && horizontal(data[cursor]))
            ++cursor;
        char quote =
            cursor < length && (data[cursor] == '\'' || data[cursor] == '"') ? data[cursor++] : 0;
        if (quote != 0)
        {
            bool closed = false;
            while (cursor < length)
            {
                char c = data[cursor++];
                if (c == quote)
                {
                    closed = true;
                    break;
                }
                if (quote == '"' && c == '\\')
                {
                    if (cursor == length)
                        goto syntax;
                    c = data[cursor++];
                    if (c == '\n')
                        continue;
                    if (c != '"' && c != '\\' && c != '$' && c != '`')
                        assignment[used++] = '\\';
                }
                assignment[used++] = c;
            }
            if (!closed)
                goto syntax;
            while (cursor < length && horizontal(data[cursor]))
                ++cursor;
            if (cursor < length && data[cursor] != '\n')
                goto syntax;
        }
        else
        {
            size_t retained = used;
            while (cursor < length && data[cursor] != '\n')
            {
                char c = data[cursor++];
                if (c == '\\')
                {
                    if (cursor == length)
                        goto syntax;
                    c = data[cursor++];
                    if (c == '\n')
                        continue;
                    assignment[used++] = c;
                    retained = used;
                }
                else
                {
                    assignment[used++] = c;
                    if (!horizontal(c))
                        retained = used;
                }
            }
            used = retained;
        }
        if (!tired_environment_set(&staged, assignment, used, TIRED_ENV_IMPORTED, false, error))
            goto fail;
        if (cursor < length)
            ++cursor;
    }
    free(assignment);
    tired_environment_destroy(environment);
    *environment = staged;
    tired_error_clear(error);
    return true;
syntax:
    tired_error_set(error, TIRED_INVALID, "environment-file-syntax",
                    "Unsupported or incomplete environment-file assignment.", 0);
fail:
    free(assignment);
    tired_environment_destroy(&staged);
    return false;
}

static int compare_entries(const void *a, const void *b)
{
    const TiredEnvironmentEntry *const *left = a;
    const TiredEnvironmentEntry *const *right = b;
    return strcmp((*left)->name.data, (*right)->name.data);
}

bool tired_environment_encode(const TiredEnvironment *environment, TiredText *output,
                              TiredError *error)
{
    assert(environment != NULL && output != NULL);
    size_t size = 0;
    for (size_t i = 0; i < environment->count; ++i)
    {
        const TiredEnvironmentEntry *entry = &environment->items[i];
        size += entry->name.length + 4;
        for (size_t j = 0; j < entry->value.length; ++j)
        {
            char c = entry->value.data[j];
            size += (c == '\\' || c == '"' || c == '$' || c == '`') ? 2 : 1;
        }
    }
    if (size > 2 * TIRED_INPUT_LIMIT || environment->count > TIRED_ENVIRONMENT_COUNT_LIMIT)
        return tired_error_set(error, TIRED_INVALID, "environment-file-limit",
                               "Encoded environment exceeds its limit.", 0);
    char *encoded = malloc(size + 1);
    if (encoded == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot allocate environment output.", errno);
    const TiredEnvironmentEntry **order = NULL;
    if (environment->count != 0)
    {
        order = malloc(environment->count * sizeof(*order));
        if (order == NULL)
        {
            int saved_errno = errno;
            free(encoded);
            return tired_error_set(error, TIRED_INTERNAL, "allocation",
                                   "Cannot order environment output.", saved_errno);
        }
        for (size_t i = 0; i < environment->count; ++i)
            order[i] = &environment->items[i];
        qsort(order, environment->count, sizeof(*order), compare_entries);
    }
    size_t used = 0;
    for (size_t i = 0; i < environment->count; ++i)
    {
        const TiredEnvironmentEntry *entry = order[i];
        memcpy(encoded + used, entry->name.data, entry->name.length);
        used += entry->name.length;
        encoded[used++] = '=';
        encoded[used++] = '"';
        for (size_t j = 0; j < entry->value.length; ++j)
        {
            char c = entry->value.data[j];
            if (c == '\\' || c == '"' || c == '$' || c == '`')
                encoded[used++] = '\\';
            encoded[used++] = c;
        }
        encoded[used++] = '"';
        encoded[used++] = '\n';
    }
    encoded[used] = '\0';
    free(order);
    tired_text_destroy(output);
    *output = (TiredText){encoded, used};
    tired_error_clear(error);
    return true;
}
