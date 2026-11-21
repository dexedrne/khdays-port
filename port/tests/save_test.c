/* khdays-save-test [FOLDER]: the save written so a crash cannot corrupt it (khdays_host_write_file_atomic):
 * a child (--child PATH) writes two 64 KB images in turn and is stopped at a random moment, 100 times;
 * the file must be one of the two, whole. */
#include "../host/app/files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#define SIZE 65536
#define STOPS 100

static unsigned char images[2][SIZE];

static void make_images(void)
{
    for (int i = 0; i < SIZE; ++i) {
        images[0][i] = (unsigned char)(0xa5 ^ i);
        images[1][i] = (unsigned char)(i * 7 + 3);
    }
}

static int child(const char *path)
{
    for (unsigned n = 0;; ++n) {
        if (!khdays_host_write_file_atomic(path, images[n & 1], SIZE)) {
            return 2;
        }
    }
}

int main(int argc, char **argv)
{
    char path[4096], name[80];
    const char *folder = argc > 1 ? argv[1] : ".";
    static unsigned char read[SIZE + 1];
    int seen[2] = {0, 0};
    make_images();
    if (argc == 3 && strcmp(argv[1], "--child") == 0) {
        return child(argv[2]);
    }
    snprintf(name, sizeof(name), "khdays-save-%llu.sav", (unsigned long long)SDL_GetPerformanceCounter());
    snprintf(path, sizeof(path), "%s/%s", folder, name);
    if (!khdays_host_write_file_atomic(path, images[0], SIZE)) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    srand((unsigned)SDL_GetTicks());
    for (int stop = 0; stop < STOPS; ++stop) {
        const char *args[] = {argv[0], "--child", path, NULL};
        SDL_Process *writer = SDL_CreateProcess(args, false);
        long n;
        int which = -1;
        if (!writer) {
            fprintf(stderr, "cannot start writer: %s\n", SDL_GetError());
            return 1;
        }
        SDL_Delay(20 + rand() % 100);
        if (!SDL_KillProcess(writer, true) || !SDL_WaitProcess(writer, true, NULL)) {
            fprintf(stderr, "cannot stop writer: %s\n", SDL_GetError());
            SDL_DestroyProcess(writer);
            return 1;
        }
        SDL_DestroyProcess(writer);
        n = khdays_host_read_file(path, read, sizeof(read));
        for (int k = 0; k < 2; ++k) {
            if (n == SIZE && memcmp(read, images[k], SIZE) == 0) {
                which = k;
            }
        }
        if (which < 0) {
            fprintf(stderr, "stop %d: the file is neither image whole (%ld bytes)\n", stop + 1, n);
            return 1;
        }
        ++seen[which];
    }
    {
        /* the writers' temporary files left by the stops: removed */
        char pattern[96];
        snprintf(pattern, sizeof(pattern), "%s.*", name);
        char **files = SDL_GlobDirectory(folder, pattern, 0, NULL);
        for (int i = 0; files && files[i]; ++i) {
            char leftover[4096];
            snprintf(leftover, sizeof(leftover), "%s/%s", folder, files[i]);
            SDL_RemovePath(leftover);
        }
        SDL_free(files);
    }
    SDL_RemovePath(path);
    printf("%d interrupted saves: the file whole each time (the first image %d times, the second %d)\n", STOPS, seen[0],
           seen[1]);
    return 0;
}
