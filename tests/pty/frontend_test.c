#define _GNU_SOURCE
#include "tired/backend.h"
#include "tired/io.h"
#include "tired/ui.h"
#include <curses.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
static bool drawn_text(const char *capture, const char *needle)
{
    char visible[65536];
    size_t length = 0;
    for (size_t i = 0; capture[i] != '\0'; ++i)
    {
        if (capture[i] == '\033' && capture[i + 1] == '[')
        {
            i += 2;
            while (capture[i] != '\0' && !(capture[i] >= '@' && capture[i] <= '~'))
                ++i;
            if (capture[i] == '\0')
                break;
        }
        else
            visible[length++] = capture[i];
    }
    visible[length] = '\0';
    return strstr(visible, needle) != NULL;
}
static bool read_until(int master, const char *needle, unsigned seconds, char *capture,
                       size_t capacity)
{
    size_t length = 0;
    uint64_t deadline = tired_monotonic_usec() + (uint64_t)seconds * 1000000;
    capture[0] = '\0';
    while (tired_monotonic_usec() < deadline)
    {
        struct pollfd descriptor = {.fd = master, .events = POLLIN};
        if (poll(&descriptor, 1, 100) <= 0)
            continue;
        ssize_t count = read(master, capture + length, capacity - length - 1);
        if (count <= 0)
            return false;
        length += (size_t)count;
        capture[length] = '\0';
        if (strstr(capture, needle) != NULL || drawn_text(capture, needle))
            return true;
        if (length + 1024 >= capacity)
        {
            memmove(capture, capture + length / 2, length - length / 2);
            length -= length / 2;
            capture[length] = '\0';
        }
    }
    return false;
}
static bool input_consumed(int slave)
{
    uint64_t deadline = tired_monotonic_usec() + 5000000;
    while (tired_monotonic_usec() < deadline)
    {
        int pending;
        if (ioctl(slave, FIONREAD, &pending) != 0)
            return false;
        if (pending == 0)
            return true;
        struct timespec pause = {.tv_nsec = 20000000};
        nanosleep(&pause, NULL);
    }
    return false;
}
static int scenario(const char *executable, bool dashboard, bool interrupt)
{
    int master = -1, slave = -1, result = 1;
    pid_t child = -1;
    struct winsize size = {.ws_row = 24, .ws_col = 100};
    struct termios initial, restored;
    char capture[65536];
    const char *stage = "open terminal";
    capture[0] = '\0';
    if (openpty(&master, &slave, NULL, NULL, &size) != 0 || tcgetattr(slave, &initial) != 0)
        goto cleanup;
    child = fork();
    if (child == 0)
    {
        close(master);
        setsid();
        ioctl(slave, TIOCSCTTY, 0);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        closefrom(3);
        setenv("TERM", "xterm", 1);
        setenv("LC_ALL", "C.UTF-8", 1);
        setenv("PATH", "/usr/bin:/bin", 1);
        if (dashboard)
            execl(executable, executable, (char *)NULL);
        else
            execl(executable, executable, "--profile", "none", "--working-directory", "/opt", "--",
                  "/usr/bin/sleep", "60", (char *)NULL);
        _exit(127);
    }
    if (child < 0 || !read_until(master, dashboard ? "system services" : "review service", 15,
                                 capture, sizeof(capture)))
    {
        fprintf(stderr, "PTY did not reach %s: %s\n", dashboard ? "dashboard" : "review", capture);
        goto cleanup;
    }
    if (!dashboard)
    {
        stage = "hardening preview";
        if (write(master, "H", 1) != 1 ||
            !read_until(master, "Baseline hardening", 5, capture, sizeof(capture)))
            goto cleanup;
        if (write(master, " ", 1) != 1 ||
            !read_until(master, "review service", 15, capture, sizeof(capture)))
            goto cleanup;
        stage = "preview";
        if (write(master, "p", 1) != 1 ||
            !read_until(master, "Generated unit", 5, capture, sizeof(capture)))
            goto cleanup;
        stage = "return from preview";
        if (write(master, "\033", 1) != 1 ||
            !read_until(master, "review service", 15, capture, sizeof(capture)))
            goto cleanup;
    }
    size.ws_row = 10;
    size.ws_col = 40;
    stage = "resize";
    if (ioctl(master, TIOCSWINSZ, &size) != 0 || kill(child, SIGWINCH) != 0 ||
        !read_until(master, "Resize", 15, capture, sizeof(capture)))
        goto cleanup;
    size.ws_row = 24;
    size.ws_col = 100;
    ioctl(master, TIOCSWINSZ, &size);
    kill(child, SIGWINCH);
    if (interrupt)
        kill(child, SIGINT);
    else if (write(master, "\033", 1) != 1)
        goto cleanup;
    uint64_t deadline = tired_monotonic_usec() + 10000000;
    int status = 0;
    pid_t waited;
    stage = "exit";
    while ((waited = waitpid(child, &status, WNOHANG)) == 0 && tired_monotonic_usec() < deadline)
    {
        struct timespec pause = {.tv_nsec = 20000000};
        nanosleep(&pause, NULL);
        struct pollfd descriptor = {.fd = master, .events = POLLIN};
        if (poll(&descriptor, 1, 0) > 0)
            (void)read(master, capture, sizeof(capture));
    }
    if (waited != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != (interrupt   ? 130
                                : dashboard ? 0
                                            : 10))
    {
        fprintf(stderr, "PTY status=%d waited=%ld expected=%d\n", status, (long)waited,
                interrupt   ? 130
                : dashboard ? 0
                            : 10);
        goto cleanup;
    }
    child = -1;
    stage = "terminal restoration";
    if (tcgetattr(slave, &restored) != 0 ||
        ((initial.c_lflag ^ restored.c_lflag) & (ICANON | ECHO | ISIG)) != 0 ||
        initial.c_cc[VMIN] != restored.c_cc[VMIN] || initial.c_cc[VTIME] != restored.c_cc[VTIME])
        goto cleanup;
    result = 0;
cleanup:
    if (result != 0)
        fprintf(stderr, "PTY failed at %s (%s, interrupt=%d): %.4096s\n", stage,
                dashboard ? "dashboard" : "review", interrupt, capture);
    if (child > 0)
    {
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
    }
    if (master >= 0)
        close(master);
    if (slave >= 0)
        close(slave);
    return result;
}
static int editor_resize(unsigned editor)
{
    int master = -1, slave = -1, result = 1, status;
    pid_t child = -1;
    struct winsize size = {.ws_row = 24, .ws_col = 100};
    struct termios initial, restored;
    char capture[65536] = {0};
    const char *titles[] = {"Prompt fixture", "Viewer fixture", "Environment:", "argv",
                            "Cancellation fixture"};
    const char *stage = "open editor";
    if (openpty(&master, &slave, NULL, NULL, &size) != 0 || tcgetattr(slave, &initial) != 0)
        goto cleanup;
    child = fork();
    if (child == 0)
    {
        close(master);
        setsid();
        ioctl(slave, TIOCSCTTY, 0);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        closefrom(3);
        setenv("TERM", "xterm", 1);
        (void)setlocale(LC_ALL, "C.UTF-8");
        SCREEN *screen = newterm(NULL, stdout, stdin);
        if (screen == NULL)
            _exit(2);
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        /* Reproduce a resize notification missed before the input wait. The
         * editors must reconcile the PTY dimensions without another key. */
        signal(SIGWINCH, SIG_IGN);
        TiredError error = {0};
        TiredMutation mutation = {0};
        TiredText value = {0};
        bool ok = true;
        if (editor == 0)
            ok = tired_ui_text_prompt(titles[editor], "original", &value, &error) &&
                 strcmp(value.data, "edited\342\234\223") == 0;
        else if (editor == 1)
            tired_ui_text_view(titles[editor], "Read-only fixture body\n");
        else if (editor == 2)
        {
            ok = tired_environment_set(&mutation.proposed.environment, "ONE=before", 10,
                                       TIRED_ENV_EXPLICIT, false, &error);
            if (ok)
                tired_ui_inputs(&mutation, false, &error);
            ok = ok && mutation.proposed.environment.count == 1 &&
                 strcmp(mutation.proposed.environment.items[0].value.data, "before") == 0;
        }
        else if (editor == 3)
        {
            ok = tired_spec_append(&mutation.proposed.spec, TIRED_FIELD_ARGV, "fixture", 7,
                                   TIRED_ORIGIN_CAPTURE, &error) &&
                 tired_spec_append(&mutation.proposed.spec, TIRED_FIELD_ARGV, "argument", 8,
                                   TIRED_ORIGIN_CAPTURE, &error) &&
                 tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_EXECUTABLE, "/fixture", 8,
                                TIRED_ORIGIN_CAPTURE, true, &error);
            if (ok)
                tired_ui_edit_list(&mutation, TIRED_FIELD_ARGV, &error);
            ok = ok && mutation.proposed.spec.fields[TIRED_FIELD_ARGV].value.list.count == 2;
        }
        else
        {
            set_escdelay(1000);
            tired_ui_draw(1, 2, titles[editor], COLS - 4);
            refresh();
            int key;
            do
                key = tired_ui_key(-1);
            while (key == KEY_RESIZE);
            ok = key == 27;
        }
        endwin();
        delscreen(screen);
        tired_text_destroy(&value);
        tired_mutation_destroy(&mutation);
        printf("RESULT: %s\n", ok ? "preserved" : "changed");
        fflush(stdout);
        _exit(ok ? 0 : 1);
    }
    if (child < 0 || !read_until(master, titles[editor], 5, capture, sizeof(capture)))
        goto cleanup;
    if (editor == 4)
    {
        stage = "cancel during resize";
        /* ncurses waits briefly for an escape-sequence continuation. Resize
         * after Escape has left the input queue, while that wait is active. */
        if (write(master, "\033", 1) != 1 || !input_consumed(slave))
            goto cleanup;
        size.ws_row = 5;
        size.ws_col = 20;
        if (ioctl(master, TIOCSWINSZ, &size) != 0 ||
            !read_until(master, "RESULT: preserved", 5, capture, sizeof(capture)))
            goto cleanup;
        goto wait_child;
    }
    if (editor == 0)
    {
        if (write(master, "\025edited", 7) != 7 ||
            !read_until(master, "edited", 5, capture, sizeof(capture)))
            goto cleanup;
        stage = "admit partial UTF-8 character";
        if (write(master, "\342", 1) != 1 || !input_consumed(slave))
            goto cleanup;
        /* Leave the character incomplete through an idle reconciliation before
         * resizing, after its first byte has left the terminal input queue. */
        struct timespec idle = {.tv_nsec = 300000000};
        nanosleep(&idle, NULL);
    }
    else if (editor == 3 && write(master, "\033OB", 3) != 3)
        goto cleanup; /* Select an editable argument, preserving argv[0]. */
    stage = "resize while editor is active";
    size.ws_row = 5;
    size.ws_col = 20;
    if (ioctl(master, TIOCSWINSZ, &size) != 0 || kill(child, SIGWINCH) != 0 ||
        !read_until(master, "Resize", 5, capture, sizeof(capture)))
        goto cleanup;
    const char *blocked = editor < 2 ? "\n" : "d";
    stage = "block hidden edits";
    if (write(master, blocked, 1) != 1 ||
        read_until(master, "RESULT:", 1, capture, sizeof(capture)))
        goto cleanup;
    size.ws_row = 24;
    size.ws_col = 100;
    stage = "restore editor";
    if (ioctl(master, TIOCSWINSZ, &size) != 0 || kill(child, SIGWINCH) != 0 ||
        !read_until(master, titles[editor], 5, capture, sizeof(capture)))
        goto cleanup;
    if (editor == 0)
    {
        stage = "complete UTF-8 character after resize";
        if (write(master, "\234\223", 2) != 2 ||
            !read_until(master, "\342\234\223", 5, capture, sizeof(capture)))
            goto cleanup;
    }
    if (write(master, editor == 0 ? "\n" : "\033", 1) != 1 ||
        !read_until(master, "RESULT: preserved", 5, capture, sizeof(capture)))
        goto cleanup;
