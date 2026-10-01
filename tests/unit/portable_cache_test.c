#define _GNU_SOURCE
#include "tired/build_identity.h"
#include "tired/io.h"
#include "tired/portable.h"
#include "tired/process.h"
#include "tired/sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHECK(e)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(e))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%d: %s [%s]\n", __LINE__, #e,                                         \
                    error.code == NULL ? "none" : error.code);                                     \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)

static int remove_entry(const char *path, const struct stat *status, int kind, struct FTW *walk)
{
    (void)status;
    (void)kind;
    (void)walk;
    return remove(path);
}

int main(void)
{
    int result = 1, fd = -1;
    char fixture[] = "portable-cache-test-XXXXXX";
    char *created = mkdtemp(fixture), *absolute = NULL;
    TiredDirectory *directory = NULL;
    TiredError error = {0};
    TiredProcess *process = NULL;
    bool found = false;
    char version[4096], path[4096], link_path[4096];
    const TiredEmbeddedFile *file = &tired_embedded_helper[0];
    CHECK(created != NULL && (absolute = realpath(created, NULL)) != NULL);
    CHECK(tired_directory_open(absolute, getuid(), false, &directory, &error));
    CHECK(tired_helper_cache_find(directory, file, &found, &error) && !found);
    CHECK(tired_helper_cache_install(directory, file, &error));
    CHECK(tired_helper_cache_find(directory, file, &found, &error) && found);
    CHECK(snprintf(version, sizeof(version), "%s/%s", absolute, file->sha256) > 0);
    CHECK(snprintf(path, sizeof(path), "%s/tired-helper", version) > 0);
    CHECK(snprintf(link_path, sizeof(link_path), "%s/other", version) > 0);
    struct stat original, current;
    CHECK(stat(path, &original) == 0 && (original.st_mode & 07777) == 0755);
    CHECK(tired_helper_cache_install(directory, file, &error));
    CHECK(stat(path, &current) == 0 && original.st_ino == current.st_ino);
    char *arguments[] = {path, "--build-id", NULL};
    char *environment[] = {"PATH=/usr/bin:/bin", "LC_ALL=C", NULL};
    CHECK(tired_process_start(path, arguments, environment, 5000, 512, &process, &error));
    struct timespec delay = {.tv_nsec = 10000000};
    while (!tired_process_step(process))
        (void)nanosleep(&delay, NULL);
    TiredProcessResult finished = tired_process_result(process);
    CHECK(finished.outcome == TIRED_PROCESS_EXITED && finished.exit_code == 0 &&
          finished.output_length == strlen(tired_build_identity) + 1 &&
          memcmp(finished.standard_output, tired_build_identity, strlen(tired_build_identity)) ==
              0);
    tired_process_destroy(process);
    process = NULL;
    CHECK(chmod(path, 0777) == 0);
    CHECK(!tired_helper_cache_install(directory, file, &error));
    CHECK(chmod(path, 0755) == 0);
    CHECK(link(path, link_path) == 0);
    CHECK(!tired_helper_cache_find(directory, file, &found, &error));
    CHECK(unlink(link_path) == 0);
    fd = open(path, O_WRONLY);
    CHECK(fd >= 0 && pwrite(fd, "broken!", 7, 0) == 7);
    CHECK(close(fd) == 0);
    fd = -1;
    CHECK(!tired_helper_cache_install(directory, file, &error));
    char bytes[7];
    fd = open(path, O_RDONLY);
    CHECK(fd >= 0 && read(fd, bytes, sizeof(bytes)) == sizeof(bytes) &&
          memcmp(bytes, "broken!", sizeof(bytes)) == 0);
    CHECK(close(fd) == 0);
    fd = -1;
    CHECK(unlink(path) == 0 && symlink("/bin/true", path) == 0);
    CHECK(!tired_helper_cache_install(directory, file, &error));
    CHECK(unlink(path) == 0 && mkdir(path, 0755) == 0);
    CHECK(!tired_helper_cache_install(directory, file, &error));
    CHECK(rmdir(path) == 0);
    /* Concurrent first use publishes one complete immutable file. */
    pid_t children[4];
    for (size_t i = 0; i < 4; ++i)
    {
        children[i] = fork();
        CHECK(children[i] >= 0);
        if (children[i] == 0)
            _exit(tired_helper_cache_install(directory, file, &error) ? 0 : 1);
    }
    for (size_t i = 0; i < 4; ++i)
    {
        int status;
        CHECK(waitpid(children[i], &status, 0) == children[i] && WIFEXITED(status) &&
              WEXITSTATUS(status) == 0);
    }
    CHECK(tired_helper_cache_find(directory, file, &found, &error) && found);
    /* A different build gets a separate entry and leaves the previous one intact. */
    const unsigned char update[] = "different build";
    char digest[TIRED_SHA256_HEX_SIZE];
    tired_sha256(update, sizeof(update), digest);
    TiredEmbeddedFile next = {
        .name = "tired-helper", .data = update, .length = sizeof(update), .sha256 = digest};
    CHECK(tired_helper_cache_install(directory, &next, &error));
    CHECK(tired_helper_cache_find(directory, &next, &found, &error) && found);
    CHECK(tired_helper_cache_find(directory, file, &found, &error) && found);
    CHECK(unlink(path) == 0 && rmdir(version) == 0 && symlink(absolute, version) == 0);
    CHECK(!tired_helper_cache_install(directory, file, &error));
    CHECK(unlink(version) == 0);
    CHECK(chmod(absolute, 0777) == 0);
    CHECK(!tired_helper_cache_install(directory, file, &error));
    CHECK(chmod(absolute, 0700) == 0);
    result = 0;
cleanup:
    if (fd >= 0)
        close(fd);
    tired_process_destroy(process);
    tired_directory_destroy(directory);
    if (created != NULL && nftw(created, remove_entry, 16, FTW_DEPTH | FTW_PHYS) != 0)
        result = 1;
    free(absolute);
    return result;
}
