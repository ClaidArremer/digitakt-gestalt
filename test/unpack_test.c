/* rads.c's unpacker (the Digitakt's packed tables) against the tables as they are: all 512 */
#include <stdio.h>
#include <string.h>
#include "../src/rads.c"
int main(int argc, char **argv)
{
    static unsigned char ref[512][256];
    uint8_t o[256];
    int t, bad = 0;
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(ref, 1, sizeof ref, f) != sizeof ref) { printf("no tables\n"); return 2; }
    for (t = 0; t < 512; t++) {
        rd_unpack(t, o);
        if (memcmp(o, ref[t], 256)) { bad++; printf("table %d differs\n", t); }
    }
    printf("%s: %d of 512 tables unpack exactly\n", bad ? "FAIL" : "OK", 512 - bad);
    return bad != 0;
}
