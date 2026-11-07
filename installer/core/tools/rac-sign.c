/*
 * rac-sign: Kalebo III's Gadgetron sign (sign.h) from the command line.
 *
 *   rac-sign RAC1 -o OUT
 *
 * Writes the class core made from the PAL disc RAC1, as tools/gen_sign.py writes it.
 */
#include "sign.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void)
{
    fputs("usage: rac-sign RAC1 -o OUT\n", stderr);
    return 2;
}

int main(int argc, char **argv)
{
    InstallError error = {NULL, {0}};
    uint8_t *sign;
    size_t size = 0;
    FILE *out;
    int ok;

    if (argc != 4 || strcmp(argv[2], "-o") != 0)
        return usage();
    sign = make_sign(argv[1], &size, &error);
    if (!sign) {
        fprintf(stderr, "rac-sign: %s (%s)\n", error.message, error.code);
        return 1;
    }
    out = fopen(argv[3], "wb");
    ok = out && fwrite(sign, 1, size, out) == size;
    if (out && fclose(out) != 0)
        ok = 0;
    free(sign);
    if (!ok) {
        fprintf(stderr, "rac-sign: cannot write %s\n", argv[3]);
        return 1;
    }
    printf("wrote %s: class 1891, %zu bytes\n", argv[3], size);
    return 0;
}
