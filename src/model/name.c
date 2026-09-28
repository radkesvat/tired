#include "tired/name.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool ascii_alnum(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

static bool is_python(const char *name)
{
    if (strncmp(name, "python", 6) != 0)
        return false;
    name += 6;
    if (*name == '\0')
        return true;
    if (*name != '2' && *name != '3')
        return false;
    ++name;
    if (*name == '\0')
        return true;
    if (*name++ != '.' || *name == '\0')
        return false;
    for (; *name != '\0'; ++name)
        if (*name < '0' || *name > '9')
            return false;
    return true;
}

static bool is_shell(const char *name)
{
    return strcmp(name, "sh") == 0 || strcmp(name, "bash") == 0 || strcmp(name, "dash") == 0 ||
           strcmp(name, "zsh") == 0 || strcmp(name, "ksh") == 0;
}

static bool is_wrapper(const char *name)
{
    return strcmp(name, "env") == 0 || strcmp(name, "sudo") == 0 || strcmp(name, "nohup") == 0 ||
           strcmp(name, "tmux") == 0;
}

bool tired_name_suggest(const TiredTextList *argv, TiredText *name, TiredNameBasis *basis,
                        TiredError *error)
{
    assert(argv != NULL && name != NULL && basis != NULL);
    if (argv->count == 0 || argv->items[0].length == 0)
        return tired_error_set(error, TIRED_INVALID, "missing-command",
                               "A command is required for naming.", 0);
    const char *executable = basename_of(argv->items[0].data);
    const char *hint = executable;
    TiredNameBasis selected = TIRED_NAME_EXECUTABLE;
    bool python = is_python(executable);
    bool shell = is_shell(executable);
    if (is_wrapper(executable) || shell)
        selected = TIRED_NAME_WRAPPER;
    if (argv->count >= 2 && (python || shell || strcmp(executable, "node") == 0) &&
        argv->items[1].length != 0 && argv->items[1].data[0] != '-')
    {
        hint = basename_of(argv->items[1].data);
        selected = TIRED_NAME_SCRIPT;
    }
    else if (argv->count >= 3 && python && strcmp(argv->items[1].data, "-m") == 0 &&
             argv->items[2].length != 0 && argv->items[2].data[0] != '-')
    {
        hint = argv->items[2].data;
        selected = TIRED_NAME_MODULE;
    }
    else if (argv->count >= 3 && strcmp(executable, "java") == 0 &&
             strcmp(argv->items[1].data, "-jar") == 0 && argv->items[2].length != 0 &&
             argv->items[2].data[0] != '-')
    {
        hint = basename_of(argv->items[2].data);
        selected = TIRED_NAME_SCRIPT;
    }
    size_t hint_length = strlen(hint);
    if (selected == TIRED_NAME_SCRIPT)
    {
        const char *extension = strrchr(hint, '.');
        if (extension != NULL && extension != hint)
            hint_length = (size_t)(extension - hint);
    }
    char result[TIRED_AUTO_NAME_LIMIT + 1];
    size_t length = 0;
    bool pending_separator = false;
    for (size_t i = 0; i < hint_length && length < TIRED_AUTO_NAME_LIMIT; ++i)
    {
        unsigned char c = (unsigned char)hint[i];
        if (c >= 'A' && c <= 'Z')
            c = (unsigned char)(c - 'A' + 'a');
        bool valid =
            ascii_alnum(c) || c == '_' || c == '-' || (c == '.' && selected != TIRED_NAME_MODULE);
        /* Collapse runs of unsupported bytes and repeated dots. */
        if (!valid || (c == '.' && length != 0 && result[length - 1] == '.'))
        {
            pending_separator = length != 0;
            continue;
        }
        if (length == 0 && !ascii_alnum(c))
            continue;
        if (pending_separator)
        {
            result[length++] = '-';
            pending_separator = false;
            if (length == TIRED_AUTO_NAME_LIMIT)
                break;
        }
        result[length++] = (char)c;
    }
    while (length != 0 && !ascii_alnum((unsigned char)result[length - 1]))
        --length;
    if (length == 0)
    {
        memcpy(result, "service", 7);
        length = 7;
    }
    if (!tired_text_set(name, result, length, TIRED_AUTO_NAME_LIMIT, error))
        return false;
    *basis = selected;
    return true;
}

static bool validate_base(const char *input, size_t length, TiredError *error)
{
    assert(input != NULL || length == 0);
    if (length == 0 || length > TIRED_EXPLICIT_NAME_LIMIT ||
        !ascii_alnum((unsigned char)input[0]) || !ascii_alnum((unsigned char)input[length - 1]))
        return tired_error_set(error, TIRED_INVALID, "service-name",
                               "Service name must start and end with an ASCII letter or digit and "
                               "contain at most 200 bytes.",
                               0);
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char c = (unsigned char)input[i];
        if ((!ascii_alnum(c) && c != '.' && c != '_' && c != '-') ||
            (c == '.' && i != 0 && input[i - 1] == '.'))
            return tired_error_set(
                error, TIRED_INVALID, "service-name",
                "Service name contains an unsupported character or adjacent dots.", 0);
    }
    return true;
}

bool tired_name_explicit(const char *input, size_t length, TiredText *name, TiredError *error)
{
    assert(input != NULL || length == 0);
    assert(name != NULL);
    if (length >= 8 && memcmp(input + length - 8, ".service", 8) == 0)
        length -= 8;
    if (!validate_base(input, length, error))
        return false;
    return tired_text_set(name, input, length, TIRED_EXPLICIT_NAME_LIMIT, error);
}

bool tired_name_candidate(const TiredText *base, uint64_t ordinal, TiredText *unit_name,
                          TiredError *error)
{
    assert(base != NULL && unit_name != NULL);
    if (ordinal == 0)
        return tired_error_set(error, TIRED_INVALID, "name-ordinal",
                               "Name ordinal must be positive.", 0);
    if (!validate_base(base->data, base->length, error))
        return false;
    char result[TIRED_EXPLICIT_NAME_LIMIT + 32];
    int count;
    if (ordinal == 1)
        count = snprintf(result, sizeof(result), "%s.service", base->data);
    else
        count = snprintf(result, sizeof(result), "%s-%" PRIu64 ".service", base->data, ordinal);
    if (count < 0 || (size_t)count >= sizeof(result))
        return tired_error_set(error, TIRED_INTERNAL, "name-format", "Cannot format service name.",
                               0);
    return tired_text_set(unit_name, result, (size_t)count, 255, error);
}
