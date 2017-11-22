#include <stddef.h>

int cst_memcmp(const void *m1, const void *m2, size_t n)
{
    const unsigned char *pm1 = (const unsigned char *) m1;
    const unsigned char *pm2 = (const unsigned char *) m2;

    int diff, res = 0;

    if (n > 0) {
        do {
            --n;
            diff = pm1[n] - pm2[n];
            res = (res & (((diff - 1) & ~diff) >> 8)) | diff;
        } while (n != 0);
    }

    return (((res - 1) & 0xff000000) | ((unsigned int) (res - 1)) >> 8) +
           ((res & 0xff000000) | (unsigned int) (res >> 8)) + 1;
}