wait_child:
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        goto cleanup;
    child = -1;
    stage = "terminal restoration";
    if (tcgetattr(slave, &restored) != 0 ||
        ((initial.c_lflag ^ restored.c_lflag) & (ICANON | ECHO | ISIG)) != 0)
        goto cleanup;
    result = 0;
cleanup:
    if (result != 0)
        fprintf(stderr, "Editor %u failed at %s: %.4096s\n", editor, stage, capture);
    if (child > 0)
    {
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
    }
    if (master >= 0)
        close(master);
    if (slave >= 0)
        close(slave);
    return result;
}
static int presentation_setting(const char *executable)
{
    int result = 1, master = -1, slave = -1;
    pid_t child = -1;
    char fixture[] = "settings-pty-XXXXXX", capture[65536];
    char *created = NULL, *cwd = NULL;
    TiredText root = {0}, directory = {0}, config = {0};
    TiredError error = {0};
    struct winsize size = {.ws_row = 24, .ws_col = 100};
    struct termios initial, restored;
    capture[0] = '\0';
    cwd = getcwd(NULL, 0);
    created = mkdtemp(fixture);
    if (cwd == NULL || created == NULL)
        goto cleanup;
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    const char settings[] = "{\"schema_version\":1,\"tui\":false}";
    if (!tired_path_absolute(&base, created, strlen(created), &root, &error) ||
        !tired_path_absolute(&root, "tired", 5, &directory, &error) ||
        mkdir(directory.data, 0700) != 0 ||
        !tired_path_absolute(&directory, "config.json", 11, &config, &error) ||
        !tired_write_private_new(config.data, settings, sizeof(settings) - 1, &error) ||
        openpty(&master, &slave, NULL, NULL, &size) != 0 || tcgetattr(slave, &initial) != 0)
        goto cleanup;
    child = fork();
    if (child == 0)
    {
        close(master);
        setsid();
        ioctl(slave, TIOCSCTTY, 0);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        setenv("TERM", "xterm", 1);
        setenv("XDG_CONFIG_HOME", root.data, 1);
        setenv("XDG_STATE_HOME", root.data, 1);
        setenv("XDG_RUNTIME_DIR", root.data, 1);
        closefrom(3);
        execl(executable, executable, "--user", (char *)NULL);
        _exit(127);
    }
    if (child < 0 || !read_until(master, "Help: tired --help", 10, capture, sizeof(capture)))
    {
        fprintf(stderr, "tui:false did not print the compact dashboard: %.4096s\n", capture);
        goto cleanup;
    }
    int status = 0;
    uint64_t deadline = tired_monotonic_usec() + 5000000;
    pid_t waited;
    while ((waited = waitpid(child, &status, WNOHANG)) == 0 && tired_monotonic_usec() < deadline)
    {
        struct timespec pause = {.tv_nsec = 20000000};
        nanosleep(&pause, NULL);
    }
    if (waited != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        goto cleanup;
    child = -1;
    if (strstr(capture, "\033[") != NULL || tcgetattr(slave, &restored) != 0 ||
        initial.c_lflag != restored.c_lflag)
        goto cleanup;
    result = 0;
cleanup:
    if (child > 0)
    {
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
    }
    if (master >= 0)
        close(master);
    if (slave >= 0)
        close(slave);
    if (config.data != NULL)
        (void)unlink(config.data);
    if (directory.data != NULL)
        (void)rmdir(directory.data);
    if (created != NULL && rmdir(created) != 0)
        result = 1;
    free(cwd);
    tired_text_destroy(&root);
    tired_text_destroy(&directory);
    tired_text_destroy(&config);
    return result;
}
int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--editors-only") == 0)
        return editor_resize(0) || editor_resize(1) || editor_resize(2) || editor_resize(3) ||
               editor_resize(4);
    if (argc == 3 && strcmp(argv[2], "--settings-only") == 0)
        return presentation_setting(argv[1]);
    if (argc != 2)
        return 2;
    TiredLayout layout = {0};
    TiredNativeBackend *native = NULL;
    TiredBackend backend = {0};
    TiredError error = {0};
    bool manager = tired_layout_discover(false, &layout, &error) &&
                   tired_backend_open(&layout, &native, &backend, &error);
    tired_backend_destroy(native);
    tired_layout_destroy(&layout);
    if (!manager)
    {
        fputs("SKIP: actual frontend PTY integration requires a usable read-only system manager.\n",
              stdout);
        return 77;
    }
    return scenario(argv[1], false, false) || scenario(argv[1], false, true) ||
           scenario(argv[1], true, false) || scenario(argv[1], true, true);
}
