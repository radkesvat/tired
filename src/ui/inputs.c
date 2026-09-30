#define _XOPEN_SOURCE_EXTENDED 1
#include "tired/encode.h"
#include "tired/ui.h"
#include <curses.h>
#include <stdio.h>
#include <string.h>
void tired_ui_inputs(TiredMutation *mutation, bool credentials, TiredError *error)
{
    TiredServiceRecord *record = &mutation->proposed;
    size_t selected = 0;
    while (!tired_ui_signal)
    {
        if (!tired_ui_ready(10, 54))
        {
            if (tired_ui_key(-1) == 27 || tired_ui_signal)
                return;
            continue;
        }
        size_t count = credentials ? record->credentials.count : record->environment.count;
        if (selected >= count && count != 0)
            selected = count - 1;
        erase();
        tired_ui_draw(
            1, 2,
            credentials ? "Credentials: references only; source contents are never read here"
                        : "Environment: explicit assignments; values persist in a private revision",
            COLS - 4);
        size_t visible = LINES > 9 ? (size_t)(LINES - 8) : 1;
        size_t first = selected >= visible ? selected - visible + 1 : 0;
        for (size_t i = first; i < count && i - first < visible; ++i)
        {
            if (i == selected)
                attron(A_REVERSE);
            const char *name = credentials ? record->credentials.items[i].name.data
                                           : record->environment.items[i].name.data;
            const char *value = credentials
                                    ? record->credentials.items[i].path.data
                                    : tired_environment_display(&record->environment.items[i]);
            tired_ui_draw((int)(i - first) + 3, 2, name, 24);
            tired_ui_draw((int)(i - first) + 3, 28, value, COLS - 30);
            if (i == selected)
                attroff(A_REVERSE);
        }
        tired_ui_draw(
            LINES - 3, 2,
            credentials
                ? "LoadCredential supplies a private file through CREDENTIALS_DIRECTORY."
                : "External EnvironmentFile references are edited on the dependencies page.",
            COLS - 4);
        tired_ui_draw(LINES - 2, 2,
                      "a add  Enter replace  d delete  s classify secret  v reveal  Esc return",
                      COLS - 4);
        refresh();
        int key = tired_ui_key(-1);
        if (key == 27)
            return;
        if (key == KEY_UP && selected != 0)
            --selected;
        if (key == KEY_DOWN && selected + 1 < count)
            ++selected;
        if (key == 's' && !credentials && count != 0)
            record->environment.items[selected].sensitive = true;
        if (key == 'v' && !credentials && count != 0)
            tired_ui_text_view("Explicit temporary reveal; Escape returns",
                               record->environment.items[selected].value.data);
        if (key == 'd' && count != 0)
        {
            if (credentials)
            {
                TiredCredential *item = &record->credentials.items[selected];
                record->credentials.bytes -= item->name.length + item->path.length + 2;
                tired_text_destroy(&item->name);
                tired_text_destroy(&item->path);
                memmove(item, item + 1, (count - selected - 1) * sizeof(*item));
                --record->credentials.count;
            }
            else
            {
                TiredEnvironmentEntry *item = &record->environment.items[selected];
                record->environment.bytes -= item->name.length + item->value.length + 2;
                tired_text_destroy(&item->name);
                tired_text_destroy(&item->value);
                memmove(item, item + 1, (count - selected - 1) * sizeof(*item));
                --record->environment.count;
            }
        }
        if (key == 'a' || ((key == '\n' || key == KEY_ENTER) && count != 0))
        {
            bool replacing = key != 'a';
            TiredText assignment = {0}, initial = {0};
            TiredBuffer buffer;
            tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
            if (replacing)
            {
                const TiredText *name = credentials ? &record->credentials.items[selected].name
                                                    : &record->environment.items[selected].name;
                (void)tired_buffer_append(&buffer, name->data, name->length, error);
                (void)tired_buffer_append(&buffer, "=", 1, error);
                if (credentials)
                    (void)tired_buffer_append(
                        &buffer, record->credentials.items[selected].path.data,
                        record->credentials.items[selected].path.length, error);
                else if (!record->environment.items[selected].sensitive)
                    (void)tired_buffer_append(
                        &buffer, record->environment.items[selected].value.data,
                        record->environment.items[selected].value.length, error);
                (void)tired_buffer_take(&buffer, &initial, error);
            }
            bool entered = tired_ui_text_prompt(
                credentials ? "NAME=/absolute/source/path"
                            : "NAME=VALUE (empty values allowed; secret replacement starts blank)",
                initial.data, &assignment, error);
            if (entered && credentials)
            {
                TiredCredentials parsed = {0};
                if (tired_credentials_add(&parsed, assignment.data, assignment.length, error))
                {
                    bool duplicate = false;
                    for (size_t i = 0; i < count; ++i)
                        duplicate |= (!replacing || i != selected) &&
                                     strcmp(record->credentials.items[i].name.data,
                                            parsed.items[0].name.data) == 0;
                    if (duplicate)
                        tired_error_set(error, TIRED_INVALID, "credential-duplicate",
                                        "Credential name already exists.", 0);
                    else if (replacing)
                    {
                        TiredCredential *old = &record->credentials.items[selected];
                        size_t next = record->credentials.bytes - old->name.length -
                                      old->path.length - 2 + parsed.bytes;
                        if (next > TIRED_INPUT_LIMIT)
                            tired_error_set(error, TIRED_INVALID, "credential-limit",
                                            "Credential references exceed their byte limit.", 0);
                        else
                        {
                            tired_text_destroy(&old->name);
                            tired_text_destroy(&old->path);
                            *old = parsed.items[0];
                            parsed.items[0] = (TiredCredential){0};
                            record->credentials.bytes = next;
                        }
                    }
                    else
                        (void)tired_credentials_add(&record->credentials, assignment.data,
                                                    assignment.length, error);
                }
                tired_credentials_destroy(&parsed);
            }
            else if (entered)
            {
                const char *equals = memchr(assignment.data, '=', assignment.length);
                const TiredText *name =
                    replacing ? &record->environment.items[selected].name : NULL;
                if (replacing &&
                    (equals == NULL || (size_t)(equals - assignment.data) != name->length ||
                     memcmp(assignment.data, name->data, name->length) != 0))
                    tired_error_set(error, TIRED_INVALID, "environment-rename",
                                    "Replacement keeps the variable name. Add a new name and "
                                    "delete the old entry explicitly.",
                                    0);
                else
                    (void)tired_environment_set(
                        &record->environment, assignment.data, assignment.length, TIRED_ENV_EDITED,
                        replacing && record->environment.items[selected].sensitive, error);
            }
            tired_text_destroy(&assignment);
            tired_text_destroy(&initial);
            tired_buffer_destroy(&buffer);
            if (error->status != TIRED_OK)
            {
                tired_ui_text_view("Invalid input", error->message);
                tired_error_clear(error);
            }
        }
        record->environment_loaded = true;
    }
}
