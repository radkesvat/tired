#define _GNU_SOURCE
#define _XOPEN_SOURCE_EXTENDED 1
#include "tired/config_frontend.h"
#include "tired/encode.h"
#include "tired/frontend.h"
#include "tired/io.h"
#include "tired/process.h"
#include "tired/show_frontend.h"
#include "tired/status_frontend.h"
#include "tired/ui.h"
#include <ctype.h>
#include <curses.h>
#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t cancelled;
static void cancel(int number) { cancelled = tired_ui_signal = number; }
static const char *string(struct json_object *object, const char *key, const char *fallback)
{
    struct json_object *value = NULL;
    return object != NULL && json_object_object_get_ex(object, key, &value) &&
                   json_object_is_type(value, json_type_string)
               ? json_object_get_string(value)
               : fallback;
}
static const char *property(struct json_object *entry, const char *key)
{
    struct json_object *live = NULL, *properties = NULL;
    if (!json_object_object_get_ex(entry, "live", &live) ||
        !json_object_object_get_ex(live, "properties", &properties))
        return "unknown";
    return string(properties, key, "unknown");
}
static struct json_object *member(struct json_object *object, const char *key)
{
    struct json_object *value = NULL;
    return object != NULL && json_object_object_get_ex(object, key, &value) ? value : NULL;
}
bool tired_ui_dashboard_details(struct json_object *entry, TiredText *output, TiredError *error)
{
    struct json_object *uid = member(entry, "service_uid");
    char numeric[32];
    const char *account = string(entry, "run_as", NULL);
    if (account == NULL && uid != NULL && json_object_is_type(uid, json_type_int))
    {
        snprintf(numeric, sizeof(numeric), "UID %" PRIu64, json_object_get_uint64(uid));
        account = numeric;
    }
    const char *parts[] = {"runs as ",
                           account == NULL ? "unknown" : account,
                           " | profile ",
                           string(entry, "profile", "unknown"),
                           "\n",
                           property(entry, "ActiveState"),
                           "/",
                           property(entry, "SubState"),
                           " | boot ",
                           string(member(entry, "live"), "file_state", "unknown"),
                           "\n",
                           "unit ",
                           string(member(entry, "unit_file"), "state", "unknown"),
                           " | env ",
                           string(member(entry, "environment_file"), "state", "unknown"),
                           " | tx ",
                           string(entry, "transactions", "unknown")};
    TiredBuffer buffer;
    tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
    bool ok = true;
    for (size_t i = 0; ok && i < sizeof(parts) / sizeof(parts[0]); ++i)
        ok = tired_buffer_append(&buffer, parts[i], strlen(parts[i]), error);
    if (ok)
        ok = tired_buffer_take(&buffer, output, error);
    tired_buffer_destroy(&buffer);
    return ok;
}
static bool matches(const char *name, const char *search)
{
    if (search == NULL || search[0] == '\0')
        return true;
    size_t length = strlen(search);
    for (; *name; ++name)
    {
        size_t i = 0;
        while (i < length && name[i] &&
               tolower((unsigned char)name[i]) == tolower((unsigned char)search[i]))
            ++i;
        if (i == length)
            return true;
    }
    return false;
}
static bool collect_start(const char *executable, bool user, const char *unit,
                          TiredProcess **process, TiredError *error)
{
    char *listing[] = {(char *)executable, "list", "--json", user ? "--user" : "--system", NULL};
    char *logs[] = {(char *)executable,           "logs", (char *)unit, "--lines", "50",
                    user ? "--user" : "--system", NULL};
    char *environment[10] = {"PATH=/usr/bin:/bin", "LC_ALL=C", "TERM=dumb", NULL};
    TiredText paths[4] = {0};
    const char *names[] = {"HOME", "XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_RUNTIME_DIR"};
    size_t count = 3;
    bool ok = true;
    for (size_t i = 0; i < 4 && ok; ++i)
    {
        const char *value = getenv(names[i]);
        if (value == NULL)
            continue;
        TiredBuffer buffer;
        tired_buffer_init(&buffer, 4096);
        ok = tired_buffer_append(&buffer, names[i], strlen(names[i]), error) &&
             tired_buffer_append(&buffer, "=", 1, error) &&
             tired_buffer_append(&buffer, value, strlen(value), error) &&
             tired_buffer_take(&buffer, &paths[i], error);
        tired_buffer_destroy(&buffer);
        if (ok)
            environment[count++] = paths[i].data;
    }
    if (ok)
        ok = tired_process_start(executable, unit == NULL ? listing : logs, environment, 10000,
                                 TIRED_INPUT_LIMIT, process, error);
    for (size_t i = 0; i < 4; ++i)
        tired_text_destroy(&paths[i]);
    return ok;
}
static void action(SCREEN *screen, const TiredRequest *request, const char *profiles,
                   const char *unit, const char *new_name, TiredCommand command)
{
    endwin();
    bool lifecycle = command >= TIRED_COMMAND_START && command <= TIRED_COMMAND_DISABLE;
    TiredRequest selected = {.command = command, .yes = lifecycle};
    TiredText output = {0};
    TiredStatus status = TIRED_OK;
    TiredError error = {0};
    bool user = tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user");
    bool prepared = tired_spec_set(&selected.overrides, TIRED_FIELD_SCOPE, user ? "user" : "system",
                                   user ? 4 : 6, TIRED_ORIGIN_USER, true, &error) &&
                    tired_text_list_append(&selected.arguments, unit, strlen(unit), 2, 512, &error);
    if (prepared && command == TIRED_COMMAND_RENAME)
        prepared = new_name != NULL && tired_text_list_append(&selected.arguments, new_name,
                                                              strlen(new_name), 2, 512, &error);
    bool ok =
        prepared &&
        (command == TIRED_COMMAND_STATUS ? tired_status_command(&selected, &output, &status, &error)
         : command == TIRED_COMMAND_SHOW
             ? tired_show_command(&selected, &output, &status, &error)
             : tired_frontend_command(&selected, profiles, &output, &status, &error));
    set_term(screen);
    refresh();
    tired_ui_text_view(ok ? "Service result" : "Operation could not be completed",
                       ok                      ? output.data
                       : error.message == NULL ? "Operation failed."
                                               : error.message);
    tired_text_destroy(&output);
    tired_request_destroy(&selected);
}
bool tired_ui_dashboard(const TiredRequest *request, const char *profiles, TiredStatus *status,
                        TiredError *error)
{
    TiredSettings settings = {0};
    if (!tired_config_discover(request, &settings, error))
        return false;
    tired_ui_presentation(settings.ascii);
    tired_settings_destroy(&settings);
    char executable[4096];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length <= 0 || (size_t)length >= sizeof(executable) - 1)
        return tired_error_set(error, TIRED_INTERNAL, "dashboard-executable",
                               "Cannot locate the frontend executable.", errno);
    executable[length] = '\0';
    (void)setlocale(LC_ALL, "");
    SCREEN *screen = newterm(NULL, stdout, stdin);
    if (screen == NULL)
        return tired_error_set(error, TIRED_UNSUPPORTED, "dashboard-terminal",
                               "No usable terminal description; use tired list or --no-tui.", 0);
    const int signals[] = {SIGINT, SIGTERM, SIGHUP};
    struct sigaction previous[3], handler = {.sa_handler = cancel};
    sigemptyset(&handler.sa_mask);
    cancelled = 0;
    tired_ui_signal = 0;
    size_t handlers = 0;
    for (; handlers < 3; ++handlers)
        if (sigaction(signals[handlers], &handler, &previous[handlers]) != 0)
            break;
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(100);
    struct json_object *document = NULL, *services = NULL;
    TiredProcess *collector = NULL;
    TiredText search = {0}, selected_id = {0};
    size_t selected = 0, visible[1024] = {0}, count = 0;
    uint64_t refreshed = 0;
    bool user = tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user"),
         collect = true;
    TiredError observation = {0};
    while (!cancelled && !tired_ui_signal)
    {
        if (collect && collector == NULL)
        {
            (void)collect_start(executable, user, NULL, &collector, &observation);
            collect = false;
        }
        if (collector != NULL && tired_process_step(collector))
        {
            TiredProcessResult result = tired_process_result(collector);
            struct json_object *next = NULL, *array = NULL;
            if (tired_json_parse(result.standard_output, result.output_length, TIRED_INPUT_LIMIT,
                                 &next, &observation) &&
                json_object_object_get_ex(next, "services", &array) &&
                json_object_is_type(array, json_type_array) &&
                json_object_array_length(array) <= 1024)
            {
                json_object_put(document);
                document = next;
                services = array;
                next = NULL;
                refreshed = tired_monotonic_usec();
                tired_error_clear(&observation);
            }
            json_object_put(next);
            tired_process_destroy(collector);
            collector = NULL;
        }
        count = 0;
        for (size_t i = 0; services != NULL && i < json_object_array_length(services); ++i)
        {
            struct json_object *entry = json_object_array_get_idx(services, i);
            const char *name =
                string(entry, "unit_name", string(entry, "filename_display", "unknown"));
            if (matches(name, search.data))
            {
                visible[count] = i;
                if (selected_id.data != NULL &&
                    strcmp(selected_id.data, string(entry, "service_uuid", "")) == 0)
                    selected = count;
                ++count;
            }
        }
        if (selected >= count)
            selected = count == 0 ? 0 : count - 1;
        erase();
        attron(A_BOLD);
        tired_ui_draw(1, 2, user ? "tired  /  user services" : "tired  /  system services",
                      COLS - 4);
        attroff(A_BOLD);
        if (LINES < 14 || COLS < 54)
            tired_ui_draw(4, 1, "Resize to at least 54 x 14; Q exits.", COLS - 2);
        else
        {
            tired_ui_draw(3, 2, "Service / identity / runtime / files & transactions", COLS - 4);
            size_t rows = (size_t)(LINES - 9) / 4,
                   first = selected >= rows ? selected - rows + 1 : 0;
            for (size_t i = first; i < count && i - first < rows; ++i)
            {
                struct json_object *entry = json_object_array_get_idx(services, visible[i]);
                if (i == selected)
                    attron(A_REVERSE);
                int row = (int)(i - first) * 4 + 5;
                tired_ui_draw(
                    row, 2,
                    string(entry, "unit_name", string(entry, "filename_display", "unknown record")),
                    COLS - 13);
                tired_ui_draw(row, COLS - 10, string(entry, "scope", user ? "user" : "system"), 8);
                TiredText details = {0};
                TiredError row_error = {0};
                if (tired_ui_dashboard_details(entry, &details, &row_error))
                {
                    char *line = details.data;
                    for (int detail = 1; detail <= 3 && line != NULL; ++detail)
                    {
                        char *next = strchr(line, '\n');
                        if (next != NULL)
                            *next++ = '\0';
                        tired_ui_draw(row + detail, 2, line, COLS - 4);
                        line = next;
                    }
                }
                if (strcmp(string(entry, "record", ""), "present") != 0)
                {
                    char record_state[96];
                    snprintf(record_state, sizeof(record_state), "Record %s | tx %s; use recover",
                             string(entry, "record", "unknown"),
                             string(entry, "transactions", "unknown"));
                    tired_ui_draw(row + 3, 2, record_state, COLS - 4);
                }
                tired_text_destroy(&details);
                if (i == selected)
                    attroff(A_REVERSE);
            }
            if (count == 0)
                tired_ui_draw(6, 2,
                              collector != NULL
                                  ? "Reading managed services..."
                                  : "No services match. Create one with tired COMMAND.",
                              COLS - 4);
            if (observation.status != TIRED_OK)
                tired_ui_draw(LINES - 4, 2,
                              observation.message == NULL
                                  ? "Refresh failed; previous observations remain visible."
                                  : observation.message,
                              COLS - 4);
            else if (member(document, "transactions_complete") != NULL &&
                     !json_object_get_boolean(member(document, "transactions_complete")))
                tired_ui_draw(LINES - 4, 2, "Incomplete transaction inventory; use tired recover.",
                              COLS - 4);
            else if (member(document, "inventory_complete") != NULL &&
                     !json_object_get_boolean(member(document, "inventory_complete")))
                tired_ui_draw(LINES - 4, 2,
                              "Incomplete service inventory; inspect tired list --json.", COLS - 4);
            else
                tired_ui_draw(
                    LINES - 4, 2,
                    "Live process state is an observation; application health is not verified.",
                    COLS - 4);
            tired_ui_draw(LINES - 2, 2, "Enter actions  / search  R refresh  Tab scope  Q exit",
                          COLS - 4);
        }
        refresh();
        uint64_t now = tired_monotonic_usec();
        uint64_t until_refresh =
            refreshed != 0 && now < refreshed + 30000000 ? refreshed + 30000000 - now : 0;
        int delay = (collector != NULL   ? 100
                     : until_refresh > 0 ? (int)((until_refresh + 999) / 1000)
                                         : 1000);
        int key = tired_ui_key(delay);
        if (key == 'q' || key == 'Q' || key == 27)
            break;
        if (key == KEY_UP && selected > 0)
        {
            --selected;
            tired_text_destroy(&selected_id);
        }
        if (key == KEY_DOWN && selected + 1 < count)
        {
            ++selected;
            tired_text_destroy(&selected_id);
        }
        if (key == '/' &&
            tired_ui_text_prompt("Search services (case insensitive)", search.data, &search, error))
            selected = 0;
        if (key == 'r' || key == 'R')
            collect = true;
        if (key == '\t' && collector == NULL)
        {
            user = !user;
            json_object_put(document);
            document = services = NULL;
            count = selected = 0;
            tired_text_destroy(&selected_id);
            collect = true;
        }
        if ((key == '\n' || key == KEY_ENTER) && count > 0)
        {
            struct json_object *entry = json_object_array_get_idx(services, visible[selected]);
            const char *unit = string(entry, "unit_name", NULL);
            if (unit == NULL || strcmp(string(entry, "record", ""), "unknown") == 0)
            {
                tired_ui_text_view("Unreadable managed entry",
                                   "Inspect tired list --json and tired recover.\nNo mutation is "
                                   "available without established ownership.\n");
                continue;
            }
            erase();
            tired_ui_draw(2, 2, unit, COLS - 4);
            tired_ui_draw(3, 2, user ? "User manager / current account" : "System manager",
                          COLS - 4);
            tired_ui_draw(5, 2, "S status  V configuration  L logs  T start  O stop  R restart",
                          COLS - 4);
            tired_ui_draw(7, 2, "E edit  N rename  D remove  Esc return", COLS - 4);
            tired_ui_draw(9, 2, "Stopping a networking service may disconnect your SSH route.",
                          COLS - 4);
            refresh();
            timeout(-1);
            int choice;
            do
            {
                choice = tired_ui_key(-1);
            } while (choice == ERR && !cancelled);
            timeout(100);
            TiredCommand command = choice == 's' || choice == 'S'   ? TIRED_COMMAND_STATUS
                                   : choice == 'v' || choice == 'V' ? TIRED_COMMAND_SHOW
                                   : choice == 't' || choice == 'T' ? TIRED_COMMAND_START
                                   : choice == 'o' || choice == 'O' ? TIRED_COMMAND_STOP
                                   : choice == 'r' || choice == 'R' ? TIRED_COMMAND_RESTART
                                   : choice == 'e' || choice == 'E' ? TIRED_COMMAND_EDIT
                                   : choice == 'n' || choice == 'N' ? TIRED_COMMAND_RENAME
                                   : choice == 'l' || choice == 'L' ? TIRED_COMMAND_LOGS
                                   : choice == 'd' || choice == 'D' ? TIRED_COMMAND_REMOVE
                                                                    : TIRED_COMMAND_COUNT;
            if (command == TIRED_COMMAND_LOGS)
            {
                TiredProcess *logs = NULL;
                if (collect_start(executable, user, unit, &logs, error))
                {
                    while (!cancelled && !tired_process_step(logs))
                    {
                        int pressed = tired_ui_key(100);
                        if (pressed == 27)
                            tired_process_cancel(logs);
                    }
                    if (cancelled)
                        tired_process_cancel(logs);
                    while (!tired_process_step(logs))
                    {
                        struct timespec pause = {.tv_nsec = 20000000};
                        nanosleep(&pause, NULL);
                    }
                    TiredProcessResult result = tired_process_result(logs);
                    TiredText body = {0};
                    if (tired_text_set(&body, result.standard_output, result.output_length,
                                       TIRED_INPUT_LIMIT, error))
                        tired_ui_text_view("Recent selected-service journal (bounded)", body.data);
                    tired_text_destroy(&body);
                    tired_process_destroy(logs);
                }
                command = TIRED_COMMAND_COUNT;
            }
            if (command != TIRED_COMMAND_COUNT)
            {
                TiredText new_name = {0};
                if (command == TIRED_COMMAND_RENAME &&
                    !tired_ui_text_prompt(
                        "New exact service name (rename stops/restarts this service)", "",
                        &new_name, error))
                    continue;
                TiredRequest selected_scope = *request;
                selected_scope.overrides = (TiredServiceSpec){0};
                if (tired_spec_set(&selected_scope.overrides, TIRED_FIELD_SCOPE,
                                   user ? "user" : "system", user ? 4 : 6, TIRED_ORIGIN_USER, true,
                                   error))
                    action(screen, &selected_scope, profiles, unit, new_name.data, command);
                tired_spec_destroy(&selected_scope.overrides);
                tired_text_destroy(&new_name);
                collect = true;
            }
        }
        if (count > 0 && selected_id.data == NULL)
        {
            const char *uuid =
                string(json_object_array_get_idx(services, visible[selected]), "service_uuid", "");
            (void)tired_text_set(&selected_id, uuid, strlen(uuid), 64, error);
        }
        if (refreshed != 0 && tired_monotonic_usec() - refreshed > 30000000 && collector == NULL)
            collect = true;
    }
    if (collector != NULL)
    {
        tired_process_cancel(collector);
        while (!tired_process_step(collector))
        {
            struct timespec pause = {.tv_nsec = 10000000};
            nanosleep(&pause, NULL);
        }
        tired_process_destroy(collector);
    }
    endwin();
    delscreen(screen);
    while (handlers > 0)
    {
        --handlers;
        sigaction(signals[handlers], &previous[handlers], NULL);
    }
    json_object_put(document);
    tired_text_destroy(&search);
    tired_text_destroy(&selected_id);
    *status = cancelled || tired_ui_signal ? TIRED_INTERRUPTED : TIRED_OK;
    tired_error_clear(error);
    return true;
}
