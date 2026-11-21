#ifndef KHDAYS_CARTRIDGE_H
#define KHDAYS_CARTRIDGE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* A verified cartridge mapped read-only. Release with munmap(bytes, size). */
const unsigned char *khdays_cartridge_open(const char *path, size_t *size);
#ifdef __cplusplus
}
#endif
#endif
