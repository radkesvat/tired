#define _GNU_SOURCE
#include "tired/io.h"
#include "tired/process.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-journal.h>
#include <time.h>
#include <unistd.h>

static const char message_prefix[] = "MESSAGE=tired-journal-format-fixture:";
enum
{
    MESSAGE_LENGTH = 16384
};

static bool export_fixture(const char *path)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        return false;
    FILE *file = fopen(path, "wx");
    if (file == NULL)
        return false;
    bool ok = fprintf(file,
                      "__REALTIME_TIMESTAMP=%" PRIu64 "\n"
                      "__MONOTONIC_TIMESTAMP=1000000\n"
                      "_BOOT_ID=1123456789abcdef0123456789abcdef\n%s",
                      (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U,
                      message_prefix) > 0;
    for (size_t i = sizeof(message_prefix) - 1; ok && i < MESSAGE_LENGTH; ++i)
        ok = fputc('a', file) != EOF;
    ok = fputs("\n\n", file) >= 0 && ok;
    return fclose(file) == 0 && ok;
}

static bool read_fixture(const char *path, bool sealed)
{
    unsigned char header[16];
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return false;
    bool ok = fread(header, 1, sizeof(header), file) == sizeof(header);
    ok = fclose(file) == 0 && ok;
    /* Compatible bit 0 is sealing; incompatible bits 0, 1, 3 are XZ/LZ4/Zstd. */
    if (!ok || memcmp(header, "LPKSHHRH", 8) != 0 || (header[12] & 0x0b) == 0 ||
        (sealed && (header[8] & 1) == 0))
    {
        fputs("Fixture does not have the requested journal format flags.\n", stderr);
        return false;
    }
    sd_journal *journal = NULL;
    const char *paths[] = {path, NULL};
    const void *data = NULL;
    size_t length = 0;
    ok = sd_journal_open_files(&journal, paths, 0) >= 0 &&
         sd_journal_set_data_threshold(journal, 0) >= 0 && sd_journal_next(journal) == 1 &&
         sd_journal_get_data(journal, "MESSAGE", &data, &length) >= 0 && length == MESSAGE_LENGTH &&
         memcmp(data, message_prefix, sizeof(message_prefix) - 1) == 0;
    for (size_t i = sizeof(message_prefix) - 1; ok && i < length; ++i)
        ok = ((const unsigned char *)data)[i] == 'a';
    if (ok)
        ok = sd_journal_next(journal) == 0;
    sd_journal_close(journal);
    if (ok)
        printf("Read compressed journal; sealed=%s, flags=0x%02x.\n",
               (header[8] & 1) ? "yes" : "no", header[12]);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--export") == 0)
        return export_fixture(argv[2]) ? 0 : 1;
    if ((argc == 3 || argc == 4) && strcmp(argv[1], "--read") == 0)
        return read_fixture(argv[2], argc == 4 && strcmp(argv[3], "--sealed") == 0) ? 0 : 1;
    if (argc != 2)
        return 2;
    char directory[] = "journal-format-test-XXXXXX";
    if (mkdtemp(directory) == NULL)
        return 1;
    char *root = realpath(directory, NULL);
    char input[4096], output[4096];
    int a = root == NULL ? -1 : snprintf(input, sizeof(input), "%s/input.export", root);
    int b = root == NULL ? -1 : snprintf(output, sizeof(output), "%s/output.journal", root);
    if (a < 0 || (size_t)a >= sizeof(input) || b < 0 || (size_t)b >= sizeof(output))
    {
        free(root);
        (void)rmdir(directory);
        return 1;
    }
    TiredProcess *process = NULL;
    TiredError error = {0};
    char *arguments[] = {argv[1],    "--compress=yes", "--seal=no", "--split-mode=none",
                         "--output", output,           input,       NULL};
    char *environment[] = {"PATH=/usr/bin:/bin", "LC_ALL=C", NULL};
    bool ok = export_fixture(input) &&
              tired_process_start(argv[1], arguments, environment, 30000, 65536, &process, &error);
    if (ok)
    {
        while (!tired_process_step(process))
        {
            struct timespec delay = {.tv_nsec = 10000000};
            nanosleep(&delay, NULL);
        }
        TiredProcessResult result = tired_process_result(process);
        ok = result.outcome == TIRED_PROCESS_EXITED && result.exit_code == 0;
        if (!ok)
            fprintf(stderr, "%.*s\n", (int)result.error_length, result.standard_error);
    }
    tired_process_destroy(process);
    if (ok)
        ok = read_fixture(output, false);
    (void)unlink(input);
    (void)unlink(output);
    if (rmdir(directory) != 0)
        ok = false;
    free(root);
    return ok ? 0 : 1;
}
