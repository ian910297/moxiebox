#include <stddef.h>
#include "sandboxrt.h"

#define PAGE_SIZE 0x1000
#define MAP_SIZE 0x1000000
#define MMAP_FAILED ((void *) -22)

static int prot = MOXIE_PROT_READ | MOXIE_PROT_WRITE | MOXIE_PROT_EXEC;
static int flags = MOXIE_MAP_PRIVATE | MOXIE_MAP_ANONYMOUS;

#define TEST_START int pass = 0;

#define GET_TEST_RESULT(total, success) \
    total = 3;                          \
    success = pass;

#define TEST_RULE(m1, m2, expected)                       \
    do {                                                  \
        pass += cst_memcmp(m1, m2, MAP_SIZE) == expected; \
    } while (0);

#define TEST_CASE_1(m1, m2)      \
    do {                         \
        memset(m1, 0, MAP_SIZE); \
        memset(m2, 0, MAP_SIZE); \
        TEST_RULE(m1, m2, 0);    \
    } while (0);

#define TEST_CASE_2(m1, m2)           \
    do {                              \
        memset(m1, 1, MAP_SIZE >> 1); \
        memset(m2, 0, MAP_SIZE);      \
        TEST_RULE(m1, m2, 1);         \
    } while (0);

#define TEST_CASE_3(m1, m2)           \
    do {                              \
        memset(m1, 0, MAP_SIZE);      \
        memset(m2, 1, MAP_SIZE >> 1); \
        TEST_RULE(m1, m2, -1);        \
    } while (0);

int main()
{
    void *a = mmap(NULL, MAP_SIZE, prot, flags, 0, 0);
    void *b = mmap(NULL, MAP_SIZE, prot, flags, 0, 0);
    void *res = mmap(NULL, PAGE_SIZE, prot, flags, 0, 0);

    if (a == MMAP_FAILED || b == MMAP_FAILED || res == MMAP_FAILED) {
        _exit(1);
    }

    int *p = res;

    TEST_START;
    TEST_CASE_1(a, b);
    TEST_CASE_2(a, b);
    TEST_CASE_3(a, b);
    GET_TEST_RESULT(p[0], p[1]);

    setreturn(res, sizeof(int) * 2);

    _exit(0);

    return 0;
}
