#define _XOPEN_SOURCE_EXTENDED 1
#include "tired/encode.h"
#include "tired/frontend.h"
#include "tired/model_format.h"
#include "tired/redaction.h"
#include "tired/render.h"
#include "tired/ui.h"
#include <curses.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <wchar.h>
volatile sig_atomic_t tired_ui_signal;
static bool ascii_display;
void tired_ui_presentation(bool ascii) { ascii_display = ascii; }
#define interrupted tired_ui_signal
static void handle_signal(int number) { interrupted = number; }
static bool terminal_resized(void)
{
    struct winsize size;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row != 0 && size.ws_col != 0 &&
        (size.ws_row != LINES || size.ws_col != COLS))
    {
        (void)resizeterm(size.ws_row, size.ws_col);
        return true;
    }
    return false;
}
int tired_ui_key(int timeout_ms)
{
    for (;;)
    {
        if (terminal_resized())
            return KEY_RESIZE;
        if (tired_ui_signal)
            return ERR;
        int delay = wgetdelay(stdscr);
        wtimeout(stdscr, 0);
        int key = wgetch(stdscr);
        wtimeout(stdscr, delay);
        /* Escape remains valid even below the minimum viewport size. */
        if (terminal_resized() && key != 27)
            return KEY_RESIZE;
        if (key != ERR)
            return key;
        struct pollfd input = {.fd = STDIN_FILENO, .events = POLLIN};
        /* SIGWINCH can arrive before poll starts; periodically reconcile the
         * actual terminal dimensions even when no keyboard input is pending. */
        int ready = poll(&input, 1, timeout_ms < 0 ? 250 : timeout_ms);
        if (ready == 0 && timeout_ms < 0)
            continue;
        if (ready <= 0)
            return ERR;
        if (input.revents & (POLLHUP | POLLERR | POLLNVAL))
        {
            tired_ui_signal = SIGHUP;
            return ERR;
        }
    }
}
bool tired_ui_usable(void)
{
    const char *term = getenv("TERM");
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && term != NULL && term[0] != '\0' &&
           strcmp(term, "dumb") != 0;
}
static const char *origin(TiredFieldOrigin value)
{
    static const char *const names[] = {"unset",      "inherit", "default", "configured",
                                        "configured", "profile", "you",     "captured"};
    return (unsigned)value < sizeof(names) / sizeof(names[0]) ? names[value] : "unknown";
}
static void safe_print(int row, int column, const char *text, int maximum)
{
    TiredText safe = {0};
    TiredError ignored = {0};
    if (maximum <= 0 || !tired_encode_display(text, strlen(text), &safe, &ignored))
        return;
    mbstate_t state = {0};
    const char *position = safe.data;
    int cells = 0;
    (void)move(row, column);
    while (*position != '\0')
    {
        wchar_t character;
        size_t length = mbrtowc(&character, position, strlen(position), &state);
        if (length == (size_t)-1 || length == (size_t)-2 || length == 0)
        {
            character = '?';
            length = 1;
            memset(&state, 0, sizeof(state));
        }
        int width = wcwidth(character);
        if (ascii_display && character > 127)
        {
            character = '?';
            width = 1;
        }
        if (width < 0)
            width = 1;
        if (cells + width > maximum)
            break;
        wchar_t pair[2] = {character, L'\0'};
        (void)addnwstr(pair, 1);
        cells += width;
        position += length;
    }
    tired_text_destroy(&safe);
}
bool tired_ui_ready(int minimum_rows, int minimum_columns)
{
    (void)terminal_resized();
    if (LINES >= minimum_rows && COLS >= minimum_columns)
        return true;
    erase();
    char message[96];
    (void)snprintf(message, sizeof(message), "Resize to at least %d x %d; Esc cancels this view.",
                   minimum_columns, minimum_rows);
    safe_print(0, 0, message, COLS);
    refresh();
    return false;
}
static bool field_text(const TiredServiceSpec *spec, TiredFieldId id, const bool *classified,
                       TiredText *output, TiredError *error)
{
    TiredServiceSpec display = {0};
    TiredRedaction redaction = {0};
    TiredText encoded = {0};
    struct json_object *document = NULL, *fields = NULL, *field = NULL, *value = NULL;
    bool ok = tired_spec_display(spec, classified, false, &display, &redaction, error) &&
              tired_spec_encode(&display, &encoded, error) &&
              tired_json_parse(encoded.data, encoded.length, TIRED_SERVICE_RECORD_LIMIT, &document,
                               error);
    if (ok)
        ok = json_object_object_get_ex(document, "fields", &fields) &&
             json_object_object_get_ex(fields, tired_field_get(id)->name, &field) &&
             json_object_object_get_ex(field, "value", &value);
    if (ok && id == TIRED_FIELD_ARGV && json_object_is_type(value, json_type_array))
        ok = json_object_array_put_idx(
                 value, 0,
                 json_object_new_string(spec->fields[TIRED_FIELD_EXECUTABLE].value.text.data)) == 0;
    const char *text = value == NULL ? "inherit"
                       : json_object_is_type(value, json_type_string)
                           ? json_object_get_string(value)
                           : json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN);
    if (ok)
        ok = tired_text_set(output, text, strlen(text), TIRED_INPUT_LIMIT, error);
    json_object_put(document);
    tired_text_destroy(&encoded);
    tired_spec_destroy(&display);
    return ok;
}
static int viewer(const char *title, const char *bytes)
{
    size_t offset = 0, horizontal = 0;
    for (;;)
    {
        if (!tired_ui_ready(10, 40))
        {
            if (tired_ui_key(-1) == 27 || interrupted)
                return 27;
            continue;
        }
        erase();
        attron(A_BOLD);
        safe_print(1, 2, title, COLS - 4);
        attroff(A_BOLD);
        size_t row = 0, position = 0;
        while (bytes[position] != '\0' && row < offset)
            if (bytes[position++] == '\n')
                ++row;
        for (int screen = 3; screen < LINES - 2 && bytes[position] != '\0'; ++screen)
        {
            const char *newline = strchr(bytes + position, '\n');
            size_t length =
                newline == NULL ? strlen(bytes + position) : (size_t)(newline - bytes - position);
            TiredText line = {0};
            TiredError ignored = {0};
            if (tired_text_set(&line, bytes + position, length, TIRED_INPUT_LIMIT, &ignored))
            {
                TiredText escaped = {0};
                if (tired_encode_display(line.data, line.length, &escaped, &ignored))
                {
                    size_t start = horizontal < escaped.length ? horizontal : escaped.length;
                    while (start != 0 && ((unsigned char)escaped.data[start] & 0xc0U) == 0x80U)
                        --start;
                    safe_print(screen, 2, escaped.data + start, COLS - 4);
                }
                tired_text_destroy(&escaped);
            }
            tired_text_destroy(&line);
            position += length + (newline != NULL);
        }
        safe_print(LINES - 1, 2,
                   "Up/Down scroll  Left/Right pan  PgUp/PgDn page  Home reset  Esc return",
                   COLS - 4);
        refresh();
        int key = tired_ui_key(-1);
        if (key == 27 || key == 'q' || key == '\n' || key == ' ' || interrupted)
            return key;
        if (key == KEY_RIGHT && horizontal < TIRED_INPUT_LIMIT)
            horizontal += 8;
        if (key == KEY_LEFT)
            horizontal = horizontal > 8 ? horizontal - 8 : 0;
        if (key == KEY_HOME)
            horizontal = 0;
        if ((key == KEY_DOWN || key == 'j') && bytes[position] != '\0')
            ++offset;
        if ((key == KEY_UP || key == 'k') && offset != 0)
            --offset;
        if (key == KEY_NPAGE && bytes[position] != '\0')
            offset += (size_t)(LINES > 8 ? LINES - 6 : 1);
        if (key == KEY_PPAGE)
            offset = offset > (size_t)LINES ? offset - (size_t)LINES : 0;
    }
}
static bool evidence_line(TiredBuffer *buffer, const char *label, const char *value,
                          TiredError *error)
{
    return tired_buffer_append(buffer, label, strlen(label), error) &&
           tired_buffer_append(buffer, value == NULL ? "unknown" : value,
                               value == NULL ? 7 : strlen(value), error) &&
           tired_buffer_append(buffer, "\n", 1, error);
}
bool tired_ui_field_evidence(const TiredMutation *mutation, TiredFieldId id, TiredText *output,
                             TiredError *error)
{
    const TiredField *field = tired_field_get(id);
    const TiredProfileSnapshot *snapshot = &mutation->proposed.profile;
    TiredBuffer report;
    tired_buffer_init(&report, 4U * TIRED_PROFILE_LIMIT);
    TiredText value = {0};
    const TiredFieldValue *effective = &mutation->proposed.spec.fields[id];
    const char *rationale =
        effective->origin == TIRED_ORIGIN_USER
            ? effective->inherit
                  ? "Explicit user inheritance omits this directive and suppresses advice."
                  : "The explicit user selection takes precedence over defaults and profile advice."
        : effective->origin == TIRED_ORIGIN_CONFIG_ADMIN
            ? "The administrator configuration supplied this service default."
        : effective->origin == TIRED_ORIGIN_CONFIG_USER
            ? "The current account configuration supplied this service default."
        : effective->origin == TIRED_ORIGIN_CAPTURE
            ? "Captured invocation or resolved account facts supplied this value before elevation."
        : effective->origin == TIRED_ORIGIN_PROFILE
            ? "Compatible retained profile advice supplied this value; evidence appears below."
            : "Generic registry/policy defaults supplied this value; optional settings inherit.";
    bool ok =
        field_text(&mutation->proposed.spec, id, mutation->proposed.review.sensitive_arguments,
                   &value, error) &&
        evidence_line(&report, "Field: ", field->name, error) &&
        evidence_line(&report, "Effective value: ", value.data, error) &&
        evidence_line(&report, "Origin: ", origin(effective->origin), error) &&
        evidence_line(&report, "Rationale: ", rationale, error) &&
        evidence_line(&report, "Directive: ", field->directive, error) &&
        evidence_line(&report, "Choices: ", field->choices, error) &&
        evidence_line(&report, "Registry default: ",
                      field->default_value == NULL ? "inherited/captured" : field->default_value,
                      error);
    struct json_object *recommendations = NULL, *sources = NULL;
    if (ok && mutation->proposed.has_profile)
    {
        (void)json_object_object_get_ex(snapshot->profile.document, "recommendations",
                                        &recommendations);
        (void)json_object_object_get_ex(snapshot->profile.document, "sources", &sources);
        bool relevant[128] = {0}, found = false;
        for (size_t i = 0; ok && i < snapshot->profile.count; ++i)
        {
            if (snapshot->profile.recommendations[i].field != id)
                continue;
            found = true;
            struct json_object *rec = json_object_array_get_idx(recommendations, i),
                               *proposed = NULL, *ids = NULL, *conditions = NULL;
            (void)json_object_object_get_ex(rec, "value", &proposed);
            (void)json_object_object_get_ex(rec, "source_ids", &ids);
            (void)json_object_object_get_ex(rec, "conditions", &conditions);
            ok = evidence_line(&report, "\nRecommendation: ",
                               json_object_to_json_string_ext(proposed, JSON_C_TO_STRING_PLAIN),
                               error) &&
                 evidence_line(&report, "Disposition: ",
                               tired_recommendation_disposition_name(snapshot->decisions[i]),
                               error) &&
                 evidence_line(&report, "Reason: ", snapshot->profile.recommendations[i].reason,
                               error) &&
                 evidence_line(&report, "Applicability: ",
                               conditions == NULL ? "unconditional"
                                                  : json_object_to_json_string_ext(
                                                        conditions, JSON_C_TO_STRING_PLAIN),
                               error);
            for (size_t j = 0; j < json_object_array_length(sources); ++j)
            {
                struct json_object *source = json_object_array_get_idx(sources, j),
                                   *source_id = NULL;
                (void)json_object_object_get_ex(source, "id", &source_id);
                for (size_t k = 0; k < json_object_array_length(ids); ++k)
                    relevant[j] |=
                        strcmp(json_object_get_string(source_id),
                               json_object_get_string(json_object_array_get_idx(ids, k))) == 0;
            }
        }
        if (ok && found)
        {
            char revision[32];
            (void)snprintf(revision, sizeof(revision), "%llu",
                           (unsigned long long)snapshot->profile.revision);
            ok = evidence_line(&report, "\nProfile: ", snapshot->profile.id, error) &&
                 evidence_line(&report, "Revision: ", revision, error) &&
                 evidence_line(&report, "Catalog origin: ",
                               snapshot->source_origin == TIRED_PROFILE_BUNDLED ? "bundled"
                               : snapshot->source_origin == TIRED_PROFILE_ADMIN ? "administrator"
                                                                                : "user",
                               error) &&
                 evidence_line(&report, "Content digest: ", snapshot->source_sha256, error) &&
                 evidence_line(&report, "Source path: ", snapshot->source_path.data, error);
            for (size_t j = 0; ok && j < json_object_array_length(sources); ++j)
            {
                if (!relevant[j])
                    continue;
                struct json_object *source = json_object_array_get_idx(sources, j);
                const char *keys[] = {"id", "url", "source_kind", "checked_at"};
                const char *labels[] = {"\nEvidence: ", "URL: ", "Kind: ", "Checked: "};
                for (size_t k = 0; ok && k < sizeof(keys) / sizeof(keys[0]); ++k)
                {
                    struct json_object *item = NULL;
                    (void)json_object_object_get_ex(source, keys[k], &item);
                    ok = evidence_line(&report, labels[k], json_object_get_string(item), error);
                }
            }
        }
    }
    if (ok)
        ok = evidence_line(&report, "\nControls: ",
                           "Enter/Space edit; u inherit; x clear list; Z reset.\n"
                           "C apply; P preview; D differences; S operation summary.\n"
                           "W full profile; F refresh profile; H opt-in hardening.\n"
                           "E environment; K credentials; R risks; M edit apply mode.\n"
                           "1-8 select pages; A all fields; Esc cancels.",
                           error);
    if (ok)
        ok = tired_buffer_take(&report, output, error);
    tired_text_destroy(&value);
    tired_buffer_destroy(&report);
    return ok;
}
static bool prompt(const char *title, const char *initial, TiredText *output, TiredError *error)
{
    TiredBuffer buffer;
    tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
    bool ok = initial == NULL || tired_buffer_append(&buffer, initial, strlen(initial), error);
    bool accepted = false;
    mbstate_t input_state = {0};
    (void)curs_set(1);
    while (ok && !interrupted)
    {
        if (!tired_ui_ready(10, 40))
        {
            if (tired_ui_key(-1) == 27 || interrupted)
                break;
            continue;
        }
        erase();
        safe_print(2, 2, title, COLS - 4);
        safe_print(4, 2, buffer.data == NULL ? "" : buffer.data, COLS - 4);
        safe_print(6, 2, "Enter saves; Esc cancels; Ctrl-U clears; Ctrl-N newline; Ctrl-T tab",
                   COLS - 4);
        refresh();
        int key = tired_ui_key(-1);
        if (key == ERR || key == KEY_RESIZE)
            continue;
        if (key == 27)
            break;
        if (key == '\n' || key == '\r' || key == KEY_ENTER)
        {
            accepted = true;
            break;
        }
        if (key == 21)
        {
            memset(&input_state, 0, sizeof(input_state));
            buffer.length = 0;
            if (buffer.data != NULL)
                buffer.data[0] = '\0';
        }
        else if (key == KEY_BACKSPACE || key == 127 || key == 8)
        {
            memset(&input_state, 0, sizeof(input_state));
            if (buffer.length != 0)
            {
                do
                {
                    --buffer.length;
                } while (buffer.length != 0 &&
                         ((unsigned char)buffer.data[buffer.length] & 0xc0U) == 0x80U);
                buffer.data[buffer.length] = '\0';
            }
        }
        else if (key == 14 || key == 20)
        {
            memset(&input_state, 0, sizeof(input_state));
            const char character = key == 14 ? '\n' : '\t';
            ok = tired_buffer_append(&buffer, &character, 1, error);
        }
        else if (key >= 32 && key <= UCHAR_MAX && key != 127)
        {
            /* Preserve an incomplete character across input waits and resizes;
             * a timed wget_wch would discard its local conversion state. */
            char byte = (char)key;
            wchar_t character;
            size_t decoded = mbrtowc(&character, &byte, 1, &input_state);
            if (decoded == (size_t)-2)
                continue;
            if (decoded == (size_t)-1)
            {
                memset(&input_state, 0, sizeof(input_state));
                continue;
            }
            char encoded[MB_LEN_MAX];
            mbstate_t state = {0};
            size_t length = wcrtomb(encoded, character, &state);
            if (length != (size_t)-1)
                ok = tired_buffer_append(&buffer, encoded, length, error);
        }
    }
    (void)curs_set(0);
    if (accepted && ok)
        ok = tired_buffer_take(&buffer, output, error);
    tired_buffer_destroy(&buffer);
    return accepted && ok;
}
void tired_ui_draw(int row, int column, const char *text, int maximum)
{
    safe_print(row, column, text, maximum);
}
void tired_ui_text_view(const char *title, const char *bytes) { viewer(title, bytes); }
bool tired_ui_text_prompt(const char *title, const char *initial, TiredText *output,
                          TiredError *error)
{
    return prompt(title, initial, output, error);
}
void tired_ui_edit_list(TiredMutation *mutation, TiredFieldId id, TiredError *error)
{
    TiredTextList *list = &mutation->proposed.spec.fields[id].value.list;
    size_t selected = 0;
    for (;;)
    {
        if (!tired_ui_ready(10, 40))
        {
            if (tired_ui_key(-1) == 27 || interrupted)
                return;
            continue;
        }
        erase();
        bool mask[TIRED_ARGUMENT_LIMIT] = {0};
        if (id == TIRED_FIELD_ARGV)
            (void)tired_argv_classify(list, mutation->proposed.review.sensitive_arguments, mask);
        safe_print(1, 2, tired_field_get(id)->name, COLS - 4);
        size_t first = selected > (size_t)(LINES - 7) ? selected - (size_t)(LINES - 7) : 0;
        for (size_t i = first; i < list->count && i - first < (size_t)(LINES - 6); ++i)
        {
            if (i == selected)
                attron(A_REVERSE);
            safe_print((int)(i - first) + 3, 2,
                       id == TIRED_FIELD_ARGV && i == 0
                           ? mutation->proposed.spec.fields[TIRED_FIELD_EXECUTABLE].value.text.data
                       : id == TIRED_FIELD_ARGV && mask[i] ? "[redacted]"
                       : list->items[i].length == 0        ? "(empty argument)"
                                                           : list->items[i].data,
                       COLS - 4);
            if (i == selected)
                attroff(A_REVERSE);
        }
        safe_print(LINES - 2, 2, "a add  Enter edit  d delete  [ / ] reorder  v reveal  Esc return",
                   COLS - 4);
        refresh();
        int key = tired_ui_key(-1);
        if (key == 27 || interrupted)
            return;
        if (key == KEY_UP && selected > 0)
            --selected;
        if (key == KEY_DOWN && selected + 1 < list->count)
            ++selected;
        if (((key == '[' && selected > 0) || (key == ']' && selected + 1 < list->count)) &&
            (id != TIRED_FIELD_ARGV || (selected != 0 && !(key == '[' && selected == 1))))
        {
            size_t next = key == '[' ? selected - 1 : selected + 1;
            TiredText saved = list->items[selected];
            list->items[selected] = list->items[next];
            list->items[next] = saved;
            if (id == TIRED_FIELD_ARGV)
            {
                bool sensitive = mutation->proposed.review.sensitive_arguments[selected];
                mutation->proposed.review.sensitive_arguments[selected] =
                    mutation->proposed.review.sensitive_arguments[next];
                mutation->proposed.review.sensitive_arguments[next] = sensitive;
            }
            selected = next;
        }
        if (key == 'd' && list->count > 0 && (id != TIRED_FIELD_ARGV || selected != 0))
        {
            list->bytes -= list->items[selected].length + 1;
            tired_text_destroy(&list->items[selected]);
            memmove(list->items + selected, list->items + selected + 1,
                    (list->count - selected - 1) * sizeof(*list->items));
            --list->count;
            if (id == TIRED_FIELD_ARGV)
            {
                memmove(mutation->proposed.review.sensitive_arguments + selected,
                        mutation->proposed.review.sensitive_arguments + selected + 1,
                        list->count - selected);
                mutation->proposed.review.sensitive_arguments[list->count] = false;
            }
            if (selected != 0 && selected == list->count)
                --selected;
        }
        if (key == 'a' || (key == '\n' && list->count > 0))
        {
            if (key != 'a' && id == TIRED_FIELD_ARGV && selected == 0)
            {
                viewer("Executable", "Edit the executable field on the primary review page.\n"
                                     "argv[0] becomes that resolved execution path.");
                continue;
            }
            TiredText value = {0};
            if (prompt("One list item; spaces remain part of this item",
                       key == 'a' || (id == TIRED_FIELD_ARGV && mask[selected])
                           ? ""
                           : list->items[selected].data,
                       &value, error))
            {
                if (key == 'a')
                    (void)tired_spec_append(&mutation->proposed.spec, id, value.data, value.length,
                                            TIRED_ORIGIN_USER, error);
                else
                {
                    size_t previous = list->items[selected].length;
                    if (tired_text_set(&list->items[selected], value.data, value.length,
                                       TIRED_INPUT_LIMIT, error))
                        list->bytes = list->bytes - previous + value.length;
                }
                mutation->proposed.spec.fields[id].origin = TIRED_ORIGIN_USER;
            }
            tired_text_destroy(&value);
        }
        if (key == 'v' && list->count > 0)
            viewer("Explicit temporary reveal; Escape returns", list->items[selected].data);
        if (key == 's' && id == TIRED_FIELD_ARGV && selected > 0)
            mutation->proposed.review.sensitive_arguments[selected] = true;
    }
}
static bool plain(TiredMutation *mutation, const TiredLayout *layout, const TiredBackend *backend,
                  TiredError *error)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
        return tired_error_set(
            error, TIRED_INVALID, "approval-required",
            "Noninteractive changes require --yes and explicit --allow-risk codes.", 0);
    fprintf(stdout, "\nService: %s\nScope: %s\nRuns as: %s\nUnit: %s\n",
            mutation->proposed.metadata.unit_name.data,
            mutation->proposed.metadata.user_scope ? "user" : "system",
            mutation->proposed.spec.fields[TIRED_FIELD_RUN_AS].value.text.data,
            mutation->proposed.unit_path.data);
    TiredRiskReport risks = {0};
    if (!tired_mutation_validate(mutation, layout, backend, &risks, error))
        return false;
    if (tired_spec_uses_hardening_baseline(&mutation->proposed.spec))
        fputs(tired_hardening_baseline_notice(), stdout);
    for (size_t i = 0; i < TIRED_FIELD_COUNT; ++i)
    {
        TiredText value = {0}, escaped = {0};
        bool read = field_text(&mutation->proposed.spec, (TiredFieldId)i,
                               mutation->proposed.review.sensitive_arguments, &value, error) &&
                    tired_encode_display(value.data, value.length, &escaped, error);
        if (read)
            fprintf(stdout, "  %-25s %s [%s]\n", tired_field_get((TiredFieldId)i)->name,
                    escaped.data, origin(mutation->proposed.spec.fields[i].origin));
        tired_text_destroy(&value);
        tired_text_destroy(&escaped);
        if (!read)
            return false;
    }
    TiredServiceSpec display = {0};
    TiredRedaction redaction = {0};
    TiredText unit = {0};
    bool rendered =
        tired_spec_display(&mutation->proposed.spec, mutation->proposed.review.sensitive_arguments,
                           false, &display, &redaction, error) &&
        tired_render_unit(&display, mutation->proposed.metadata.service_uuid,
                          mutation->proposed.has_environment ? &mutation->proposed.environment_path
                                                             : NULL,
                          &mutation->proposed.credentials, &unit, error);
    if (rendered)
        fprintf(stdout, "\n%s\n%s\n",
                redaction.redacted ? "Redacted unit preview; not installable" : "Generated unit",
                unit.data);
    tired_spec_destroy(&display);
    tired_text_destroy(&unit);
    if (!rendered)
        return false;
    fprintf(stdout, "Environment assignments: %zu; credential references: %zu\n",
            mutation->proposed.environment.count, mutation->proposed.credentials.count);
    for (size_t i = 0; i < mutation->proposed.environment.count; ++i)
    {
        const TiredEnvironmentEntry *entry = &mutation->proposed.environment.items[i];
        TiredText escaped = {0};
        const char *value = tired_environment_display(entry);
        bool shown = tired_encode_display(value, strlen(value), &escaped, error);
        if (shown)
            fprintf(stdout, "  %s=%s\n", entry->name.data, escaped.data);
        tired_text_destroy(&escaped);
        if (!shown)
            return false;
    }
    if (mutation->defer)
        fputs("Configuration changes; the running process keeps its earlier start context.\n",
              stdout);
    if (mutation->operation == TIRED_TRANSACTION_REMOVE ||
        mutation->operation == TIRED_TRANSACTION_RENAME)
        fputs("The service will stop. This may disconnect a route used by this SSH session.\n",
              stdout);
    char acknowledgment[128];
    for (size_t i = 0; i < TIRED_RISK_COUNT; ++i)
    {
        if (risks.pending[i])
            return tired_error_set(error, TIRED_INVALID, "risk-pending",
                                   "A required risk check is unresolved.", 0);
        if (risks.present[i] && !mutation->proposed.review.acknowledged[i])
        {
            const TiredRisk *risk = tired_risk_get((TiredRiskId)i);
            fprintf(stdout,
                    "\nRisk: %s\n%s\nType %s to acknowledge, or Enter to cancel: ", risk->code,
                    risk->message, risk->code);
            fflush(stdout);
            if (fgets(acknowledgment, sizeof(acknowledgment), stdin) == NULL ||
                strncmp(acknowledgment, risk->code, strlen(risk->code)) != 0 ||
                strcmp(acknowledgment + strlen(risk->code), "\n") != 0)
                return tired_error_set(error, TIRED_CANCELLED, "cancelled",
                                       "Cancelled before making service changes.", 0);
            mutation->proposed.review.acknowledged[i] = true;
        }
    }
    fputs("Apply this displayed operation? [y/N] ", stdout);
    fflush(stdout);
    char reply[8];
    return (fgets(reply, sizeof(reply), stdin) != NULL &&
            (strcmp(reply, "y\n") == 0 || strcmp(reply, "Y\n") == 0)) ||
           tired_error_set(error, TIRED_CANCELLED, "cancelled",
                           "Cancelled before making service changes.", 0);
}
static void summary_view(const TiredMutation *mutation)
{
    char summary[8192];
    const TiredServiceRecord *record = &mutation->proposed;
    int length = snprintf(
        summary, sizeof(summary),
        "Operation: %s\nUnit: %s\nScope: %s\nService identity: %s%s\n"
        "Unit destination: %s\nPrivate environment revision: %s\n"
        "Boot/user-manager enablement: %s\nStart request: %s\n"
        "Apply mode: %s\nEnable account lingering: %s\n"
        "Keep removal history: %s\n\n"
        "Removal deletes owned service files and private revisions only.\n"
        "Rename stops the old identity before starting the replacement.\n"
        "A restart can interrupt connections, including an SSH route.\n"
        "Review P for unit contents, D for differences, W for evidence, R for risks.\n",
        tired_transaction_operation_name(mutation->operation), record->metadata.unit_name.data,
        record->metadata.user_scope ? "user" : "system",
        record->spec.fields[TIRED_FIELD_RUN_AS].value.text.data,
        record->metadata.service_uid == 0 ? " (root, UID 0)" : "", record->unit_path.data,
        record->has_environment ? record->environment_path.data : "none",
        record->spec.fields[TIRED_FIELD_ENABLE].value.boolean ? "enabled" : "disabled",
        record->spec.fields[TIRED_FIELD_START].value.boolean ? "yes" : "no",
        mutation->defer ? "defer; running process retains its earlier context" : "apply now",
        record->spec.fields[TIRED_FIELD_ENABLE_LINGER].value.boolean ? "yes; account-level change"
                                                                     : "no",
        mutation->keep_history ? "yes" : "no");
    if (length > 0 && (size_t)length < sizeof(summary))
        viewer("Planned operation and consequences", summary);
}
bool tired_ui_review_recompute(TiredMutation *mutation, const TiredBackend *backend,
                               const TiredSettings *settings, TiredError *error)
{
    if (!mutation->proposed.has_profile)
        return true;
    TiredServiceSpec base = {0}, defaults = {0};
    TiredProfileMerge merged = {0};
    bool ok = tired_plan_service_defaults(&mutation->proposed.spec, settings, &defaults, error);
    for (size_t i = 0; ok && i < TIRED_FIELD_COUNT; ++i)
        ok = tired_spec_copy_field(&base,
                                   mutation->proposed.spec.fields[i].origin == TIRED_ORIGIN_PROFILE
                                       ? &defaults
                                       : &mutation->proposed.spec,
                                   (TiredFieldId)i, error);
    if (ok)
        ok = tired_profile_merge(&mutation->proposed.profile.profile, &base, &backend->features,
                                 &merged, error);
    if (ok)
    {
        const TiredFieldId dependent_risks[] = {TIRED_FIELD_RESTART_SEC, TIRED_FIELD_RETRY_POLICY,
                                                TIRED_FIELD_KILL_MODE};
        for (size_t i = 0; i < sizeof(dependent_risks) / sizeof(dependent_risks[0]); ++i)
        {
            TiredFieldId id = dependent_risks[i];
            const TiredFieldValue *old = &mutation->proposed.spec.fields[id],
                                  *next = &merged.spec.fields[id];
            if (tired_field_has_value(old) != tired_field_has_value(next) ||
                (tired_field_has_value(old) &&
                 (id == TIRED_FIELD_RESTART_SEC
                      ? old->value.microseconds != next->value.microseconds
                      : old->value.choice != next->value.choice)))
                tired_mutation_change(mutation, id);
        }
        tired_spec_destroy(&mutation->proposed.spec);
        mutation->proposed.spec = merged.spec;
        merged.spec = (TiredServiceSpec){0};
        free(mutation->proposed.profile.decisions);
        mutation->proposed.profile.decisions = merged.decisions;
        mutation->proposed.profile.count = merged.count;
        merged.decisions = NULL;
    }
    tired_spec_destroy(&base);
    tired_spec_destroy(&defaults);
    tired_profile_merge_destroy(&merged);
    return ok;
}

