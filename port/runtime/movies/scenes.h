/* The theater's HD scenes in place of the game's event scenes: the scene map, and which
 * scene the field's event runs. */
#ifndef KHDAYS_SCENES_H
#define KHDAYS_SCENES_H

#include <stddef.h>
#include <stdint.h>

#include "scene_map.h"

/* The map: the port's (port/movies/hd_scenes.txt, compiled in) and a mod's cutscenes/scenes.txt over
 * it; read once, on first use. */
const KhdaysSceneMap *khdays_scenes_map(void);

/* The scene whose skippable stretch the event's machine has just begun: `commands` the reference of
 * its running script's steps (the depth's entry +0xc). Writes its key (EV_S#4) into `key` and returns
 * 1, or 0 when the script is none of the ROM's event archives' entries. */
int khdays_scenes_recognise(uint32_t commands, char *key, int size);

/* At the start, once the cartridge is open: says on stderr and in the session log where the player's
 * HD videos are looked for (data/cutscenes/cinematics in the working folder) and how many of them are
 * there -- of the game's movies (the cartridge's mv/ files, each as the map says it is shown), of the
 * map's scenes and of its monologues' voices -- naming the movies without one. */
void khdays_scenes_videos_report(void);

/* The report's numbers (one count for both; the videos' test checks them against the folder): the
 * game's movies (`movies`, of which `movies_off` are the game's own by the map and `movies_there` have
 * every video the map shows them as), the map's scenes and its monologues' voices with all of theirs.
 * `missing`, when not NULL: the ids of the movies without, each after a space. */
typedef struct KhdaysVideosCount {
    int movies, movies_there, movies_off;
    int scenes, scenes_there;
    int voices, voices_there;
} KhdaysVideosCount;
void khdays_scenes_videos_count(KhdaysVideosCount *count, char *missing, size_t missing_size);

#endif
