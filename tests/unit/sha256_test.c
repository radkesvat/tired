#include "tired/memory.h"
#include "tired/sha256.h"
#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%d: %s\n", __LINE__, #expression);                                    \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void)
{
    static const struct
    {
        const char *input;
        const char *expected;
    } vectors[] = {{"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
                   {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
                   {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"}};
    char digest[TIRED_SHA256_HEX_SIZE];
    for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i)
    {
        size_t length = strlen(vectors[i].input);
        tired_sha256(vectors[i].input, length, digest);
        CHECK(strcmp(digest, vectors[i].expected) == 0);
        for (size_t split = 0; split <= length; ++split)
        {
            TiredSha256 state;
            tired_sha256_init(&state);
            tired_sha256_update(&state, vectors[i].input, split);
            tired_sha256_update(&state, NULL, 0);
            tired_sha256_update(&state, vectors[i].input + split, length - split);
            tired_sha256_final(&state, digest);
            CHECK(strcmp(digest, vectors[i].expected) == 0);
            const unsigned char *cleared = (const unsigned char *)&state;
            for (size_t j = 0; j < sizeof(state); ++j)
                CHECK(cleared[j] == 0);
        }
    }
    tired_sha256(NULL, 0, digest);
    CHECK(strcmp(digest, vectors[0].expected) == 0);
    unsigned char block[1000];
    memset(block, 'a', sizeof(block));
    TiredSha256 state;
    tired_sha256_init(&state);
    for (size_t i = 0; i < 1000; ++i)
        tired_sha256_update(&state, block, sizeof(block));
    tired_sha256_final(&state, digest);
    CHECK(strcmp(digest, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
    tired_memory_clear(NULL, 0);
    tired_memory_clear(block + 1, sizeof(block) - 2);
    CHECK(block[0] == 'a' && block[sizeof(block) - 1] == 'a');
    for (size_t i = 1; i < sizeof(block) - 1; ++i)
        CHECK(block[i] == 0);
    return 0;
}
