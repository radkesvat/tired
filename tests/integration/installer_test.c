#define _GNU_SOURCE
#include "tired/encode.h"
#include "tired/io.h"
#include "tired/mutation.h"
#include "tired/process.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
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
static bool run(char *const args[], char *const environment[], int expected, TiredError *error)
{
    TiredProcess *process = NULL;
    if (!tired_process_start(args[0], args, environment, 180000, TIRED_INPUT_LIMIT, &process,
                             error))
        return false;
    while (!tired_process_step(process))
    {
        struct timespec delay = {.tv_nsec = 20000000};
        nanosleep(&delay, NULL);
    }
    TiredProcessResult result = tired_process_result(process);
    bool ok = result.outcome == TIRED_PROCESS_EXITED && result.exit_code == expected;
    if (!ok)
        fprintf(stderr, "%s: outcome %d exit %d (expected %d)\n%.*s\n%.*s\n", args[0],
                result.outcome, result.exit_code, expected, (int)result.output_length,
                result.standard_output, (int)result.error_length, result.standard_error);
    tired_process_destroy(process);
    return ok;
}
static bool join(char output[4096], const char *root, const char *leaf)
{
    int length = snprintf(output, 4096, "%s/%s", root, leaf);
    return length > 0 && length < 4096;
}
static bool remove_tree(const char *path)
{
    struct stat info;
    if (lstat(path, &info) != 0)
        return errno == ENOENT;
    if (!S_ISDIR(info.st_mode))
        return unlink(path) == 0;
    DIR *directory = opendir(path);
    if (directory == NULL)
        return false;
    bool ok = true;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char child[4096];
        ok = join(child, path, entry->d_name) && remove_tree(child) && ok;
    }
    ok = closedir(directory) == 0 && ok;
    return rmdir(path) == 0 && ok;
}
static bool checksums(const char *archive, const char *directory, const char *name,
                      TiredError *error)
{
    TiredText bytes = {0};
    char sha[65], filename[4096], row[512];
    bool ok = tired_read_file(archive, 64 * 1024 * 1024, &bytes, error) &&
              tired_digest_bytes(bytes.data, bytes.length, sha, error) &&
              join(filename, directory, "SHA256SUMS");
    if (ok)
    {
        int length = snprintf(row, sizeof(row), "%s  %s\n", sha, name);
        ok = length > 0 && (size_t)length < sizeof(row);
        if (ok)
        {
            (void)unlink(filename);
            ok = tired_write_private_new(filename, row, (size_t)length, error);
        }
    }
    tired_text_destroy(&bytes);
    return ok;
}
int main(int argc, char **argv)
{
    if (argc != 6 && argc != 7)
        return 2;
    if (strcmp(argv[4], "DIRECT") != 0)
        return 77;
    int result = 1, socket_fd = -1;
    char fixture[] = "installer-test-XXXXXX", *created = NULL, *absolute = NULL,
         *original = getcwd(NULL, 0);
    char stage[4096], server_root[4096], certificate[4096], key[4096], destination[4096],
        artifact[4096], prefix[4096];
    char archive_name[256], archive_root[256], endpoint[128], port[16], listener[64], config[4096],
        ca_env[4200];
    TiredProcess *server = NULL;
    TiredError error = {0};
#if defined(__aarch64__)
    const char *arch = "arm64";
#else
    const char *arch = "amd64";
#endif
    char *environment[] = {"PATH=/usr/bin:/bin", "LC_ALL=C", NULL, NULL, NULL};
    CHECK(original != NULL && (created = mkdtemp(fixture)) != NULL &&
          (absolute = realpath(created, NULL)) != NULL);
    int length = snprintf(archive_root, sizeof(archive_root), "v%s", argv[5]);
    CHECK(length > 0 && (size_t)length < sizeof(archive_root));
    CHECK(join(server_root, absolute, archive_root) &&
          join(certificate, absolute, "certificate.pem") && join(key, absolute, "key.pem") &&
          join(prefix, absolute, "prefix"));
    CHECK(mkdir(server_root, 0700) == 0);
    length = snprintf(archive_root, sizeof(archive_root), "tired-%s-linux-%s", argv[5], arch);
    CHECK(length > 0 && (size_t)length < sizeof(archive_root));
    length = snprintf(archive_name, sizeof(archive_name), "%s.tar.gz", archive_root);
    CHECK(length > 0 && (size_t)length < sizeof(archive_name));
    CHECK(join(artifact, server_root, archive_name) && join(destination, absolute, archive_root));
    if (argc == 7)
    {
        TiredText payload = {0};
        bool copied = tired_read_file(argv[6], 64 * 1024 * 1024, &payload, &error) &&
                      tired_write_private_new(artifact, payload.data, payload.length, &error);
        tired_text_destroy(&payload);
        CHECK(copied);
    }
    else
    {
        char source_stage[4096], source_archive[4096], source_root[4096], source_name[256];
        CHECK(join(source_stage, absolute, "source-package") && mkdir(source_stage, 0700) == 0);
        CHECK(join(config, argv[3], "CPackSourceConfig.cmake"));
        char *source_package[] = {argv[2], "--config", config, "-B", source_stage, NULL};
        CHECK(run(source_package, environment, 0, &error));
        length = snprintf(source_name, sizeof(source_name), "tired-%s.tar.gz", argv[5]);
        CHECK(length > 0 && (size_t)length < sizeof(source_name) &&
              join(source_archive, source_stage, source_name));
        char *source_extract[] = {"/usr/bin/tar", "-xzf", source_archive, "-C", source_stage, NULL};
        CHECK(run(source_extract, environment, 0, &error));
        length = snprintf(source_name, sizeof(source_name), "tired-%s", argv[5]);
        CHECK(length > 0 && (size_t)length < sizeof(source_name) &&
              join(source_root, source_stage, source_name) &&
              join(source_archive, source_root, "CMakeLists.txt"));
        CHECK(access(source_archive, R_OK) == 0);
        CHECK(join(source_archive, source_root, "review_1.md") &&
              access(source_archive, F_OK) != 0 && errno == ENOENT);
        puts("PASS: source archive keeps its buildable root and excludes local review notes.");
        CHECK(join(config, argv[3], "CPackConfig.cmake"));
        char *package[] = {argv[2], "--config", config, "-G", "TGZ", "-B", server_root, NULL};
        CHECK(run(package, environment, 0, &error));
    }
    CHECK(checksums(artifact, server_root, archive_name, &error));
    char obsolete_payload[4096], obsolete_installed[4096];
    char *pack[] = {"/usr/bin/tar", "-czf", artifact, "-C", absolute, archive_root, NULL};
    char *certificate_args[] = {"/usr/bin/openssl",
                                "req",
                                "-x509",
                                "-newkey",
                                "rsa:2048",
                                "-nodes",
                                "-keyout",
                                key,
                                "-out",
                                certificate,
                                "-days",
                                "1",
                                "-subj",
                                "/CN=127.0.0.1",
                                "-addext",
                                "subjectAltName=IP:127.0.0.1",
                                NULL};
    CHECK(run(certificate_args, environment, 0, &error));
    socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t size = sizeof(address);
    CHECK(socket_fd >= 0 && bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) == 0 &&
          getsockname(socket_fd, (struct sockaddr *)&address, &size) == 0);
    (void)snprintf(port, sizeof(port), "%u", ntohs(address.sin_port));
    (void)snprintf(endpoint, sizeof(endpoint), "https://127.0.0.1:%s", port);
    (void)snprintf(listener, sizeof(listener), "127.0.0.1:%s", port);
    close(socket_fd);
    socket_fd = -1;
    CHECK(chdir(absolute) == 0);
    char *serve[] = {"/usr/bin/openssl",
                     "s_server",
                     "-accept",
                     listener,
                     "-cert",
                     certificate,
                     "-key",
                     key,
                     "-WWW",
                     "-quiet",
                     NULL};
    CHECK(tired_process_start(serve[0], serve, environment, 300000, TIRED_INPUT_LIMIT, &server,
                              &error));
    struct timespec ready = {.tv_nsec = 300000000};
    nanosleep(&ready, NULL);
    length = snprintf(ca_env, sizeof(ca_env), "CURL_CA_BUNDLE=%s", certificate);
    CHECK(length > 0 && (size_t)length < sizeof(ca_env));
    environment[2] = ca_env;
    char *installer[] = {"/bin/sh", argv[1],     "--base-url", endpoint, "--prefix",
                         prefix,    "--version", argv[5],      NULL};
    CHECK(run(installer, environment, 0, &error));
    CHECK(join(stage, prefix, "bin/tired"));
    char *version[] = {stage, "--version", "--json", NULL};
    CHECK(run(version, environment, 0, &error));
    char installed_helper[4096], installed_profile[4096];
    CHECK(join(installed_helper, prefix, "libexec/tired/tired-helper") &&
          join(installed_profile, prefix, "share/tired/profiles.d/backhaul.json"));
    CHECK(access(installed_helper, X_OK) == 0 && access(installed_profile, R_OK) == 0);
    char *profiles[] = {stage, "profiles", "list", "--json", NULL};
    CHECK(run(profiles, environment, 0, &error));
    puts("PASS: exact produced archive installs complete relocatable payload through HTTPS.");
    char *extract[] = {"/usr/bin/tar", "-xzf", artifact, "-C", absolute, NULL};
    CHECK(run(extract, environment, 0, &error));
    CHECK(join(obsolete_payload, destination, "usr/local/share/tired/obsolete-release-file"));
    CHECK(tired_write_private_new(obsolete_payload, "old", 3, &error));
    CHECK(run(pack, environment, 0, &error) &&
          checksums(artifact, server_root, archive_name, &error));
    CHECK(run(installer, environment, 0, &error));
    CHECK(join(obsolete_installed, prefix, "share/tired/obsolete-release-file"));
    CHECK(access(obsolete_installed, F_OK) == 0);
    CHECK(join(obsolete_payload, destination, "usr/local/share/tired/obsolete-release-file"));
    CHECK(unlink(obsolete_payload) == 0);
    CHECK(run(pack, environment, 0, &error) &&
          checksums(artifact, server_root, archive_name, &error));
    CHECK(run(installer, environment, 0, &error));
    CHECK(access(obsolete_installed, F_OK) != 0 && errno == ENOENT);
    puts("PASS: complete HTTPS download, checksum verification, relocatable prefix and reinstall.");
    /* An untrusted TLS endpoint is rejected before payload changes. */
    environment[2] = NULL;
    CHECK(run(installer, environment, 1, &error));
    environment[2] = ca_env;
    CHECK(run(version, environment, 0, &error));
    /* A checksum-valid archive containing the other ELF architecture is refused. */
    char staged_binary[4096], payload_root[4096];
    CHECK(join(payload_root, absolute, archive_root) &&
          join(staged_binary, payload_root, "usr/local/bin/tired"));
    int binary_fd = open(staged_binary, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    unsigned char machine[2], wrong[2] = {0, 0};
    CHECK(binary_fd >= 0 && pread(binary_fd, machine, 2, 18) == 2);
    wrong[0] = machine[0] == 62 ? 183 : 62;
    CHECK(pwrite(binary_fd, wrong, 2, 18) == 2 && fsync(binary_fd) == 0);
    CHECK(run(pack, environment, 0, &error) &&
          checksums(artifact, server_root, archive_name, &error));
    CHECK(run(installer, environment, 1, &error));
    CHECK(run(version, environment, 0, &error));
    CHECK(pwrite(binary_fd, machine, 2, 18) == 2 && fsync(binary_fd) == 0 && close(binary_fd) == 0);
    CHECK(run(pack, environment, 0, &error) &&
          checksums(artifact, server_root, archive_name, &error));
    puts("PASS: TLS verification and wrong-architecture refusal preserve working payload.");
    /* Corrupt download cannot touch the installed payload. */
    CHECK(join(destination, server_root, "saved.tar.gz") && rename(artifact, destination) == 0);
    CHECK(tired_write_private_new(artifact, "corrupt", 7, &error));
    CHECK(run(installer, environment, 1, &error));
    CHECK(run(version, environment, 0, &error));
    CHECK(unlink(artifact) == 0 && rename(destination, artifact) == 0);
    /* A missing artifact is a download error, not a partial installation. */
    CHECK(rename(artifact, destination) == 0);
    CHECK(run(installer, environment, 1, &error));
    CHECK(rename(destination, artifact) == 0);
    CHECK(run(version, environment, 0, &error));
    char *uninstall[] = {"/bin/sh", argv[1], "--prefix", prefix, "--uninstall", NULL};
    CHECK(run(uninstall, environment, 0, &error));
    CHECK(access(stage, F_OK) != 0 && errno == ENOENT);
    puts("PASS: corrupt/missing downloads preserve the prior installation; uninstall removes "
         "payload only.");
    /* Unrelated files and unsafe prefixes cannot be overwritten. */
    CHECK(tired_write_private_new(stage, "unrelated", 9, &error));
    CHECK(run(installer, environment, 1, &error));
    CHECK(unlink(stage) == 0);
    CHECK(join(destination, absolute, "link") && symlink(prefix, destination) == 0);
    char *bad_prefix[] = {"/bin/sh",    argv[1],  "--prefix", destination,
                          "--base-url", endpoint, NULL};
    CHECK(run(bad_prefix, environment, 1, &error));
    char *unwritable[] = {"/bin/sh",    argv[1],  "--prefix", "/proc/tired-installer-test",
                          "--base-url", endpoint, NULL};
    CHECK(run(unwritable, environment, 1, &error));
    puts("PASS: unrelated-file, symlink-prefix and unwritable-destination refusal.");
    result = 0;
cleanup:
    if (server != NULL)
    {
        tired_process_cancel(server);
        while (!tired_process_step(server))
        {
            struct timespec pause = {.tv_nsec = 20000000};
            nanosleep(&pause, NULL);
        }
        tired_process_destroy(server);
    }
    if (socket_fd >= 0)
        close(socket_fd);
    if (original != NULL)
        (void)chdir(original);
    if (absolute != NULL && !remove_tree(absolute))
    {
        fprintf(stderr, "Retained installer fixture: %s\n", absolute);
        result = 1;
    }
    free(absolute);
    free(original);
    return result;
}
