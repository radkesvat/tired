#define _GNU_SOURCE
#include "tired/payload.h"
#include "tired/encode.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
bool tired_payload_path(bool helper, TiredText *path, TiredError *error)
{
    char executable[4097];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    const char *relative = helper ? TIRED_HELPER_RELATIVE : TIRED_PROFILES_RELATIVE;
    const char *fallback = helper ? TIRED_HELPER_PATH : TIRED_PROFILE_PATH;
    if (length > 0 && (size_t)length < sizeof(executable) - 1)
    {
        executable[length] = '\0';
        char *slash = strrchr(executable, '/');
        if (slash != NULL && strcmp(slash + 1, "tired") == 0)
        {
            TiredBuffer candidate;
            tired_buffer_init(&candidate, 4096);
            bool built =
                tired_buffer_append(&candidate, executable, (size_t)(slash - executable), error) &&
                tired_buffer_append(&candidate, "/", 1, error) &&
                tired_buffer_append(&candidate, relative, strlen(relative), error);
            if (helper && built)
            {
                struct stat entry;
                if (lstat(candidate.data, &entry) == 0)
                {
                    /* Keep every component for the administrative trust walk;
                     * resolving a caller-owned link could select another tool. */
                    bool copied =
                        tired_text_set(path, candidate.data, candidate.length, 4096, error);
                    tired_buffer_destroy(&candidate);
                    return copied;
                }
            }
            char *normalized = built && !helper ? realpath(candidate.data, NULL) : NULL;
            tired_buffer_destroy(&candidate);
            if (normalized != NULL)
            {
                struct stat entry;
                bool suitable = lstat(normalized, &entry) == 0 &&
                                (helper ? S_ISREG(entry.st_mode) : S_ISDIR(entry.st_mode));
                if (suitable)
                {
                    bool copied = tired_text_set(path, normalized, strlen(normalized), 4096, error);
                    free(normalized);
                    return copied;
                }
                free(normalized);
            }
        }
    }
    return tired_text_set(path, fallback, strlen(fallback), 4096, error);
}
