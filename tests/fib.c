#include <stddef.h>
#include "sandboxrt.h"

#define MAP_SIZE 0x1000

#define MMAP_FAILED ((void *) -22)


static struct moxie_memory_map_ent *data;

static void find_input(void)
{
    data = moxie_memmap;
    while (data->addr) {
        if (strstr(data->tags, "data0,"))
            return;

        data++;
    }

    _exit(1);
}

#if defined(ITERATIVE)
#define fib(input, result) fib_iterative(input, result)
static void fib_iterative(int n, int *result)
{
    int f0 = 0;
    int f1 = 1;
    int sum;

    if (n == 1) {
        sum = f1;
    } else {
        sum = f0;
    }

    for (int i = 1; i < n; i++) {
        sum = f0 + f1;
        f0 = f1;
        f1 = sum;
    }

    result[0] = sum;
}

#elif defined(RECURSIVE)
#define fib(input, result) fib_recursive(input, result)
static void fib_recursive(int n, int *result)
{
    if (n == 0 || n == 1) {
        result[0] += n;
        return;
    }

    fib_recursive(n - 1, result);
    fib_recursive(n - 2, result);
}

#elif defined(TAIL_RECURSION)
#define fib(input, result) fib_tail_recursion(input, result, 0, 1)
static void fib_tail_recursion(int n, int *result, int a, int b)
{
    if (n == 0) {
        result[0] = a;
        return;
    }

    fib_tail_recursion(n - 1, result, b, a + b);
}

#elif defined(FAST_DOUBLING)
#define fib(input, result) fib_fast_doubling(input, result)
static void fib_fast_doubling(int n, int *result)
{
    if (n == 0 || n == 1) {
        result[0] = n;
        return;
    }

    int cbs = 30 - __builtin_clz(n);
    int f1 = 1;
    int f2 = 1;
    int sum, sum_1;

    while (cbs >= 0) {
        sum = (f2 * 2 - f1) * f1;
        sum_1 = f1 * f1 + f2 * f2;
        f2 = sum_1;
        f1 = sum;

        if ((n >> cbs) & 1) {
            f1 = f2;
            f2 = sum_1 + sum;
        }
        cbs--;
    }

    result[0] = f1;
}
#endif

int main()
{
    find_input();

    void *res = mmap(NULL, MAP_SIZE,
                     MOXIE_PROT_READ | MOXIE_PROT_WRITE | MOXIE_PROT_EXEC,
                     MOXIE_MAP_PRIVATE | MOXIE_MAP_ANONYMOUS, 0, 0);

    if (res == MMAP_FAILED) {
        _exit(1);
    }

    fib(*(int *) data->addr, res);
    setreturn(res, sizeof(int));
    _exit(0);

    return 0;
}
