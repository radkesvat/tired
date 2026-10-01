#include "tired/sha256.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build-time byte embedding. Input order, names and bytes determine the output;
 * absolute input paths and timestamps are never written into generated code. */
int main(int argc, char **argv)
{
    if (argc < 4)
        return 2;
    const char *symbol = argv[2];
    for (const char *p = symbol; *p != '\0'; ++p)
        if (!((*p >= 'a' && *p <= 'z') || *p == '_'))
            return 2;
    FILE *out = fopen(argv[1], "w");
    if (out == NULL)
        return 1;
    int result = 1;
    size_t count = (size_t)argc - 3;
    char(*digests)[TIRED_SHA256_HEX_SIZE] = calloc(count, sizeof(*digests));
    if (digests == NULL)
        goto done;
    fputs("#include \"tired/embedded.h\"\n", out);
    for (size_t i = 0; i < count; ++i)
    {
        const char *name = strrchr(argv[i + 3], '/');
        name = name == NULL ? argv[i + 3] : name + 1;
        for (const char *p = name; *p != '\0'; ++p)
            if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-' || *p == '.'))
                goto done;
        FILE *input = fopen(argv[i + 3], "rb");
        if (input == NULL)
            goto done;
        fprintf(out, "static const unsigned char data_%zu[] = {\n", i);
        TiredSha256 hash;
        tired_sha256_init(&hash);
        unsigned char bytes[4096];
        size_t length, total = 0;
        while ((length = fread(bytes, 1, sizeof(bytes), input)) != 0)
        {
            tired_sha256_update(&hash, bytes, length);
            for (size_t j = 0; j < length; ++j)
                fprintf(out, "0x%02x,%s", bytes[j], ++total % 16 == 0 ? "\n" : "");
        }
        bool ok = !ferror(input) && total != 0;
        if (fclose(input) != 0 || !ok)
            goto done;
        tired_sha256_final(&hash, digests[i]);
        fputs("\n};\n", out);
    }
    fprintf(out, "const TiredEmbeddedFile %s[] = {\n", symbol);
    for (size_t i = 0; i < count; ++i)
    {
        const char *name = strrchr(argv[i + 3], '/');
        name = name == NULL ? argv[i + 3] : name + 1;
        fprintf(out, "{\"%s\", data_%zu, sizeof(data_%zu), \"%s\"},\n", name, i, i, digests[i]);
    }
    fprintf(out, "};\nconst size_t %s_count = %zu;\n", symbol, count);
    if (!ferror(out))
        result = 0;
done:
    free(digests);
    if (fclose(out) != 0)
        result = 1;
    if (result != 0)
        (void)remove(argv[1]);
    return result;
}
