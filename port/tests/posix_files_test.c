#include "../host/app/files.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void)
{
    char directory[] = "save-test-XXXXXX", readback[8];
    if (!mkdtemp(directory) || chdir(directory))
        return 1;
    const char *path = "saves/slot";
    if (khdays_host_read_file(path, readback, 8) != -1 || !khdays_host_write_file_atomic(path, "original", 8) ||
        !khdays_host_write_file_atomic(path, "new", 3) || khdays_host_read_file(path, readback, 8) != 3 ||
        memcmp(readback, "new", 3))
        return 1;
    return unlink(path) || rmdir("saves") || chdir("..") || rmdir(directory);
}
