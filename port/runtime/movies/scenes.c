/* The scene map's files and the recognition of the running scene (scenes.h).
 *
 * The event archives (the ROM's files ev/EV_<world>.p2) hold the scenes' scripts, one entry each; a
 * script begins with a word, its data's offset, then its steps. The event's machine runs a scene's
 * steps from where it read them, unchanged, so the running script is its ROM entry byte for byte
 * (RUNTIME_MAP 21): it is found among the entries of the same first word. */
#include "scenes.h"
#include "../core/rom.h"
#include "../core/session_log.h"
#include "../core/window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char khdays_scenes_port[];
extern const unsigned int khdays_scenes_port_size;

/* --- The map -------------------------------------------------------------------------------- */

static KhdaysSceneMap map;
static int map_read;

static void report(const char *file, int line, const char *why)
{
    fprintf(stderr, "scenes: %s, line %d skipped: %s\n", file, line, why);
    khdays_log("scenes: %s, line %d skipped: %s", file, line, why);
}

static char *file_text(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    char *text = NULL;
    long length;
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (length = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        text = (char *)malloc((size_t)length + 1);
        if (text != NULL && fread(text, 1, (size_t)length, f) != (size_t)length) {
            free(text);
            text = NULL;
        }
        *size = (size_t)length;
    }
    fclose(f);
    return text;
}

const KhdaysSceneMap *khdays_scenes_map(void)
{
    if (!map_read) {
        char path[1024];
        map_read = 1;
        khdays_scene_map_read(&map, (const char *)khdays_scenes_port, khdays_scenes_port_size,
                              "port/movies/hd_scenes.txt", report);
        if (khdays_mods_find("cutscenes/scenes.txt", path, (int)sizeof(path))) {
            size_t size = 0;
            char *text = file_text(path, &size);
            if (text != NULL) {
                khdays_scene_map_read(&map, text, size, path, report);
                free(text);
            } else {
                report(path, 0, "cannot be read");
            }
        }
        khdays_log("scenes: %d scenes in the map", map.count);
    }
    return &map;
}

/* --- The player's videos, said at the start ------------------------------------------------------- */

/* Whether a video is there as the course looks for it (movie_hd.c): `movie`, a mod's own video of the
 * whole movie (movies/<stem>.mp4, a mod's only); else a part's or a scene's, a mod's
 * cutscenes/cinematics/<stem>.mp4 or the player's in the working folder. */
static int video_there(const char *stem, int movie)
{
    char relative[128], path[1024];
    FILE *f;
    snprintf(relative, sizeof(relative), movie ? "movies/%s.mp4" : "cutscenes/cinematics/%s.mp4", stem);
    if (khdays_mods_find(relative, path, (int)sizeof(path))) {
        return 1; /* a mod's */
    }
    if (movie) {
        return 0; /* (the player's own are looked for part by part, as the map says) */
    }
    snprintf(path, sizeof(path), "data/cutscenes/cinematics/%s.mp4", stem);
    f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
    }
    return f != NULL;
}

void khdays_scenes_videos_count(KhdaysVideosCount *count, char *missing, size_t missing_size)
{
    const KhdaysSceneMap *m = khdays_scenes_map();
    const int files = khdays_rom_file_count();
    char seen[128][16];
    int movies = 0, movies_there = 0, movies_off = 0, scenes = 0, scenes_there = 0, voices = 0, voices_there = 0;
    size_t used = 0;
    if (missing != NULL && missing_size > 0) {
        missing[0] = '\0';
    }
    for (int id = 0; id < files; ++id) {
        char path[256], movie[16], stem[64];
        u32 offset, bytes;
        const char *name;
        double from, until;
        int parts, there = 1, known = 0;
        size_t n = 0;
        if (!khdays_rom_file_info(id, path, (int)sizeof(path), &offset, &bytes)) {
            continue;
        }
        name = path[0] == '/' ? path + 1 : path;
        if (strncmp(name, "mv/", 3) != 0 || strlen(name) < 8 || strcmp(name + strlen(name) - 5, ".mods") != 0) {
            continue; /* not a movie (mv/dummy.dat) */
        }
        /* its id: the file's name before its first dot or underscore (mv/839_es.mods: 839) */
        for (name += 3; name[n] != '\0' && name[n] != '.' && name[n] != '_' && n + 1 < sizeof(movie); ++n) {
            movie[n] = name[n];
        }
        movie[n] = '\0';
        for (int i = 0; i < movies && !known; ++i) {
            known = strcmp(seen[i], movie) == 0;
        }
        if (n == 0 || known || movies >= 128) {
            continue;
        }
        snprintf(seen[movies++], sizeof(seen[0]), "%s", movie);
        parts = khdays_scene_map_movie(m, movie, 0, stem, sizeof(stem), &from, &until);
        if (parts == 0) {
            ++movies_off;
            continue;
        }
        snprintf(stem, sizeof(stem), "hd%s", movie);
        if (!video_there(stem, 1)) { /* a mod's own video of the movie first, else the map's */
            for (int i = 0; i < parts && there; ++i) {
                khdays_scene_map_movie(m, movie, i, stem, sizeof(stem), &from, &until);
                there = video_there(stem, 0);
            }
        }
        if (there) {
            ++movies_there;
        } else if (missing != NULL && used + strlen(movie) + 2 < missing_size) {
            used += (size_t)snprintf(missing + used, missing_size - used, " %s", movie);
        }
    }
    for (int i = 0; i < m->count; ++i) {
        const KhdaysSceneEntry *e = &m->entries[i];
        int there = e->stem_count > 0;
        if (e->off || e->skip || strncmp(e->key, "mv#", 3) == 0) {
            continue;
        }
        for (int s = 0; s < e->stem_count && there; ++s) {
            there = video_there(e->stems[s], 0);
        }
        if (strncmp(e->key, "mnl#", 4) == 0) {
            ++voices;
            voices_there += there;
        } else {
            ++scenes;
            scenes_there += there;
        }
    }
    count->movies = movies;
    count->movies_there = movies_there;
    count->movies_off = movies_off;
    count->scenes = scenes;
    count->scenes_there = scenes_there;
    count->voices = voices;
    count->voices_there = voices_there;
}

