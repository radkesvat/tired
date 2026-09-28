#include "tired/process_value.h"
#include <assert.h>
#include <signal.h>
#include <string.h>

#define SIGNAL(name) {#name, SIG##name}
static const struct
{
    const char *name;
    int number;
} signals[] = {SIGNAL(HUP),      SIGNAL(INT),    SIGNAL(QUIT), SIGNAL(ILL),  SIGNAL(TRAP),
               SIGNAL(ABRT),     SIGNAL(BUS),    SIGNAL(FPE),  SIGNAL(KILL), SIGNAL(USR1),
               SIGNAL(SEGV),     SIGNAL(USR2),   SIGNAL(PIPE), SIGNAL(ALRM), SIGNAL(TERM),
               SIGNAL(CHLD),     SIGNAL(CONT),   SIGNAL(STOP), SIGNAL(TSTP), SIGNAL(TTIN),
               SIGNAL(TTOU),     SIGNAL(URG),    SIGNAL(XCPU), SIGNAL(XFSZ), SIGNAL(VTALRM),
               SIGNAL(PROF),     SIGNAL(WINCH),  SIGNAL(IO),   SIGNAL(PWR),  SIGNAL(SYS),
               {"IOT", SIGABRT}, {"POLL", SIGIO}};
#undef SIGNAL

bool tired_parse_signal(const char *text, size_t length, int *signal_number, TiredError *error)
{
    assert(signal_number != NULL && (text != NULL || length == 0));
    if (length >= 3 && memcmp(text, "SIG", 3) == 0)
    {
        text += 3;
        length -= 3;
    }
    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i)
        if (strlen(signals[i].name) == length && memcmp(text, signals[i].name, length) == 0)
        {
            *signal_number = signals[i].number;
            tired_error_clear(error);
            return true;
        }
    int first = SIGRTMIN;
    int last = SIGRTMAX;
    if (length >= 5 && (memcmp(text, "RTMIN", 5) == 0 || memcmp(text, "RTMAX", 5) == 0))
    {
        bool from_min = text[2] == 'M' && text[3] == 'I';
        uint64_t offset = 0;
        if (length != 5 &&
            (length < 7 || text[5] != (from_min ? '+' : '-') ||
             !tired_parse_u64(text + 6, length - 6, 0, (uint64_t)(last - first), &offset, error)))
            goto invalid;
        *signal_number = from_min ? first + (int)offset : last - (int)offset;
        tired_error_clear(error);
        return true;
    }
    uint64_t number;
    if (!tired_parse_u64(text, length, 1, (uint64_t)last, &number, error))
        goto invalid;
    bool valid = number >= (uint64_t)first;
    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i)
        if ((uint64_t)signals[i].number == number)
            valid = true;
    if (!valid)
        goto invalid;
    *signal_number = (int)number;
    tired_error_clear(error);
    return true;
invalid:
    return tired_error_set(error, TIRED_INVALID, "signal",
                           "Unknown or unsupported signal name or number.", 0);
}

bool tired_parse_capability(const char *text, size_t length, unsigned *number, TiredError *error)
{
    assert(number != NULL && (text != NULL || length == 0));
    static const char *names[] = {"CHOWN",
                                  "DAC_OVERRIDE",
                                  "DAC_READ_SEARCH",
                                  "FOWNER",
                                  "FSETID",
                                  "KILL",
                                  "SETGID",
                                  "SETUID",
                                  "SETPCAP",
                                  "LINUX_IMMUTABLE",
                                  "NET_BIND_SERVICE",
                                  "NET_BROADCAST",
                                  "NET_ADMIN",
                                  "NET_RAW",
                                  "IPC_LOCK",
                                  "IPC_OWNER",
                                  "SYS_MODULE",
                                  "SYS_RAWIO",
                                  "SYS_CHROOT",
                                  "SYS_PTRACE",
                                  "SYS_PACCT",
                                  "SYS_ADMIN",
                                  "SYS_BOOT",
                                  "SYS_NICE",
                                  "SYS_RESOURCE",
                                  "SYS_TIME",
                                  "SYS_TTY_CONFIG",
                                  "MKNOD",
                                  "LEASE",
                                  "AUDIT_WRITE",
                                  "AUDIT_CONTROL",
                                  "SETFCAP",
                                  "MAC_OVERRIDE",
                                  "MAC_ADMIN",
                                  "SYSLOG",
                                  "WAKE_ALARM",
                                  "BLOCK_SUSPEND",
                                  "AUDIT_READ",
                                  "PERFMON",
                                  "BPF",
                                  "CHECKPOINT_RESTORE"};
    if (length < 4 || memcmp(text, "CAP_", 4) != 0)
        goto invalid;
    text += 4;
    length -= 4;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strlen(names[i]) == length && memcmp(text, names[i], length) == 0)
        {
            *number = (unsigned)i;
            tired_error_clear(error);
            return true;
        }
invalid:
    return tired_error_set(error, TIRED_INVALID, "capability",
                           "Expected a known uppercase CAP_* name.", 0);
}
