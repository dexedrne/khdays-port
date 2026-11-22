/* The player's HD videos as they are, against the cartridge and the scene map as compiled in
 * (runtime/movies/scenes.c, scene_map.c): for each of the game's movies (the cartridge's mv/ files)
 * which video shows it and which part of it, whether that file is there, and -- when ffprobe is
 * installed -- whether it can be read and the part lies inside it; then the map's scenes and
 * monologues. It also runs the report the port says at its start (khdays_scenes_videos_report) and
 * checks its numbers (khdays_scenes_videos_count: the phone's only word on which videos it found)
 * against what the folder holds.
 *
 *   hd_videos <cartridge> <folder of hd*.mp4>
 *
 * Passes when every video the folder holds is one the map shows for a game's movie, none is empty,
 * none is refused by ffprobe (damaged, or still being copied) where ffprobe is installed, none of
 * those parts begins past its file's end, and the report's numbers are the folder's. Skipped (77) without the cartridge or without videos. Nothing is
 * written to the folder; it runs in a folder of its own with a link to it. */
#include "../runtime/core/rom.h"
#include "../runtime/core/text_patches.h"
extern "C" {
#include "../runtime/movies/scenes.h"
}

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>
#include <unistd.h>

extern "C" void khdays_host_tell_no_data(const char *where, const char *why)
{
    std::fprintf(stderr, "%s: %s\n", where, why ? why : "missing game data");
}
extern "C" void khdays_origin_want(void)
{
    std::abort();
}
bool khdays_text_patch(const std::string &, const std::filesystem::path &, std::vector<uint8_t> &)
{
    std::abort();
}
extern "C" {
unsigned char *khdays_window; /* scenes.c's recognition, not used here */
void khdays_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::fputs("log: ", stderr);
    std::vfprintf(stderr, format, args);
    std::fputc('\n', stderr);
    va_end(args);
}
}

static int failures;
static std::set<std::string> damaged; /* the stems said to be empty or unreadable, each once */

/* Whether ffprobe is installed: asked once. */
static bool have_ffprobe()
{
    static const bool have = std::system("ffprobe -version >/dev/null 2>&1") == 0;
    return have;
}

/* The file's length in seconds by ffprobe, or -1 when it does not say: it is not installed
 * (have_ffprobe), or it cannot read the file. */
static double length_of(const std::string &file)
{
    const std::string command =
        "ffprobe -v error -show_entries format=duration -of csv=p=0 '" + file + "' 2>/dev/null";
    double seconds = -1.0;
    if (FILE *pipe = popen(command.c_str(), "r")) {
        if (std::fscanf(pipe, "%lf", &seconds) != 1) {
            seconds = -1.0;
        }
        pclose(pipe);
    }
    return seconds;
}

