#include <stddef.h>
#include "sandboxrt.h"

// moxiebox
#define PAGE_SIZE 0x1000
#define MAP_SIZE 0x100000
#define MMAP_FAILED ((void *) -22)
static int prot = MOXIE_PROT_READ | MOXIE_PROT_WRITE | MOXIE_PROT_EXEC;
static int flags = MOXIE_MAP_PRIVATE | MOXIE_MAP_ANONYMOUS;

// Define struct
typedef unsigned int cn_uint;
typedef unsigned char cn_byte;

typedef struct cn_string {
    cn_byte *data;
    cn_uint len;
} * CN_STRING;

typedef struct {
    void *ptr;
    size_t used;
    size_t size;
} MEMPool;

// CN_STRING
CN_STRING cn_string_init();
CN_STRING cn_string_from_cstr(const char *);
char *cn_string_str(CN_STRING);
cn_uint cn_string_len(CN_STRING);

// Memory Pool and Utils
MEMPool *pool;
void *my_malloc(size_t size);
void *my_calloc(size_t num, size_t size);

int main()
{
    void *res = mmap(NULL, PAGE_SIZE, prot, flags, 0, 0);

    // initial memory pool
    pool = mmap(NULL, PAGE_SIZE, prot, flags, 0, 0);
    pool->ptr = mmap(NULL, MAP_SIZE, prot, flags, 0, 0);
    pool->used = 0;
    pool->size = MAP_SIZE;

    if (res == MMAP_FAILED || pool == MMAP_FAILED || pool->ptr == MMAP_FAILED) {
        _exit(1);
    }

    // Starting with a blank string
    CN_STRING str = cn_string_init();
    // Starting with initial string
    CN_STRING str_i = cn_string_from_cstr("This is also a test.");

    // Output to check result
    int i;
    char *p = res;
    for (i = 0; i < 8; i++) {
        p[i] = str_i->data[i];
    }
    setreturn(res, sizeof(char) * 8);

    _exit(0);

    return 0;
}

void *my_malloc(size_t size)
{
    if (pool->used + size <= pool->size) {
        void *p = pool->ptr + pool->used;
        pool->used += size;
        return p;
    }

    return NULL;
}

void *my_calloc(size_t num, size_t size)
{
    if (pool->used + (num * size) <= pool->size) {
        void *p = pool->ptr + pool->used;
        memset(p, 0, num * size);
        pool->used += num * size;
        return p;
    }

    return NULL;
}

CN_STRING cn_string_init()
{
    return cn_string_from_cstr((const char *) NULL);
}

CN_STRING cn_string_from_cstr(const char *data)
{
    CN_STRING str = (CN_STRING) my_malloc(sizeof(struct cn_string));
    // Set up Parametres
    if (data == NULL) {
        str->data = (cn_byte *) my_calloc(1, 1);
        str->len = 0;
    } else {
        str->len = strlen(data);
        str->data = (cn_byte *) my_calloc(str->len + 1, 1);
        memcpy(str->data, data, str->len);
    }

    return str;
}

char *cn_string_str(CN_STRING str)
{
    return str->data;
}

cn_uint cn_string_len(CN_STRING str)
{
    return str->len;
}