static bool reset_field(TiredMutation *mutation, TiredFieldId id, const TiredBackend *backend,
                        const TiredSettings *settings, TiredError *error)
{
    const TiredField *field = tired_field_get(id);
    if (id == TIRED_FIELD_NAME || id == TIRED_FIELD_SCOPE || id == TIRED_FIELD_EXECUTABLE ||
        id == TIRED_FIELD_ARGV || id == TIRED_FIELD_WORKING_DIRECTORY || id == TIRED_FIELD_RUN_AS ||
        id == TIRED_FIELD_GROUP)
        return tired_error_set(error, TIRED_INVALID, "captured-field-reset",
                               "Captured execution fields require an explicit edit.", 0);
    TiredServiceSpec defaults = {0};
    TiredProfileMerge merged = {0};
    TiredText encoded = {0};
    bool ok = tired_plan_service_defaults(&mutation->proposed.spec, settings, &defaults, error) &&
              tired_spec_copy_field(&mutation->proposed.spec, &defaults, id, error);
    if (ok && mutation->proposed.has_profile)
        ok = tired_profile_merge(&mutation->proposed.profile.profile, &mutation->proposed.spec,
                                 &backend->features, &merged, error) &&
             tired_spec_copy_field(&mutation->proposed.spec, &merged.spec, id, error);
    if (ok && field->default_value == NULL &&
        !tired_field_has_value(&mutation->proposed.spec.fields[id]))
        mutation->proposed.spec.fields[id].origin = TIRED_ORIGIN_UNSET;
    tired_spec_destroy(&defaults);
    tired_profile_merge_destroy(&merged);
    tired_text_destroy(&encoded);
    return ok;
}
bool tired_ui_review(TiredMutation *mutation, const TiredLayout *layout,
                     const TiredBackend *backend, const TiredSettings *settings, bool no_tui,
                     bool monochrome, TiredError *error)
{
    if (no_tui || !tired_ui_usable())
        return plain(mutation, layout, backend, error);
    (void)setlocale(LC_ALL, "");
    SCREEN *screen = newterm(NULL, stdout, stdin);
    if (screen == NULL)
        return plain(mutation, layout, backend, error);
    const int signals[] = {SIGINT, SIGTERM, SIGHUP};
    struct sigaction old[3], action = {.sa_handler = handle_signal};
    sigemptyset(&action.sa_mask);
    size_t handlers = 0;
    interrupted = 0;
    for (; handlers < 3; ++handlers)
        if (sigaction(signals[handlers], &action, &old[handlers]) != 0)
            break;
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    (void)curs_set(0);
    timeout(-1);
    if (!monochrome && has_colors())
    {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_YELLOW, -1);
    }
    bool advanced = false, accepted = false, scope_changed = false, refresh_profile = false;
    unsigned page = 0;
    size_t selected = 0, scroll = 0;
    TiredRiskReport risks = {0};
    TiredError validation = {0};
    static const TiredFieldId primary[] = {TIRED_FIELD_NAME,          TIRED_FIELD_ARGV,
                                           TIRED_FIELD_EXECUTABLE,    TIRED_FIELD_WORKING_DIRECTORY,
                                           TIRED_FIELD_RUN_AS,        TIRED_FIELD_SCOPE,
                                           TIRED_FIELD_START,         TIRED_FIELD_ENABLE,
                                           TIRED_FIELD_ENABLE_LINGER, TIRED_FIELD_RESTART,
                                           TIRED_FIELD_RESTART_SEC,   TIRED_FIELD_RETRY_POLICY,
                                           TIRED_FIELD_NOFILE_SOFT,   TIRED_FIELD_NOFILE_HARD,
                                           TIRED_FIELD_NETWORK};
    bool validate = true, recompute = false;
    while (!interrupted)
    {
        if (validate)
        {
            tired_error_clear(&validation);
            if ((!recompute ||
                 tired_ui_review_recompute(mutation, backend, settings, &validation)) &&
                tired_mutation_refresh(mutation, layout, &validation))
                (void)tired_mutation_validate(mutation, layout, backend, &risks, &validation);
            recompute = false;
            validate = false;
        }
        erase();
        attron(A_BOLD | COLOR_PAIR(1));
        safe_print(1, 2, "tired  /  review service", COLS - 4);
        attroff(A_BOLD | COLOR_PAIR(1));
        if (LINES < 15 || COLS < 54)
        {
            safe_print(4, 1, "Resize to at least 54 x 15; Esc cancels safely.", COLS - 2);
            refresh();
            int key = tired_ui_key(-1);
            if (key == 27)
                break;
            continue;
        }
        char summary[512];
        (void)snprintf(summary, sizeof(summary), "%s  |  %s scope  |  runs as %s%s",
                       mutation->proposed.metadata.unit_name.data,
                       mutation->proposed.metadata.user_scope ? "user" : "system",
                       mutation->proposed.spec.fields[TIRED_FIELD_RUN_AS].value.text.data,
                       mutation->proposed.metadata.service_uid == 0 ? " (root, UID 0)" : "");
        safe_print(2, 2, summary, COLS - 4);
        if (mutation->proposed.has_profile)
        {
            const TiredProfileSnapshot *profile = &mutation->proposed.profile;
            (void)snprintf(summary, sizeof(summary), "%s | %s", profile->profile.name,
                           profile->explicit_selection ? "selected by profile ID"
                           : strcmp(profile->profile.id, "generic") == 0
                               ? "generic fallback"
                               : "matched by executable name");
            safe_print(3, 2, summary, COLS - 4);
        }
        else
            safe_print(3, 2, "Generic; application requirements unknown", COLS - 4);
        if (mutation->proposed.metadata.user_scope)
            safe_print(4, 2,
                       backend->linger_known ? backend->linger_enabled
                                                   ? "Linger enabled; the user manager can persist "
                                                     "after logout and start at boot."
                                                   : "Linger disabled; --enable-linger is a "
                                                     "separate account change, shown below."
                                             : "Linger unknown; enabling the unit alone does not "
                                               "establish startup before login.",
                       COLS - 4);
        else if (mutation->proposed.has_profile)
            safe_print(4, 2, "Profile match is advice; executable identity is not authenticated.",
                       COLS - 4);
        else
            safe_print(
                4, 2, "Persistent retries supervise a process; application health is not verified.",
                COLS - 4);
        TiredFieldId fields[TIRED_FIELD_COUNT];
        size_t count = 0;
        for (size_t i = 0; i < TIRED_FIELD_COUNT; ++i)
        {
            bool include =
                page == 0   ? advanced
                : page == 1 ? i == TIRED_FIELD_TYPE || i == TIRED_FIELD_PID_FILE ||
                                  i == TIRED_FIELD_REMAIN_AFTER_EXIT ||
                                  i == TIRED_FIELD_KILL_MODE || i == TIRED_FIELD_KILL_SIGNAL ||
                                  i == TIRED_FIELD_TIMEOUT_START || i == TIRED_FIELD_TIMEOUT_STOP
                : page == 2 ? (i >= TIRED_FIELD_RESTART && i <= TIRED_FIELD_START_LIMIT_BURST) ||
                                  i == TIRED_FIELD_SUCCESS_EXIT_STATUS ||
                                  i == TIRED_FIELD_RESTART_PREVENT_EXIT_STATUS
                : page == 3 ? (i >= TIRED_FIELD_NOFILE_SOFT && i <= TIRED_FIELD_CPU_QUOTA) ||
                                  i == TIRED_FIELD_NICE
                : page == 4 ? i == TIRED_FIELD_RUN_AS || i == TIRED_FIELD_GROUP ||
                                  i == TIRED_FIELD_SUPPLEMENTARY_GROUPS || i == TIRED_FIELD_UMASK ||
                                  i == TIRED_FIELD_CAPABILITY_BOUNDING_SET ||
                                  i == TIRED_FIELD_AMBIENT_CAPABILITIES
                : page == 5
                    ? (i >= TIRED_FIELD_NO_NEW_PRIVILEGES && i <= TIRED_FIELD_PROTECT_HOME) ||
                          (i >= TIRED_FIELD_READ_WRITE_PATHS && i <= TIRED_FIELD_STATE_DIRECTORY) ||
                          i == TIRED_FIELD_RUNTIME_DIRECTORY_MODE ||
                          i == TIRED_FIELD_STATE_DIRECTORY_MODE
                : page == 6
                    ? (i >= TIRED_FIELD_ENVIRONMENT_FILES &&
                       i <= TIRED_FIELD_REQUIRES_MOUNTS_FOR) ||
                          i == TIRED_FIELD_WANTED_BY || i == TIRED_FIELD_NETWORK
                    : (i >= TIRED_FIELD_STANDARD_INPUT && i <= TIRED_FIELD_SYSLOG_IDENTIFIER);
            if (include)
                fields[count++] = (TiredFieldId)i;
        }
        if (page == 0 && !advanced)
        {
            count = sizeof(primary) / sizeof(primary[0]);
            memcpy(fields, primary, sizeof(primary));
        }
        static const char *titles[] = {
            "1 primary / A all fields", "2 process",      "3 retry",  "4 resources", "5 identity",
            "6 security & paths",       "7 dependencies", "8 logging"};
        safe_print(5, 2, titles[page], COLS - 4);
        if (selected >= count)
            selected = count - 1;
        size_t visible = (size_t)(LINES - 11);
        if (selected < scroll)
            scroll = selected;
        if (selected >= scroll + visible)
            scroll = selected - visible + 1;
        for (size_t index = scroll; index < count && index - scroll < visible; ++index)
        {
            TiredFieldId id = fields[index];
            TiredText value = {0};
            TiredError ignored = {0};
            (void)field_text(&mutation->proposed.spec, id,
                             mutation->proposed.review.sensitive_arguments, &value, &ignored);
            int row = (int)(index - scroll) + 6;
            if (index == selected)
                attron(A_REVERSE);
            safe_print(row, 2, tired_field_get(id)->name, 22);
            safe_print(row, 25, value.data == NULL ? "unknown" : value.data, COLS - 41);
            safe_print(row, COLS - 13, origin(mutation->proposed.spec.fields[id].origin), 11);
            if (index == selected)
                attroff(A_REVERSE);
            tired_text_destroy(&value);
        }
        size_t required = 0,
               warnings = (validation.status != TIRED_OK) +
                          tired_spec_uses_hardening_baseline(&mutation->proposed.spec);
        for (size_t i = 0; i < TIRED_RISK_COUNT; ++i)
        {
            warnings += risks.pending[i] || risks.present[i];
            required += risks.pending[i] ||
                        (risks.present[i] && !mutation->proposed.review.acknowledged[i]);
        }
        for (size_t i = 0; i < mutation->proposed.profile.count; ++i)
        {
            TiredRecommendationDisposition decision = mutation->proposed.profile.decisions[i];
            warnings += decision == TIRED_RECOMMENDATION_CONDITION_UNKNOWN ||
                        decision == TIRED_RECOMMENDATION_VERSION_UNKNOWN ||
                        decision == TIRED_RECOMMENDATION_INCOMPATIBLE ||
                        decision == TIRED_RECOMMENDATION_CONFLICT ||
                        decision == TIRED_RECOMMENDATION_REQUIRED_CONFLICT;
        }
        attron(COLOR_PAIR(2));
        if (validation.status != TIRED_OK)
            safe_print(LINES - 4, 2,
                       validation.message == NULL ? "Validation failed." : validation.message,
                       COLS - 4);
        else if (required != 0)
            safe_print(LINES - 4, 2,
                       "Risk acknowledgment required: press R to inspect each choice.", COLS - 4);
        else if (mutation->defer)
            safe_print(LINES - 4, 2,
                       "Apply deferred: the current process keeps its earlier start context.",
                       COLS - 4);
        else if (mutation->operation == TIRED_TRANSACTION_REMOVE ||
                 mutation->operation == TIRED_TRANSACTION_RENAME)
            safe_print(LINES - 4, 2,
                       "This operation stops the service and may disconnect your SSH route.",
                       COLS - 4);
        else
            safe_print(LINES - 4, 2,
                       "Approve only the displayed configuration. Elevation follows when required.",
                       COLS - 4);
        attroff(COLOR_PAIR(2));
        (void)snprintf(summary, sizeof(summary), "%zu warnings | %zu risks need review/checks",
                       warnings, required);
        safe_print(LINES - 3, 2, summary, COLS - 4);
        safe_print(LINES - 2, 2, "C apply  1-8 pages  P preview  H hardening  ? field/origin help",
                   COLS - 4);
        refresh();
        int key = tired_ui_key(-1);
        if (key == 27 || key == 'q')
            break;
        if (key == KEY_UP || key == 'k' || key == KEY_BTAB)
            selected = selected == 0 ? count - 1 : selected - 1;
        if (key == KEY_DOWN || key == 'j' || key == '\t')
            selected = (selected + 1) % count;
        if (key == KEY_NPAGE)
            selected = selected + visible < count ? selected + visible : count - 1;
        if (key == KEY_PPAGE)
            selected = selected > visible ? selected - visible : 0;
        if (key >= '1' && key <= '8')
        {
            page = (unsigned)(key - '1');
            advanced = false;
            selected = scroll = 0;
        }
        if (key == 'e' || key == 'E' || key == 'K')
        {
            tired_ui_inputs(mutation, key == 'K', error);
            validate = true;
        }
        if (key == 'a' || key == 'A')
        {
            advanced = !advanced;
            page = 0;
            selected = scroll = 0;
        }
        if (key == '?')
        {
            TiredText description = {0};
            if (tired_ui_field_evidence(mutation, fields[selected], &description, error))
                viewer("Selected field: rationale and sources", description.data);
            else
                viewer("Cannot display field evidence", error->message);
            tired_text_destroy(&description);
        }
        if (key == 'h' || key == 'H')
        {
            if (viewer("Baseline hardening: Space selects; Esc returns",
                       tired_hardening_baseline_notice()) == ' ')
            {
                if (tired_spec_hardening_baseline(&mutation->proposed.spec, error))
                    validate = recompute = true;
                else
                    viewer("Cannot select hardening", error->message);
            }
        }
        if (key == 'd' || key == 'D')
            tired_ui_diff(mutation, layout, error);
        if (key == 's' || key == 'S')
            summary_view(mutation);
        if ((key == 'm' || key == 'M') && (mutation->operation == TIRED_TRANSACTION_EDIT ||
                                           mutation->operation == TIRED_TRANSACTION_RESTORE))
        {
            mutation->defer = !mutation->defer;
            validate = true;
        }
        if (key == 'z' || key == 'Z')
        {
            tired_mutation_change(mutation, fields[selected]);
            if (!reset_field(mutation, fields[selected], backend, settings, error))
                viewer("Reset field", error->message);
            validate = recompute = true;
        }
        if (key == 'f' || key == 'F')
        {
            refresh_profile = true;
            break;
        }
        if (key == 'p' || key == 'P')
        {
            TiredText unit = {0};
            TiredServiceSpec display = {0};
            TiredRedaction redaction = {0};
            if (tired_spec_display(&mutation->proposed.spec,
                                   mutation->proposed.review.sensitive_arguments, false, &display,
                                   &redaction, error) &&
                tired_render_unit(&display, mutation->proposed.metadata.service_uuid,
                                  mutation->proposed.has_environment
                                      ? &mutation->proposed.environment_path
                                      : NULL,
                                  &mutation->proposed.credentials, &unit, error))
                viewer(redaction.redacted ? "Redacted preview; not installable" : "Generated unit",
                       unit.data);
            tired_spec_destroy(&display);
            tired_text_destroy(&unit);
        }
        if (key == 'w' || key == 'W')
        {
            TiredText profile = {0};
            if (mutation->proposed.has_profile &&
                tired_profile_snapshot_encode(&mutation->proposed.profile, &profile, error))
                viewer("Profile / evidence / suppressed advice", profile.data);
            else
                viewer("Generic policy",
                       "Foreground process, captured directory/account.\nRestart after failure; "
                       "persistent delayed retries.\nNo target execution during discovery.\nNo "
                       "network or descriptor assumptions.\n");
            tired_text_destroy(&profile);
        }
        if (key == 'r' || key == 'R')
        {
            for (size_t i = 0; i < TIRED_RISK_COUNT; ++i)
                if (risks.present[i] || risks.pending[i])
                {
                    int choice = KEY_RESIZE;
                    while ((choice == ERR || choice == KEY_RESIZE) && !interrupted)
                    {
                        if (!tired_ui_ready(15, 54))
                        {
                            choice = tired_ui_key(-1);
                            if (choice != 27)
                                choice = KEY_RESIZE;
                            continue;
                        }
                        erase();
                        safe_print(2, 2, tired_risk_get((TiredRiskId)i)->code, COLS - 4);
                        const char *message = tired_risk_get((TiredRiskId)i)->message;
                        size_t length = strlen(message), offset = 0;
                        int row = 4;
                        while (offset < length && row < LINES - 4)
                        {
                            size_t width = length - offset < (size_t)(COLS - 4)
                                               ? length - offset
                                               : (size_t)(COLS - 4);
                            if (offset + width < length)
                                while (width > 1 && message[offset + width] != ' ')
                                    --width;
                            char line[4096];
                            if (width >= sizeof(line))
                                width = sizeof(line) - 1;
                            memcpy(line, message + offset, width);
                            line[width] = '\0';
                            safe_print(row++, 2, line, COLS - 4);
                            offset += width;
                            while (offset < length && message[offset] == ' ')
                                ++offset;
                        }
                        safe_print(LINES - 3, 2,
                                   risks.pending[i]
                                       ? "Unresolved check; cannot acknowledge."
                                       : "Space explicitly acknowledges this risk; Esc returns.",
                                   COLS - 4);
                        refresh();
                        choice = tired_ui_key(-1);
                    }
                    if (choice == ' ' && !risks.pending[i])
                        mutation->proposed.review.acknowledged[i] = true;
                    if (choice == 27 || interrupted)
                        break;
                }
            validate = true;
        }
        if (key == '\n' || key == KEY_ENTER || key == ' ' || key == 'u' || key == 'x')
        {
            TiredFieldId id = fields[selected];
            tired_mutation_change(mutation, id);
            const TiredField *field = tired_field_get(id);
            if (id == TIRED_FIELD_NAME && mutation->operation != TIRED_TRANSACTION_CREATE &&
                mutation->operation != TIRED_TRANSACTION_RENAME)
            {
                viewer("Service name",
                       "Use the explicit rename action to transfer a service identity.");
                continue;
            }
            if (id == TIRED_FIELD_SCOPE)
            {
                if (mutation->operation != TIRED_TRANSACTION_CREATE)
                {
                    viewer("Scope", "Existing services stay in their owning manager.\nCreate a "
                                    "separately reviewed service to change its scope.\n");
                    continue;
                }
                bool user = !mutation->proposed.metadata.user_scope;
                if (tired_spec_set(&mutation->proposed.spec, id, user ? "user" : "system",
                                   user ? 4 : 6, TIRED_ORIGIN_USER, true, error))
                {
                    scope_changed = true;
                    break;
                }
                continue;
            }
            if (key == 'u')
                (void)tired_spec_inherit(&mutation->proposed.spec, id, error);
            else if (field->kind == TIRED_FIELD_LIST)
            {
                if (key == 'x')
                    (void)tired_spec_clear_list(&mutation->proposed.spec, id, TIRED_ORIGIN_USER,
                                                error);
                else
                    tired_ui_edit_list(mutation, id, error);
            }
            else if (field->kind == TIRED_FIELD_BOOL)
            {
                bool next = !mutation->proposed.spec.fields[id].value.boolean;
                (void)tired_spec_set(&mutation->proposed.spec, id, next ? "true" : "false",
                                     next ? 4 : 5, TIRED_ORIGIN_USER, true, error);
            }
            else if (field->kind == TIRED_FIELD_CHOICE && field->choices != NULL)
            {
                const char *choice = field->choices;
                size_t current = mutation->proposed.spec.fields[id].value.choice;
                for (size_t i = 0; i <= current; ++i)
                {
                    const char *next = strchr(choice, '|');
                    if (next == NULL)
                    {
                        choice = field->choices;
                        break;
                    }
                    choice = next + 1;
                }
                const char *end = strchr(choice, '|');
                (void)tired_spec_set(&mutation->proposed.spec, id, choice,
                                     end == NULL ? strlen(choice) : (size_t)(end - choice),
                                     TIRED_ORIGIN_USER, true, error);
            }
            else
            {
                TiredText initial = {0}, value = {0};
                (void)field_text(&mutation->proposed.spec, id,
                                 mutation->proposed.review.sensitive_arguments, &initial, error);
                if (prompt(field->name, initial.data, &value, error))
                    (void)tired_spec_set(&mutation->proposed.spec, id, value.data, value.length,
                                         TIRED_ORIGIN_USER, true, error);
                tired_text_destroy(&initial);
                tired_text_destroy(&value);
            }
            if (error->status != TIRED_OK)
                viewer("Invalid value", error->message);
            validate = recompute = true;
        }
        if ((key == 'c' || key == 'C') && validation.status == TIRED_OK && required == 0)
        {
            accepted = true;
            break;
        }
    }
    endwin();
    delscreen(screen);
    while (handlers != 0)
    {
        --handlers;
        (void)sigaction(signals[handlers], &old[handlers], NULL);
    }
    if (scope_changed)
        return tired_error_set(error, TIRED_CONFLICT, "review-scope-changed",
                               "Rebuild the proposal for the newly selected manager.", 0);
    if (refresh_profile)
        return tired_error_set(
            error, TIRED_CONFLICT, "review-refresh-profile",
            "Explicitly refresh profile recommendations and review the revision comparison.", 0);
    if (!accepted)
        return tired_error_set(error, interrupted ? TIRED_INTERRUPTED : TIRED_CANCELLED,
                               "cancelled", "Cancelled before making service changes.", 0);
    tired_error_clear(error);
    return true;
}
