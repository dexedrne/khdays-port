/* A path from the working folder as a whole one, for SDL's file functions.
 *
 * On iOS SDL does not take a relative path from the process's working folder (the app's Documents,
 * runtime/core/start.c) but from SDL_GetPrefPath("", ""), the app's Library/Application Support: its
 * SDL_CreateDirectory, SDL_RenamePath, SDL_RemovePath, SDL_CopyFile, SDL_EnumerateDirectory and
 * SDL_GlobDirectory look only there; SDL_GetPathInfo and SDL_IOFromFile (SDL_LoadFile, SDL_SaveFile) look
 * there first and in the working folder only when that fails, so a write lands there whenever its folder
 * exists there (SDL3 src/filesystem/posix/SDL_sysfsops.c and src/io/SDL_iostream.c, SDL_PLATFORM_IOS).
 * The phone's first run showed it: saves/logs was made in Application Support while the picture went to
 * Documents/saves/logs ("picture 1: cannot write saves/logs/picture-00001.png"), and the settings file was
 * kept in Application Support where neither the Files app nor devicectl's Documents see it.
 *
 * khdays_fs_path gives such a call the whole path (the working folder's, then `path`) on every POSIX
 * system: the same file there as the relative path names (Linux and the Mac take a relative path from the
 * working folder anyway), so the Linux tests go the phone's way. Windows: `path` as it is. An absolute
 * path, or one too long for `out`, is given back as it is. */
#ifndef KHDAYS_HOST_FS_PATH_H
#define KHDAYS_HOST_FS_PATH_H

#include <stddef.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

static inline const char *khdays_fs_path(const char *path, char *out, size_t size)
{
#ifndef _WIN32
    size_t n, more;
    if (path == NULL || path[0] == '/' || path[0] == '\0' || out == NULL || size == 0) {
        return path;
    }
    while (path[0] == '.' && path[1] == '/') {
        path += 2;
    }
    if (getcwd(out, size) == NULL) {
        return path;
    }
    n = strlen(out);
    more = strlen(path);
    if (n + 1 + more + 1 > size) {
        return path;
    }
    if (n == 0 || out[n - 1] != '/') {
        out[n++] = '/';
    }
    memcpy(out + n, path, more + 1);
    return out;
#else
    (void)out;
    (void)size;
    return path;
#endif
}

/* The size of a buffer for khdays_fs_path. */
#define KHDAYS_FS_PATH_MAX 1024

#endif
