#include "tired/sha256.h"
#include "tired/memory.h"
#include <assert.h>
#include <nettle/version.h>

void tired_sha256_init(TiredSha256 *state)
{
    assert(state != NULL);
    sha256_init(&state->context);
}

void tired_sha256_update(TiredSha256 *state, const void *data, size_t length)
{
    assert(state != NULL && (data != NULL || length == 0));
    if (length != 0)
        sha256_update(&state->context, length, data);
}

void tired_sha256_final(TiredSha256 *state, char digest[TIRED_SHA256_HEX_SIZE])
{
    assert(state != NULL && digest != NULL);
    unsigned char bytes[SHA256_DIGEST_SIZE];
#if NETTLE_VERSION_MAJOR >= 4
    sha256_digest(&state->context, bytes);
#else
    sha256_digest(&state->context, sizeof(bytes), bytes);
#endif
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(bytes); ++i)
    {
        digest[2 * i] = hex[bytes[i] >> 4];
        digest[2 * i + 1] = hex[bytes[i] & 15];
    }
    digest[2 * sizeof(bytes)] = '\0';
    tired_memory_clear(bytes, sizeof(bytes));
    tired_memory_clear(state, sizeof(*state));
}

void tired_sha256(const void *data, size_t length, char digest[TIRED_SHA256_HEX_SIZE])
{
    TiredSha256 state;
    tired_sha256_init(&state);
    tired_sha256_update(&state, data, length);
    tired_sha256_final(&state, digest);
}
