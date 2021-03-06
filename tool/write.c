#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_FILEPATH_LENGTH 256

#define USAGE                                 \
    "Description:\n"                          \
    "\n"                                      \
    "  Generate the input data for sandbox\n" \
    "  It write bytes to output file\n"       \
    "\n"                                      \
    "Usage:\n"                                \
    "\n"                                      \
    "-i [input byte]: input data(decimal)\n"  \
    "-o [output file]: output file\n"         \
    "-r [repeat times]: repeat input data\n"  \
    "-a : append input data to output file\n" \
    "\n"

static bool is_little_endian()
{
    unsigned int test_num = 0x1;
    unsigned char *start = (unsigned char *) &test_num;
    return start[0];
}

static unsigned int reverse_byte(unsigned int num)
{
    unsigned int mask = 0xff000000;
    unsigned int reverse_num = 0;

    while (num) {
        reverse_num |= num & mask;
        num <<= 8;
        if (!num)
            break;
        reverse_num >>= 8;
    }

    return reverse_num;
}

int main(int argc, char *argv[])
{
    int opt;
    FILE *out_file;
    char filepath[MAX_FILEPATH_LENGTH];
    char *access = "wb";

    bool i_flag = false;
    bool o_flag = false;

    unsigned int input;
    unsigned int repeat = 1;
    unsigned char *buffer;

    while ((opt = getopt(argc, argv, "i:o:r:ah")) != -1) {
        switch (opt) {
        case 'i':
            input = (unsigned int) strtol(optarg, NULL, 10);
            i_flag = true;
            break;
        case 'o':
            strcpy(filepath, optarg);
            o_flag = true;
            break;
        case 'a':
            access = "ab";
            break;
        case 'r':
            repeat = (unsigned int) strtol(optarg, NULL, 10);
            break;
        case 'h':
        default:
            printf(USAGE);
            return 0;
        }
    }

    if (!(i_flag && o_flag)) {
        printf("Input or output option not found. See `-h`.\n");
        return 1;
    }

    out_file = fopen(filepath, access);
    if (out_file == NULL) {
        printf("open file error\n");
        return 1;
    }

    if (!is_little_endian())
        input = reverse_byte(input);

    buffer = malloc(sizeof(char) * repeat);
    memset(buffer, input, repeat);

    fwrite(buffer, sizeof(char), sizeof(char) * repeat, out_file);
    fclose(out_file);
    free(buffer);

    return 0;
}
