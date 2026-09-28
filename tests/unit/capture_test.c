#include "tired/capture.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)

static bool append(TiredTextList *args, const char *text)
{
    return tired_text_list_append(args, text, strlen(text), TIRED_ARGUMENT_LIMIT, TIRED_INPUT_LIMIT,
                                  NULL);
}

int main(void)
{
    int result = 1;
    char fixture[] = "/tmp/tired-capture-XXXXXX";
    char *root = NULL;
    int original = -1;
    int fd = -1;
    TiredTextList args = {0};
    TiredInvocation invocation = {0};
    TiredError error = {0};
    CHECK(tired_validate_text("é😀\n\t", strlen("é😀\n\t"), false, &error));
    CHECK(!tired_validate_text("a\nb", 3, true, &error));
    CHECK(!tired_validate_text("a\0b", 3, false, &error));
    const char *invalid[] = {"\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\x80",
                             "\xe2\x82", "\xff",         "\xe2x\xa0"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        CHECK(!tired_validate_text(invalid[i], strlen(invalid[i]), false, &error));
    original = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(original >= 0);
    root = mkdtemp(fixture);
    CHECK(root != NULL);
    CHECK(chdir(root) == 0);
    CHECK(mkdir("versions", 0700) == 0);
    CHECK(mkdir("versions/v1", 0700) == 0);
    fd = open("versions/program", O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0700);
    CHECK(fd >= 0);
    /* Deliberately not executable content: capture must never run this file. */
    CHECK(write(fd, "not a program\n", 14) == 14);
    CHECK(close(fd) == 0);
    fd = -1;
    CHECK(symlink("versions/v1", "current") == 0);
    CHECK(symlink("versions/program", "tool space'quote") == 0);
    CHECK(append(&args, "current/../program"));
    const char *values[] = {"",   ";",  "$HOME", "${TOKEN}", "%n", "a\nb",
                            "\\", "\"", "é",     "--help",   ">"};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        CHECK(append(&args, values[i]));
    CHECK(tired_invocation_capture(&args, NULL, &invocation, &error));
    CHECK(strcmp(invocation.directory.data, root) == 0);
    CHECK(strstr(invocation.executable.data, "/current/../program") != NULL);
    CHECK(strstr(invocation.resolved_target.data, "/versions/program") != NULL);
    CHECK(invocation.uid == getuid() && invocation.gid == getgid());
    CHECK(invocation.argv.count == args.count);
    for (size_t i = 0; i < args.count; ++i)
        CHECK(invocation.argv.items[i].length == args.items[i].length &&
              memcmp(invocation.argv.items[i].data, args.items[i].data, args.items[i].length) == 0);
    CHECK(tired_text_set(&args.items[0], "missing", 7, 100, &error));
    CHECK(!tired_invocation_capture(&args, NULL, &invocation, &error));
    CHECK(strcmp(error.code, "path-unset") == 0);
    CHECK(strstr(invocation.executable.data, "/current/../program") != NULL);
    CHECK(!tired_invocation_capture(&args, ":/missing:", &invocation, &error));
    CHECK(strcmp(error.code, "command-not-found") == 0);
    tired_text_list_destroy(&args);
    CHECK(append(&args, "tool space'quote"));
    CHECK(tired_invocation_capture(&args, "/missing::", &invocation, &error));
    CHECK(strstr(invocation.executable.data, "tool space'quote") != NULL);
    CHECK(strstr(invocation.resolved_target.data, "/versions/program") != NULL);
    CHECK(chmod("versions/program", 0600) == 0);
    CHECK(!tired_invocation_capture(&args, "", &invocation, &error));
    CHECK(chmod("versions/program", 0700) == 0);
    tired_text_list_destroy(&args);
    CHECK(append(&args, "./versions"));
    CHECK(!tired_invocation_capture(&args, NULL, &invocation, &error));
    CHECK(strcmp(error.code, "executable-type") == 0);
    tired_text_list_destroy(&args);
    CHECK(append(&args, "./tool space'quote"));
    CHECK(append(&args, "\xc0\x80"));
    CHECK(!tired_invocation_capture(&args, NULL, &invocation, &error));
    CHECK(strcmp(error.code, "invalid-utf8") == 0);
    result = 0;
cleanup:
    if (fd >= 0)
        (void)close(fd);
    tired_text_list_destroy(&args);
    tired_invocation_destroy(&invocation);
    tired_invocation_destroy(&invocation);
    if (root != NULL)
    {
        if (chdir(root) == 0)
        {
            (void)unlink("tool space'quote");
            (void)unlink("current");
            (void)unlink("versions/program");
            (void)rmdir("versions/v1");
            (void)rmdir("versions");
        }
    }
    if (original >= 0)
    {
        if (fchdir(original) != 0)
            result = 1;
        (void)close(original);
    }
    if (root != NULL && rmdir(root) != 0)
    {
        fprintf(stderr, "Fixture cleanup failed: %s\n", root);
        result = 1;
    }
    return result;
}
