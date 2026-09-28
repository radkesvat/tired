#ifndef TIRED_PROCESS_VALUE_H
#define TIRED_PROCESS_VALUE_H
#include "tired/value.h"
/* Standard signal names with optional SIG prefix, decimal numbers, and
 * RTMIN[+N]/RTMAX[-N]. Reserved libc signals and zero are rejected. */
bool tired_parse_signal(const char *text, size_t length, int *signal_number, TiredError *error);
/* Canonical uppercase CAP_* names. This validates syntax against the known
 * Linux capability table, not availability in the running kernel or identity. */
bool tired_parse_capability(const char *text, size_t length, unsigned *number, TiredError *error);
#endif
