#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <systemd/sd-daemon.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t stopped;
static void stop(int number) { stopped = number; }
static bool number(const char *text, unsigned *output)
{
    unsigned value = 0;
    if (*text == '\0')
        return false;
    for (; *text != '\0'; ++text)
    {
        if (*text < '0' || *text > '9' || value > (1000000U - (unsigned)(*text - '0')) / 10)
            return false;
        value = value * 10 + (unsigned)(*text - '0');
    }
    *output = value;
    return true;
}
static void hex(FILE *stream, const char *label, const char *value)
{
    if (value == NULL)
        value = "";
    fprintf(stream, "%s %zu ", label, strlen(value));
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p)
        fprintf(stream, "%02x", *p);
    fputc('\n', stream);
}
static int report(int argc, char **argv)
{
    if (argc < 3)
        return 2;
    int fd = open(argv[2], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return 2;
    FILE *stream = fdopen(fd, "w");
    if (stream == NULL)
    {
        close(fd);
        return 2;
    }
    fprintf(stream, "argc %d\nuid %lu\ngid %lu\n", argc - 3, (unsigned long)getuid(),
            (unsigned long)getgid());
    gid_t groups[128];
    int count = getgroups(128, groups);
    fprintf(stream, "groups");
    for (int i = 0; i < count; ++i)
        fprintf(stream, " %lu", (unsigned long)groups[i]);
    fprintf(stream, "\nnice %d\n", getpriority(PRIO_PROCESS, 0));
    char *directory = getcwd(NULL, 0);
    hex(stream, "directory", directory);
    free(directory);
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) == 0)
        fprintf(stream, "nofile %llu %llu\n", (unsigned long long)limit.rlim_cur,
                (unsigned long long)limit.rlim_max);
    for (int i = 3; i < argc; ++i)
        hex(stream, "arg", argv[i]);
    hex(stream, "environment", getenv("TIRED_TEST_ENV"));
    hex(stream, "token", getenv("TIRED_TEST_TOKEN"));
    bool ok = fflush(stream) == 0 && fsync(fd) == 0;
    return fclose(stream) == 0 && ok ? 0 : 2;
}
static int loop(void)
{
    struct sigaction action = {.sa_handler = stop};
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    while (!stopped)
    {
        printf("fixture running uid=%lu pid=%lu\n", (unsigned long)getuid(),
               (unsigned long)getpid());
        fflush(stdout);
        struct timespec pause = {.tv_sec = 1};
        while (nanosleep(&pause, &pause) != 0 && errno == EINTR && !stopped)
        {
        }
    }
    return 0;
}
int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    if (strcmp(argv[1], "report") == 0)
        return report(argc, argv);
    if (strcmp(argv[1], "exit") == 0 && argc == 3)
    {
        unsigned value;
        return number(argv[2], &value) && value <= 255 ? (int)value : 2;
    }
    if (strcmp(argv[1], "signal") == 0 && argc == 3)
    {
        unsigned value;
        if (!number(argv[2], &value) || value == 0 || value > 31)
            return 2;
        raise((int)value);
        return 1;
    }
    if (strcmp(argv[1], "marker") == 0 && argc == 3)
    {
        int fd = open(argv[2], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0)
            return 2;
        if (write(fd, "executed\n", 9) != 9 || close(fd) != 0)
            return 2;
        return loop();
    }
    if (strcmp(argv[1], "notify") == 0)
    {
        unsigned delay = 0;
        if (argc > 2 && !number(argv[2], &delay))
            return 2;
        struct timespec pause = {.tv_sec = delay / 1000, .tv_nsec = (long)(delay % 1000) * 1000000};
        while (nanosleep(&pause, &pause) != 0 && errno == EINTR)
        {
        }
        if (sd_notify(0, "READY=1") <= 0)
            return 2;
        return loop();
    }
    if (strcmp(argv[1], "fork") == 0 && argc == 3)
    {
        pid_t child = fork();
        if (child < 0)
            return 2;
        if (child > 0)
        {
            FILE *file = fopen(argv[2], "wx");
            if (file == NULL)
                return 2;
            fprintf(file, "%lu\n", (unsigned long)child);
            return fclose(file) == 0 ? 0 : 2;
        }
        setsid();
        return loop();
    }
    if (strcmp(argv[1], "fail-times") == 0 && argc == 4)
    {
        unsigned maximum;
        if (!number(argv[3], &maximum))
            return 2;
        unsigned count = 0;
        FILE *file = fopen(argv[2], "r");
        if (file != NULL)
        {
            char value[32];
            bool valid = fgets(value, sizeof(value), file) != NULL;
            size_t length = valid ? strlen(value) : 0;
            if (length > 0 && value[length - 1] == '\n')
                value[length - 1] = '\0';
            if (!valid || !number(value, &count) || count >= 1000000)
            {
                fclose(file);
                return 2;
            }
            fclose(file);
        }
        file = fopen(argv[2], "w");
        if (file == NULL)
            return 2;
        fprintf(file, "%u\n", count + 1);
        if (fclose(file) != 0)
            return 2;
        if (count < maximum)
            return 1;
        return loop();
    }
    if (strcmp(argv[1], "spawn") == 0)
    {
        if (fork() < 0)
            return 2;
        return loop();
    }
    if (strcmp(argv[1], "ignore-term") == 0)
    {
        signal(SIGTERM, SIG_IGN);
        for (;;)
            pause();
    }
    if (strcmp(argv[1], "loop") == 0)
        return loop();
    return 2;
}
