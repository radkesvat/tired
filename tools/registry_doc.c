#include "tired/model.h"
#include <stdio.h>

static void choices(const char *text)
{
    if (text == NULL)
    {
        fputs("—", stdout);
        return;
    }
    for (; *text; ++text)
    {
        if (*text == '|')
            fputc('\\', stdout);
        fputc(*text, stdout);
    }
}

int main(void)
{
    static const char *const kinds[] = {"text",       "choice",     "boolean", "integer",
                                        "duration",   "timeout",    "list",    "limit",
                                        "octal mode", "percentage", "signal"};
    puts("# Complete field reference\n\n"
         "These are the shared CLI/TUI registry fields. Use `--set FIELD=VALUE` or\n"
         "`--unset FIELD`; selected fields still require semantic and live validation.\n"
         "Lists accept one literal member per repeated assignment. Registry defaults\n"
         "precede configured/profile/user choices; capture supplies identity, executable,\n"
         "arguments, directory and name. See [model rules](model.md) for exact grammar,\n"
         "bounds, retry resolution, scope and field combinations.\n\n"
         "| Field | Type | Directive | Registry default | Choices |\n"
         "|---|---|---|---|---|");
    for (unsigned i = 0; i < TIRED_FIELD_COUNT; ++i)
    {
        const TiredField *field = tired_field_get((TiredFieldId)i);
        if (field == NULL || (unsigned)field->kind >= sizeof(kinds) / sizeof(kinds[0]))
            return 1;
        printf("| `%s` | %s | %s | %s | ", field->name, kinds[field->kind],
               field->directive == NULL ? "control-plane choice" : field->directive,
               field->default_value == NULL ? "inherit/capture" : field->default_value);
        choices(field->choices);
        puts(" |");
    }
    return ferror(stdout) ? 1 : 0;
}
