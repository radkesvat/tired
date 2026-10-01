#ifndef TIRED_SHA256_H
#define TIRED_SHA256_H
#include <nettle/sha2.h>
#include <stddef.h>

#define TIRED_SHA256_HEX_SIZE 65U
typedef struct
{
    struct sha256_ctx context;
} TiredSha256;

void tired_sha256_init(TiredSha256 *state);
void tired_sha256_update(TiredSha256 *state, const void *data, size_t length);
/* Finalize to lowercase hexadecimal, including the terminator, and clear state. */
void tired_sha256_final(TiredSha256 *state, char digest[TIRED_SHA256_HEX_SIZE]);
void tired_sha256(const void *data, size_t length, char digest[TIRED_SHA256_HEX_SIZE]);
#endif
