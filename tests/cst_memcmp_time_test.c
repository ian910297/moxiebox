#include <stddef.h>
#include "sandboxrt.h"

#define PAGE_SIZE 0x1000
#define MAP_SIZE 0x100000
#define MMAP_FAILED ((void *) -22)

static int prot = MOXIE_PROT_READ | MOXIE_PROT_WRITE | MOXIE_PROT_EXEC;
static int flags = MOXIE_MAP_PRIVATE | MOXIE_MAP_ANONYMOUS;

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

int main()
{
    find_input();

    void *mem = mmap(NULL, MAP_SIZE, prot, flags, 0, 0);

    if (mem == MMAP_FAILED) {
        _exit(1);
    }

    memset(mem, 0, MAP_SIZE);
    cst_memcmp(mem, data->addr, MAP_SIZE);
    _exit(0);

    return 0;
}
