#include "instance.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static int slot;
static char folder[64] = "saves";

int khdays_host_player_slot(void)
{
    if (slot) return slot;
    const char *override = getenv("KHDAYS_PLAYER_SLOT");
    int chosen = override ? atoi(override) : 0;
    if (mkdir("saves", 0700) && errno != EEXIST) {
        perror("saves");
        exit(1);
    }
    if (chosen <= 0) {
        for (chosen = 1; ; ++chosen) {
            char path[64];
            snprintf(path, sizeof(path), "saves/.player-%d.lock", chosen);
            int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
            if (fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0) break; /* held until process exit */
            if (fd < 0 || errno != EWOULDBLOCK) {
                perror(path);
                exit(1);
            }
            close(fd);
        }
    }
    if (chosen > 1) {
        snprintf(folder, sizeof(folder), "saves/player%d", chosen);
        if (mkdir(folder, 0700) && errno != EEXIST) {
            perror(folder);
            exit(1);
        }
    }
    return slot = chosen;
}

const char *khdays_host_user_folder(void) { return folder; }
int khdays_host_player_slot_taken(void) { return slot; }

int khdays_host_other_copies(void)
{
    DIR *dir = opendir("saves");
    if (!dir) return 0;
    int other = 0;
    struct dirent *entry;
    while (!other && (entry = readdir(dir))) {
        int n, used = 0;
        if (sscanf(entry->d_name, ".player-%d.lock%n", &n, &used) != 1 ||
            !used || entry->d_name[used] || n == slot) continue;
        int fd = openat(dirfd(dir), entry->d_name, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            other = flock(fd, LOCK_EX | LOCK_NB) < 0 && errno == EWOULDBLOCK;
            close(fd);
        }
    }
    closedir(dir);
    return other;
}

void khdays_host_automatic_name(char *out, size_t size)
{
    char machine[256] = {0};
    gethostname(machine, sizeof(machine) - 1);
    unsigned hash = 2166136261u;
    for (const char *p = machine; *p; ++p) hash = (hash ^ (unsigned char)*p) * 16777619u;
    for (const char *p = folder; *p; ++p) hash = (hash ^ (unsigned char)*p) * 16777619u;
    snprintf(out, size, "Player%04u", hash % 10000);
}

int khdays_host_player_name(const char *nickname, unsigned short *name, int room)
{
    char automatic[32];
    const char *text = getenv("KHDAYS_NAME");
    if (!text || !*text) text = nickname;
    if (!text || !*text) {
        khdays_host_automatic_name(automatic, sizeof(automatic));
        text = automatic;
    }
    int length = 0;
    while (*text && length < room) {
        unsigned char c = (unsigned char)*text++;
        if (c >= 32 && c < 127) name[length++] = c;
    }
    if (slot > 1 && room > 0) {
        if (length == room) --length;
        name[length++] = '0' + slot % 10;
    }
    return length;
}