/* One part of a video: there or not, and where it lies in the file. Returns whether it can play. */
static int part(const char *what, const char *stem, double from, double until, int needed)
{
    const std::string file = std::string("data/cutscenes/cinematics/") + stem + ".mp4";
    std::error_code size_error;
    char span[64] = "whole";
    if (until > 0.0) {
        std::snprintf(span, sizeof(span), "%.1f to %.1f s", from, until);
    } else if (from > 0.0) {
        std::snprintf(span, sizeof(span), "%.1f s to its end", from);
    }
    if (!std::filesystem::exists(file)) {
        std::printf("  %-14s %-8s %-18s NOT THERE%s\n", what, stem, span, needed ? "" : " (the game's own plays)");
        return 0;
    }
    if (std::filesystem::file_size(file, size_error) == 0 || size_error) {
        std::printf("  %-14s %-8s %-18s there, BUT EMPTY OR NOT READABLE\n", what, stem, span);
        if (damaged.insert(stem).second) {
            std::fprintf(stderr, "hd_videos: FAILED: %s.mp4 is empty or cannot be read\n", stem);
            ++failures;
        }
        return 0;
    }
    const double length = length_of(file);
    if (length < 0.0 && !have_ffprobe()) {
        std::printf("  %-14s %-8s %-18s there (length not known: no ffprobe)\n", what, stem, span);
        return 1;
    }
    if (length < 0.0) {
        std::printf("  %-14s %-8s %-18s there, BUT ffprobe CANNOT READ IT\n", what, stem, span);
        if (damaged.insert(stem).second) {
            std::fprintf(stderr, "hd_videos: FAILED: %s.mp4 cannot be read by ffprobe (damaged, or still being copied)\n",
                         stem);
            ++failures;
        }
        return 0;
    }
    if (from >= length) {
        std::printf("  %-14s %-8s %-18s there, %.1f s long: THE PART BEGINS PAST ITS END (the game's own plays)\n",
                    what, stem, span, length);
        return 0;
    }
    if (until > length) {
        std::printf("  %-14s %-8s %-18s there, %.1f s long: the part's end is past the file's, it plays to its end\n",
                    what, stem, span, length);
        return 1;
    }
    std::printf("  %-14s %-8s %-18s there, %.1f s long\n", what, stem, span, length);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "hd_videos <cartridge> <folder of hd*.mp4>\n");
        return 2;
    }
    std::error_code error;
    const std::filesystem::path folder = std::filesystem::absolute(argv[2], error);
    std::set<std::string> held; /* the stems the folder holds */
    if (std::filesystem::is_directory(folder, error)) {
        for (const auto &entry : std::filesystem::directory_iterator(folder, error)) {
            if (entry.path().extension() == ".mp4") {
                held.insert(entry.path().stem().string());
            }
        }
    }
    if (held.empty()) {
        std::printf("hd_videos: SKIPPED: no .mp4 in %s\n", folder.string().c_str());
        return 77;
    }
    if (!std::filesystem::exists(argv[1])) {
        std::printf("hd_videos: SKIPPED: no cartridge at %s\n", argv[1]);
        return 77;
    }
    const std::filesystem::path cartridge = std::filesystem::absolute(argv[1]);
    /* a working folder as the port's: data/cutscenes/cinematics is a link to the videos */
    const std::filesystem::path work = std::filesystem::current_path() / "hd-videos-work";
    const std::filesystem::path link = work / "data" / "cutscenes" / "cinematics";
    unlink(link.c_str()); /* a link left by a run that stopped: the link alone, never what it points to */
    std::filesystem::remove_all(work, error);
    std::filesystem::create_directories(work / "data" / "cutscenes");
    std::filesystem::create_directory_symlink(folder, link, error);
    if (error || chdir(work.c_str()) != 0) {
        std::fprintf(stderr, "hd_videos: no working folder %s\n", work.string().c_str());
        return 1;
    }
    if (setenv("KHDAYS_ROM", cartridge.c_str(), 1) != 0 || !khdays_rom_init()) {
        std::fprintf(stderr, "hd_videos: the cartridge cannot be opened\n");
        return 1;
    }

    const KhdaysSceneMap *map = khdays_scenes_map();
    std::printf("hd_videos: %zu videos in %s; %d entries in the scene map\n", held.size(), folder.string().c_str(),
                map->count);

    /* the game's movies */
    std::set<std::string> movies, used;
    int movies_shown = 0, movies_own = 0, movies_without = 0;
    int movies_held = 0; /* every video the map shows it as is in the folder (what the port's report counts) */
    std::printf("the game's movies (the cartridge's mv/):\n");
    for (int id = 0; id < khdays_rom_file_count(); ++id) {
        char path[256];
        u32 offset, bytes;
        if (!khdays_rom_file_info(id, path, (int)sizeof(path), &offset, &bytes)) {
            continue;
        }
        std::string name = path[0] == '/' ? path + 1 : path;
        if (name.rfind("mv/", 0) != 0 || name.size() < 8 || name.substr(name.size() - 5) != ".mods") {
            continue;
        }
        name = name.substr(3, name.find_first_of("._", 3) - 3);
        if (!movies.insert(name).second) {
            continue; /* the other languages' files of the same movie */
        }
        char stem[64];
        double from = 0.0, until = 0.0;
        const int parts = khdays_scene_map_movie(map, name.c_str(), 0, stem, sizeof(stem), &from, &until);
        const std::string what = "movie " + name;
        if (parts == 0) {
            std::printf("  %-14s the game's own (the map's mv#%s off)\n", what.c_str(), name.c_str());
            ++movies_own;
            continue;
        }
        int plays = 1, all_held = 1;
        for (int i = 0; i < parts; ++i) {
            khdays_scene_map_movie(map, name.c_str(), i, stem, sizeof(stem), &from, &until);
            const int there = part(what.c_str(), stem, from, until, 0);
            all_held = all_held && held.count(stem);
            if (held.count(stem)) {
                used.insert(stem);
                if (!there) {
                    std::fprintf(stderr, "hd_videos: FAILED: %s is there but movie %s cannot play its part of it\n",
                                 stem, name.c_str());
                    ++failures;
                }
            }
            plays = plays && there;
        }
        plays ? ++movies_shown : ++movies_without;
        movies_held += all_held;
    }
    for (const std::string &stem : held) {
        if (!used.count(stem)) {
            std::fprintf(stderr, "hd_videos: FAILED: %s.mp4 is in the folder but no game's movie is shown as it\n",
                         stem.c_str());
            ++failures;
        }
    }

    /* the map's scenes and monologues */
    int scenes = 0, scenes_shown = 0, voices = 0, voices_heard = 0, skips = 0;
    int scenes_held = 0, voices_held = 0; /* with every video in the folder (what the port's report counts) */
    std::printf("the map's scenes whose videos are there (or partly there):\n");
    for (int i = 0; i < map->count; ++i) {
        const KhdaysSceneEntry *e = &map->entries[i];
        if (e->off || std::strncmp(e->key, "mv#", 3) == 0) {
            continue;
        }
        if (e->skip) {
            ++skips;
            std::printf("  %-14s skipped after movie %s's video when that one plays on past the game's movie "
                        "(a player that says its videos' lengths; else always)\n",
                        e->key, e->stems[0] + 2);
            continue;
        }
        int any = 0, all = 1;
        for (int s = 0; s < e->stem_count; ++s) {
            any = any || held.count(e->stems[s]);
            all = all && held.count(e->stems[s]);
        }
        const int voice = std::strncmp(e->key, "mnl#", 4) == 0;
        if (all && e->stem_count > 0) {
            voice ? ++voices_held : ++scenes_held;
        }
        if (any) {
            for (int s = 0; s < e->stem_count; ++s) {
                all = part(e->key, e->stems[s], e->from ? e->from[s] : 0.0, e->until ? e->until[s] : 0.0, 0) && all;
            }
        }
        voice ? ++voices : ++scenes;
        if (all) {
            voice ? ++voices_heard : ++scenes_shown;
        }
    }
    std::printf("hd_videos: the game's movies: %zu, shown as the player's videos %d, the game's own by the map %d, "
                "without a video %d\n",
                movies.size(), movies_shown, movies_own, movies_without);
    std::printf("hd_videos: the map's scenes: %d, with all their videos %d; monologues' voices: %d, with theirs %d; "
                "scenes skipped after a movie's video: %d\n",
                scenes, scenes_shown, voices, voices_heard, skips);
    std::printf("the port's own report at its start:\n");
    std::fflush(stdout);
    khdays_scenes_videos_report();
    {
        /* the report's own numbers (its reading of the cartridge's movies, of the map and of the folder)
         * against this test's */
        KhdaysVideosCount c;
        char missing[512];
        khdays_scenes_videos_count(&c, missing, sizeof(missing));
        const struct {
            const char *what;
            int said, expected;
        } numbers[] = {{"the game's movies", c.movies, (int)movies.size()},
                       {"the movies that are the game's own by the map", c.movies_off, movies_own},
                       {"the movies with their videos", c.movies_there, movies_held},
                       {"the map's scenes", c.scenes, scenes},
                       {"the scenes with their videos", c.scenes_there, scenes_held},
                       {"the monologues' voices", c.voices, voices},
                       {"the voices with their videos", c.voices_there, voices_held}};
        for (const auto &n : numbers) {
            if (n.said != n.expected) {
                std::fprintf(stderr, "hd_videos: FAILED: the port's report counts %s as %d; the folder and the map give %d\n",
                             n.what, n.said, n.expected);
                ++failures;
            }
        }
        /* each movie it names as without a video is one this test found without, and the other way */
        int named = 0;
        for (const char *p = missing; *p != '\0'; ++p) {
            named += *p == ' ';
        }
        if (named != (int)movies.size() - movies_own - movies_held) {
            std::fprintf(stderr, "hd_videos: FAILED: the port's report names %d movies without a video (%s); %d are\n",
                         named, missing, (int)movies.size() - movies_own - movies_held);
            ++failures;
        }
        std::printf("hd_videos: the port's report: movies %d of %d (%d the game's own), scenes %d of %d, voices %d of %d: "
                    "%s\n",
                    c.movies_there, c.movies - c.movies_off, c.movies_off, c.scenes_there, c.scenes, c.voices_there,
                    c.voices, failures ? "see the failures" : "as the folder and the map give");
    }
    if (unlink(link.c_str()) == 0 && chdir("..") == 0) {
        std::filesystem::remove_all(work, error); /* the link is gone first: only this test's own folders */
    }
    if (failures) {
        std::fprintf(stderr, "hd_videos: %d checks FAILED\n", failures);
        return 1;
    }
    std::printf("hd_videos: passed\n");
    return 0;
}
