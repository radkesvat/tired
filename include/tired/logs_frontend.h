#ifndef TIRED_LOGS_FRONTEND_H
#define TIRED_LOGS_FRONTEND_H
#include "tired/cli.h"
#include <signal.h>
#include <stdio.h>
#include <systemd/sd-journal.h>
/* Single-threaded streaming frontend. Caller ignores SIGPIPE and provides a
 * cancellation flag set by non-restarting signal handlers. Writes one bounded
 * event at a time; does not retain history or save files. Output must be a fresh
 * exclusively used stream with no pending stdio output. Uses its descriptor with
 * temporary O_NONBLOCK and restores original flags, including on cancellation.
 * A write failure can leave a partial final event; do not append JSON to repair it.
 * Historical journal access does not start/load/reload units or elevate privileges. */
bool tired_logs_command(const TiredRequest *request, FILE *output, volatile sig_atomic_t *cancel,
                        TiredStatus *result, TiredError *error);
/* Integration boundary for private journal fixtures. Borrowed fresh handle;
 * applies scope matches before iteration. roots are trusted access-check paths,
 * NULL for production roots; machine ID is the selected local machine. Same UID
 * as real caller, no privilege delegation. Caller always closes the handle. */
bool tired_logs_session(const TiredRequest *request, sd_journal *journal,
                        const char *const roots[2], const char *machine_id, size_t default_lines,
                        FILE *output, volatile sig_atomic_t *cancel, TiredStatus *result,
                        TiredError *error);
#endif
