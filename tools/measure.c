#define _GNU_SOURCE
#include "tired/backend.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

static bool usage(pid_t pid, unsigned long *rss, unsigned long long *ticks)
{
    char path[96], bytes[4096];
    snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    FILE *file = fopen(path, "r");
    if (file == NULL)
        return false;
    bool ok = fgets(bytes, sizeof(bytes), file) != NULL;
    fclose(file);
    char *position = ok ? strrchr(bytes, ')') : NULL;
    if (position == NULL)
        return false;
    unsigned long long user = 0, system = 0;
    char *save = NULL, *token = strtok_r(position + 2, " ", &save);
    for (unsigned field = 3; token != NULL && field <= 15; ++field)
    {
        if (field == 14)
            user = strtoull(token, NULL, 10);
        if (field == 15)
            system = strtoull(token, NULL, 10);
        token = strtok_r(NULL, " ", &save);
    }
    *ticks = user + system;
    snprintf(path, sizeof(path), "/proc/%ld/status", (long)pid);
    file = fopen(path, "r");
    if (file == NULL)
        return false;
    ok = false;
    while (fgets(bytes, sizeof(bytes), file) != NULL)
        if (sscanf(bytes, "VmRSS: %lu kB", rss) == 1)
            ok = true;
    fclose(file);
    return ok;
}

int main(int argc, char **argv)
{
    if ((argc != 4 && argc != 5) || argv[1][0] != '/' ||
        (strcmp(argv[2], "review") != 0 && strcmp(argv[2], "dashboard") != 0 &&
         !(strcmp(argv[2], "logs") == 0 && argc == 5)))
    {
        fputs("Usage: tired_measure ABSOLUTE_FRONTEND review|dashboard|logs SECONDS [UNIT]\n",
              stderr);
        return 2;
    }
    char *end = NULL;
    errno = 0;
    unsigned long seconds = strtoul(argv[3], &end, 10);
    if (errno || end == argv[3] || *end || seconds == 0 || seconds > 600)
        return 2;
    struct winsize size = {.ws_row = 24, .ws_col = 100};
    int master = -1;
    uint64_t began = tired_monotonic_usec();
    pid_t child = forkpty(&master, NULL, NULL, &size);
    if (child == 0)
    {
        setenv("TERM", "xterm", 1);
        setenv("LC_ALL", "C.UTF-8", 1);
        if (strcmp(argv[2], "review") == 0)
            execl(argv[1], argv[1], "--profile", "none", "--", "/usr/bin/sleep", "60",
                  (char *)NULL);
        else if (strcmp(argv[2], "logs") == 0)
            execl(argv[1], argv[1], "logs", argv[4], "--follow", "--json", (char *)NULL);
        else
            execl(argv[1], argv[1], (char *)NULL);
        _exit(127);
    }
    if (child < 0)
        return 1;
    const char *needle = strcmp(argv[2], "review") == 0      ? "review service"
                         : strcmp(argv[2], "dashboard") == 0 ? "system services"
                                                             : "schema_version";
    char capture[65536] = {0};
    size_t used = 0;
    uint64_t ready = 0, deadline = began + UINT64_C(60000000);
    unsigned long first_rss = 0, final_rss = 0, peak_rss = 0;
    unsigned long warm_rss = 0;
    unsigned long long first_ticks = 0, final_ticks = 0;
    int result = 1, status = 0;
    while (tired_monotonic_usec() < deadline)
    {
        struct pollfd input = {.fd = master, .events = POLLIN};
        if (poll(&input, 1, 100) > 0)
        {
            ssize_t count = read(master, capture + used, sizeof(capture) - used - 1);
            if (count <= 0)
                break;
            used += (size_t)count;
            capture[used] = '\0';
            if (ready == 0 && strstr(capture, needle) != NULL)
            {
                ready = tired_monotonic_usec();
                deadline = ready + (uint64_t)seconds * 1000000;
                if (!usage(child, &first_rss, &first_ticks))
                    break;
            }
            if (used > sizeof(capture) / 2)
            {
                memmove(capture, capture + used / 2, used - used / 2);
                used -= used / 2;
                capture[used] = '\0';
            }
        }
        if (ready && usage(child, &final_rss, &final_ticks))
        {
            if (final_rss > peak_rss)
                peak_rss = final_rss;
            if (warm_rss == 0 && tired_monotonic_usec() - ready >= UINT64_C(30000000))
                warm_rss = final_rss;
        }
    }
    if (ready && usage(child, &final_rss, &final_ticks))
    {
        printf("mode=%s startup_ms=%.3f duration_s=%lu rss_start_kib=%lu rss_end_kib=%lu "
               "rss_peak_kib=%lu rss_at_30s_kib=%lu cpu_seconds=%.3f\n",
               argv[2], (double)(ready - began) / 1000, seconds, first_rss, final_rss, peak_rss,
               warm_rss, (double)(final_ticks - first_ticks) / sysconf(_SC_CLK_TCK));
        result = 0;
    }
    kill(child, SIGINT);
    deadline = tired_monotonic_usec() + UINT64_C(10000000);
    while (waitpid(child, &status, WNOHANG) == 0 && tired_monotonic_usec() < deadline)
    {
        struct pollfd input = {.fd = master, .events = POLLIN};
        if (poll(&input, 1, 100) > 0)
            (void)read(master, capture, sizeof(capture));
    }
    if (waitpid(child, &status, WNOHANG) == 0)
    {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
        result = 1;
    }
    close(master);
    return result;
}
