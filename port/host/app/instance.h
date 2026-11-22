/* Several copies of the port on one PC. Each copy takes the first free player slot and keeps it while
 * it runs: slot 1 uses `saves/` as always, slot N `saves/player<N>/` for its save, settings and logs,
 * and plays with the Nth gamepad. The Windows version is instance_win.c (one named lock per slot). */
#ifndef KHDAYS_HOST_INSTANCE_H
#define KHDAYS_HOST_INSTANCE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* This copy's slot, from 1 up. It is taken on the first call, made when the
 * game starts. KHDAYS_PLAYER_SLOT can name it instead (the walks use 1). */
int khdays_host_player_slot(void);

/* The slot taken, or 0 while none is (the tools and tests never take one). */
int khdays_host_player_slot_taken(void);

/* The folder for this slot's files, without a trailing slash: "saves" or
 * "saves/player<N>" (created on the first call). "saves" while no slot is
 * taken. */
const char *khdays_host_user_folder(void);

/* KHDAYS_USER_FOLDER (the phone's khdays-env.txt, a test): this slot's folder is that one instead, a path
 * under saves/ ("saves/movie-test"), made with its parents, so that a test run never reads or writes the
 * player's own save, settings and logs. Into `folder` when it is such a path; else nothing changes. Called by
 * khdays_host_player_slot (instance_posix.c, instance_ios.c); defined at the end (POSIX only). */

/* Whether another copy of the port is running on this machine right now. */
int khdays_host_other_copies(void);

/* The player's name as the DS's firmware holds it (at most `room` UTF-16 characters); its length.
 * KHDAYS_NAME, else the settings' `nickname`, else "Player" and four digits from this machine; from
 * player 2 on the slot's number added; printable ASCII only. Never the system's user name: it can be a
 * real name, and online anyone sees it. */
int khdays_host_player_name(const char *nickname, unsigned short *name, int room);

/* The automatic nickname: "Player" and four digits made from this machine. */
void khdays_host_automatic_name(char *out, size_t size);

#ifdef __cplusplus
}
#endif

#if !defined(_WIN32)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static inline void khdays_host_user_folder_from_env(char *folder, size_t size)
{
    const char *chosen = getenv("KHDAYS_USER_FOLDER");
    char made[64];
    if (chosen == NULL || strncmp(chosen, "saves/", 6) != 0 || chosen[6] == '\0' || strstr(chosen, "..") != NULL ||
        strlen(chosen) >= size || strlen(chosen) >= sizeof(made)) {
        if (chosen != NULL && *chosen != '\0') {
            fprintf(stderr, "host: KHDAYS_USER_FOLDER=%s is not a folder under saves/: saves/ is used\n", chosen);
        }
        return;
    }
    for (size_t i = 0; chosen[i] != '\0'; ++i) {
        made[i] = chosen[i];
        made[i + 1] = '\0';
        if (chosen[i + 1] == '/' || chosen[i + 1] == '\0') {
            mkdir(made, 0700); /* (POSIX mkdir: from the working folder, the phone's Documents) */
        }
    }
    strcpy(folder, chosen);
    fprintf(stderr, "host: KHDAYS_USER_FOLDER: the save, the settings and the logs are in %s\n", folder);
}
#endif

#endif
