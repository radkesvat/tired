#define _GNU_SOURCE
#include "tired/backend.h"
#include "tired/encode.h"
#include "tired/io.h"
#include "tired/process.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static char *environment[] = {"PATH=/usr/bin:/bin", "LC_ALL=C", "HOME=/root", NULL};
static bool run(char *const arguments[], unsigned timeout, TiredText *output)
{
    TiredProcess *process = NULL;
    TiredError error = {0};
    if (!tired_process_start(arguments[0], arguments, environment, timeout, TIRED_INPUT_LIMIT,
                             &process, &error))
    {
        fprintf(stderr, "Cannot launch %s: %s\n", arguments[0], error.code);
        return false;
    }
    while (!tired_process_step(process))
    {
        struct timespec pause = {.tv_nsec = 20000000};
        nanosleep(&pause, NULL);
    }
    TiredProcessResult result = tired_process_result(process);
    bool ok = result.outcome == TIRED_PROCESS_EXITED && result.exit_code == 0;
    if (output != NULL)
        ok = tired_text_set(output, result.standard_output, result.output_length, TIRED_INPUT_LIMIT,
                            &error) &&
             ok;
    if (!ok && result.error_length != 0)
        fprintf(stderr, "%.*s\n", (int)result.error_length, result.standard_error);
    tired_process_destroy(process);
    return ok;
}
static bool write_file(const char *path, const char *bytes)
{
    TiredError error = {0};
    return tired_write_private_new(path, bytes, strlen(bytes), &error);
}
static bool path(char output[4096], const char *directory, const char *leaf)
{
    int n = snprintf(output, 4096, "%s/%s", directory, leaf);
    return n > 0 && n < 4096;
}
static bool ssh(const char *key, const char *port, const char *command, unsigned timeout,
                bool quiet)
{
    char *arguments[] = {"/usr/bin/ssh",
                         "-i",
                         (char *)key,
                         "-p",
                         (char *)port,
                         "-o",
                         "BatchMode=yes",
                         "-o",
                         "ConnectTimeout=30",
                         "-o",
                         "StrictHostKeyChecking=no",
                         "-o",
                         "UserKnownHostsFile=/dev/null",
                         "-o",
                         "LogLevel=ERROR",
                         "root@127.0.0.1",
                         (char *)command,
                         NULL};
    TiredText output = {0};
    bool ok = run(arguments, timeout, &output);
    if (!quiet && output.data != NULL)
        fwrite(output.data, 1, output.length, stdout);
    tired_text_destroy(&output);
    return ok;
}
static bool ready(const char *key, const char *port)
{
    uint64_t began = tired_monotonic_usec();
    if (began == 0)
        return false;
    while (tired_monotonic_usec() - began < UINT64_C(600000000))
    {
        if (ssh(key, port, "/usr/bin/true", 30000, true))
            return true;
        struct timespec pause = {.tv_sec = 1};
        nanosleep(&pause, NULL);
    }
    return false;
}
int main(int argc, char **argv)
{
    if ((argc != 5 && !(argc == 6 && strcmp(argv[5], "--resume") == 0)) ||
        (strcmp(argv[1], "x64") != 0 && strcmp(argv[1], "arm64") != 0))
    {
        fputs("Usage: tired_vm_runner x64|arm64 IMAGE WORK_DIRECTORY SSH_PORT\n"
              "Creates a disposable overlay and seed. Keep WORK_DIRECTORY private.\n",
              stderr);
        return 2;
    }
    unsigned long port;
    const char *memory = getenv("TIRED_VM_MEMORY_MB");
    if (memory == NULL)
        memory = "2048";
    uint64_t memory_mb;
    TiredError memory_error = {0};
    if (!tired_parse_u64(memory, strlen(memory), 512, 4096, &memory_mb, &memory_error))
        return 2;
    char *end;
    errno = 0;
    port = strtoul(argv[4], &end, 10);
    if (errno || *end || port < 1024 || port > 65535)
        return 2;
    char *work = realpath(argv[3], NULL), *image = realpath(argv[2], NULL);
    struct stat info;
    if (work == NULL || image == NULL || lstat(work, &info) != 0 || !S_ISDIR(info.st_mode) ||
        info.st_uid != getuid() || (info.st_mode & 0077) != 0)
    {
        fputs("Use an existing owner-only work directory and verified image.\n", stderr);
        free(work);
        free(image);
        return 2;
    }
    int result = 1;
    pid_t emulator = -1;
    char key[4096], public_key[4096], overlay[4096], seed[4096], user_data[4096], metadata[4096],
        console[4096], pid_file[4096];
    if (!path(key, work, "id_ed25519") || !path(public_key, work, "id_ed25519.pub") ||
        !path(overlay, work, "overlay.qcow2") || !path(seed, work, "seed.iso") ||
        !path(user_data, work, "user-data") || !path(metadata, work, "meta-data") ||
        !path(console, work, "console.log") || !path(pid_file, work, "qemu.pid"))
        goto done;
    int n;
    if (argc == 6)
    {
        if (lstat(overlay, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid())
            goto done;
        if (unlink(pid_file) != 0 && errno != ENOENT)
            goto done;
        goto boot;
    }
    char *keygen[] = {"/usr/bin/ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", key, NULL};
    if (!run(keygen, 10000, NULL))
        goto done;
    TiredText public = {0};
    TiredError error = {0};
    if (!tired_read_file(public_key, 4096, &public, &error))
        goto done;
    while (public.length > 0 && public.data[public.length - 1] == '\n')
    public.data[--public.length] = '\0';
    char config[8192];
    n = snprintf(config, sizeof(config),
                 "#cloud-config\ndisable_root: false\nssh_pwauth: false\nusers:\n  - default\n  - "
                 "name: root\n    ssh_authorized_keys:\n      - %s\nwrite_files:\n  - path: "
                 "/opt/tired-tests/disposable\n    permissions: '0600'\n    owner: root:root\n    "
                 "content: tired-disposable-vm\n",
                 public.data);
    tired_text_destroy(&public);
    if (n <= 0 || (size_t)n >= sizeof(config) || !write_file(user_data, config) ||
        !write_file(metadata,
                    "instance-id: tired-qualification\nlocal-hostname: tired-qualification\n"))
        goto done;
    char *disk[] = {"/usr/bin/qemu-img",
                    "create",
                    "-q",
                    "-f",
                    "qcow2",
                    "-F",
                    "qcow2",
                    "-b",
                    image,
                    overlay,
                    "16G",
                    NULL};
    char *iso[] = {"/usr/bin/genisoimage",
                   "-quiet",
                   "-output",
                   seed,
                   "-volid",
                   "cidata",
                   "-joliet",
                   "-rock",
                   user_data,
                   metadata,
                   NULL};
    if (!run(disk, 10000, NULL) || !run(iso, 10000, NULL))
        goto done;
boot:;
    char drive[4200], seed_drive[4200], serial[4200], network[256];
    if (snprintf(drive, sizeof(drive), "file=%s,if=virtio,format=qcow2", overlay) >=
            (int)sizeof(drive) ||
        snprintf(seed_drive, sizeof(seed_drive), "file=%s,if=virtio,format=raw,readonly=on",
                 seed) >= (int)sizeof(seed_drive) ||
        snprintf(serial, sizeof(serial), "file:%s", console) >= (int)sizeof(serial) ||
        snprintf(network, sizeof(network), "user,id=network,hostfwd=tcp:127.0.0.1:%lu-:22", port) >=
            (int)sizeof(network))
        goto done;
    bool arm = strcmp(argv[1], "arm64") == 0;
    char *x64[] = {"/usr/bin/qemu-system-x86_64",
                   "-accel",
                   "kvm",
                   "-cpu",
                   "host",
                   "-m",
                   (char *)memory,
                   "-smp",
                   "2",
                   "-drive",
                   drive,
                   "-drive",
                   seed_drive,
                   "-netdev",
                   network,
                   "-device",
                   "virtio-net-pci,netdev=network",
                   "-display",
                   "none",
                   "-serial",
                   serial,
                   "-monitor",
                   "none",
                   NULL};
    char *arm64[] = {"/usr/bin/qemu-system-aarch64",
                     "-accel",
                     "tcg,tb-size=128",
                     "-machine",
                     "virt",
                     "-cpu",
                     "cortex-a72",
                     "-m",
                     (char *)memory,
                     "-smp",
                     "2",
                     "-bios",
                     "/usr/share/AAVMF/AAVMF_CODE.fd",
                     "-drive",
                     drive,
                     "-drive",
                     seed_drive,
                     "-netdev",
                     network,
                     "-device",
                     "virtio-net-pci,netdev=network",
                     "-display",
                     "none",
                     "-serial",
                     serial,
                     "-monitor",
                     "none",
                     NULL};
    emulator = fork();
    if (emulator == 0)
    {
        int null = open("/dev/null", O_RDWR);
        if (null >= 0)
        {
            dup2(null, STDIN_FILENO);
            dup2(null, STDOUT_FILENO);
            dup2(null, STDERR_FILENO);
        }
        execve(arm ? arm64[0] : x64[0], arm ? arm64 : x64, environment);
        _exit(127);
    }
    if (emulator < 0)
        goto done;
    char pid[32];
    n = snprintf(pid, sizeof(pid), "%lu\n", (unsigned long)emulator);
    if (n <= 0 || !write_file(pid_file, pid))
        goto done;
    if (!ready(key, argv[4]) ||
        !ssh(key, argv[4], "/usr/bin/cloud-init status --wait", 300000, false))
        goto done;
    printf("Disposable VM ready: architecture=%s port=%s pid=%lu\n", argv[1], argv[4],
           (unsigned long)emulator);
    fflush(stdout);
    /* Deliberately remain its parent while local build/qualification commands
     * run. The operator sends one newline to shut down this exact VM. */
    fputs("Press Enter after qualification to stop this VM.\n", stdout);
    fflush(stdout);
    (void)getchar();
    result = 0;
done:
    if (emulator > 0)
    {
        kill(emulator, SIGTERM);
        int state;
        while (waitpid(emulator, &state, 0) < 0 && errno == EINTR)
        {
        }
    }
    if (result != 0)
        fprintf(stderr, "VM preparation failed; inspect %s/console.log\n", work);
    free(work);
    free(image);
    return result;
}
