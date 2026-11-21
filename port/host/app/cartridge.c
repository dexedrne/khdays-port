#include "cartridge.h"
#include "../../runtime/core/sha256.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

const unsigned char *khdays_cartridge_open(const char *path, size_t *size)
{
    /* tools/supported_roms.json, normalized header and restored cartridge padding. */
    static const unsigned char digest[32] = {0xd9, 0xe1, 0x03, 0xc6, 0xcc, 0xd3, 0x9d, 0x5b, 0x91, 0xe4, 0x4d,
                                             0x86, 0x59, 0x83, 0xba, 0xdc, 0x5e, 0x2a, 0xf1, 0x67, 0x4d, 0xb4,
                                             0x04, 0xb3, 0x76, 0x85, 0x57, 0x71, 0x20, 0x38, 0xa3, 0x73};
    enum { CAPACITY = 256 * 1024 * 1024 };
    *size = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return NULL;
    }
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 4096 || st.st_size > CAPACITY) {
        fprintf(stderr, "Invalid cartridge size: %s\n", path);
        close(fd);
        return NULL;
    }
    size_t length = (size_t)st.st_size;
    const unsigned char *bytes = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (bytes == MAP_FAILED) {
        perror(path);
        return NULL;
    }
    if (memcmp(bytes + 12, "YKGP", 4) == 0) {
        KhdaysSha256 hash;
        unsigned char fill[4096] = {0}, actual[32];
        uint32_t used = (uint32_t)bytes[0x80] | (uint32_t)bytes[0x81] << 8 | (uint32_t)bytes[0x82] << 16 |
                        (uint32_t)bytes[0x83] << 24;
        khdays_sha256_begin(&hash);
        khdays_sha256_add(&hash, bytes, 512);
        khdays_sha256_add(&hash, fill, 4096 - 512);
        khdays_sha256_add(&hash, bytes + 4096, length - 4096);
        memset(fill, 255, sizeof(fill));
        for (size_t left = CAPACITY - length; left;) {
            size_t n = left < sizeof(fill) ? left : sizeof(fill);
            khdays_sha256_add(&hash, fill, n);
            left -= n;
        }
        khdays_sha256_end(&hash, actual);
        if (length >= used && memcmp(actual, digest, sizeof(actual)) == 0) {
            *size = length;
            return bytes;
        }
    }
    fprintf(stderr, "%s: unsupported cartridge (requires verified European YKGP revision 0)\n", path);
    munmap((void *)bytes, length);
    return NULL;
}
