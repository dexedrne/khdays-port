#include "files.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int khdays_host_write_file_atomic(const char *path, const void *data, size_t size)
{
    if (!path || !*path) {
        fprintf(stderr, "host: empty save path\n");
        return 0;
    }
    char *temp = malloc(strlen(path) + sizeof(".XXXXXX"));
    if (!temp) {
        perror(path);
        return 0;
    }
    strcpy(temp, path);
    for (char *slash = temp + 1; *slash; ++slash) {
        if (*slash != '/')
            continue;
        *slash = 0;
        if (mkdir(temp, 0700) && errno != EEXIST)
            goto fail;
        *slash = '/';
    }
    strcat(temp, ".XXXXXX");
    int fd = mkstemp(temp);
    if (fd < 0)
        goto fail;
    size_t done = 0;
    while (done < size) {
        size_t left = size - done;
        ssize_t n = write(fd, (const char *)data + done, left > INT_MAX ? INT_MAX : left);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        done += (size_t)n;
    }
    int ok = done == size && fsync(fd) == 0;
    if (close(fd))
        ok = 0;
    if (ok && rename(temp, path) == 0) {
        free(temp);
        return 1;
    }
    int reason = errno;
    unlink(temp);
    errno = reason;
fail:
    perror(path);
    free(temp);
    return 0;
}

long khdays_host_read_file(const char *path, void *data, size_t room)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return -1;
    long n = (long)fread(data, 1, room > LONG_MAX ? LONG_MAX : room, file);
    if (ferror(file))
        n = -1;
    fclose(file);
    return n;
}

void khdays_host_local_time(int *hour, int *minute, int *second)
{
    time_t now = time(NULL);
    struct tm t = {0};
    if (!localtime_r(&now, &t))
        t = (struct tm){0};
    *hour = t.tm_hour;
    *minute = t.tm_min;
    *second = t.tm_sec;
}