void khdays_scenes_videos_report(void)
{
    KhdaysVideosCount c;
    char missing[512], line[1024];
    khdays_scenes_videos_count(&c, missing, sizeof(missing));
    snprintf(line, sizeof(line),
             "HD videos looked for in data/cutscenes/cinematics of the working folder: the game's movies with "
             "theirs %d of %d (%d the game's own by the map)%s%s; the map's scenes with theirs %d of %d; its "
             "monologues' voices %d of %d. They show with [video] movies = hd (the default); a movie or scene "
             "without its video plays as the game's",
             c.movies_there, c.movies - c.movies_off, c.movies_off, missing[0] ? "; none for" : "", missing,
             c.scenes_there, c.scenes, c.voices_there, c.voices);
    fprintf(stderr, "movies: %s\n", line);
    khdays_log("movies: %s", line);
}

/* --- The event archives' entries ------------------------------------------------------------- */

typedef struct Script {
    char key[48];
    uint32_t first; /* its first word */
    uint8_t *bytes;
    uint32_t size;
} Script;

static Script *scripts;
static int script_count, script_capacity;
static int indexed;

static void add_script(const char *key, uint8_t *bytes, uint32_t size)
{
    if (script_count == script_capacity) {
        const int capacity = script_capacity ? script_capacity * 2 : 1024;
        Script *grown = (Script *)realloc(scripts, (size_t)capacity * sizeof(*grown));
        if (grown == NULL) {
            free(bytes);
            return;
        }
        scripts = grown;
        script_capacity = capacity;
    }
    snprintf(scripts[script_count].key, sizeof(scripts[script_count].key), "%s", key);
    memcpy(&scripts[script_count].first, bytes, 4);
    scripts[script_count].bytes = bytes;
    scripts[script_count].size = size;
    ++script_count;
}

/* Every entry of every ev/EV_<world>.p2 the ROM's file list names. */
static void index_archives(void)
{
    KhdaysRomReader *reader = khdays_rom_reader_new();
    const int files = khdays_rom_file_count();
    indexed = 1;
    for (int id = 0; id < files; ++id) {
        char path[256], world[32];
        u32 offset, bytes;
        const char *name;
        size_t length;
        if (!khdays_rom_file_info(id, path, (int)sizeof(path), &offset, &bytes)) {
            continue;
        }
        name = path[0] == '/' ? path + 1 : path;
        length = strlen(name);
        if (strncmp(name, "ev/EV_", 6) != 0 || length < 10 || strcmp(name + length - 3, ".p2") != 0 ||
            length - 9 >= sizeof(world)) {
            continue;
        }
        memcpy(world, name + 6, length - 9);
        world[length - 9] = '\0';
        for (int entry = 0;; ++entry) {
            u8 *data = NULL;
            u32 size = 0;
            char key[48];
            if (!khdays_rom_file_data(reader, name, entry, &data, &size)) {
                break;
            }
            if (size < 8) {
                free(data);
                continue;
            }
            snprintf(key, sizeof(key), "EV_%s#%d", world, entry);
            add_script(key, data, size);
        }
    }
    khdays_log("scenes: %d scripts of the event archives indexed", script_count);
}

int khdays_scenes_recognise(uint32_t commands, char *key, int size)
{
    const uint32_t start = commands - 4u;
    uint32_t first;
    if (!indexed) {
        index_archives();
    }
    if (!khdays_game_ram(start)) {
        return 0;
    }
    memcpy(&first, KHDAYS_W(start), 4);
    for (int i = 0; i < script_count; ++i) {
        const Script *s = &scripts[i];
        if (s->first == first && khdays_game_ram(start + s->size - 1u) &&
            memcmp(KHDAYS_W(start), s->bytes, s->size) == 0) {
            snprintf(key, (size_t)size, "%s", s->key);
            return 1;
        }
    }
    return 0;
}
