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

#include <SDL3/SDL.h>
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

/* The one wrapper: SDL's file functions on a path from the working folder. Use these, never the SDL ones, for any
 * path of the game's own (data, saves, mods, caches, logs, pictures); a path built from SDL_GetBasePath or
 * SDL_GetPrefPath (absolute) goes through unchanged. */
#define KHDAYS_FS_WHOLE(path) char whole_[KHDAYS_FS_PATH_MAX]; const char *at_ = khdays_fs_path((path), whole_, sizeof(whole_))

static inline void *khdays_fs_load_file(const char *path, size_t *size)
{
    KHDAYS_FS_WHOLE(path);
    return SDL_LoadFile(at_, size);
}
static inline bool khdays_fs_save_file(const char *path, const void *data, size_t size)
{
    KHDAYS_FS_WHOLE(path);
    return SDL_SaveFile(at_, data, size);
}
static inline SDL_IOStream *khdays_fs_io_from_file(const char *path, const char *mode)
{
    KHDAYS_FS_WHOLE(path);
    return SDL_IOFromFile(at_, mode);
}
static inline bool khdays_fs_create_directory(const char *path)
{
    KHDAYS_FS_WHOLE(path);
    return SDL_CreateDirectory(at_);
}
static inline bool khdays_fs_get_path_info(const char *path, SDL_PathInfo *info)
{
    KHDAYS_FS_WHOLE(path);
    return SDL_GetPathInfo(at_, info);
}
static inline bool khdays_fs_remove_path(const char *path)
{
    KHDAYS_FS_WHOLE(path);
    return SDL_RemovePath(at_);
}
static inline bool khdays_fs_rename_path(const char *from, const char *to)
{
    char whole_to[KHDAYS_FS_PATH_MAX];
    KHDAYS_FS_WHOLE(from);
    return SDL_RenamePath(at_, khdays_fs_path(to, whole_to, sizeof(whole_to)));
}
static inline bool khdays_fs_copy_file(const char *from, const char *to)
{
    char whole_to[KHDAYS_FS_PATH_MAX];
    KHDAYS_FS_WHOLE(from);
    return SDL_CopyFile(at_, khdays_fs_path(to, whole_to, sizeof(whole_to)));
}
static inline char **khdays_fs_glob_directory(const char *path, const char *pattern, SDL_GlobFlags flags, int *count)
{
    KHDAYS_FS_WHOLE(path);
    return SDL_GlobDirectory(at_, pattern, flags, count);
}
static inline bool khdays_fs_enumerate_directory(const char *path, SDL_EnumerateDirectoryCallback callback, void *userdata)
{
    KHDAYS_FS_WHOLE(path);
    return SDL_EnumerateDirectory(at_, callback, userdata);
}

#endif
