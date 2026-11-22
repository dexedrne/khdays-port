/* For the tests that read the game's files alone (cartridge_test, hd_videos: runtime/core/rom.cpp with no
 * mods folder): the mods' side of khdays_rom_init, with no mod installed -- nothing unpacked, listed or
 * started, no room added, no save world's card. */
#include "../runtime/menus/saveworld.h"
#include "../runtime/mods/days.h"
#include "../runtime/mods/list.h"
#include "../runtime/rooms/added.h"

extern "C" {
void khdays_days_unpack_all(const char *) {}
void khdays_days_check_originals(const char *) {}
void khdays_mods_scan(const char *, const char *) {}
void khdays_mods_start(void) {}
int khdays_mods_started_count(void)
{
    return 0;
}
const char *khdays_mods_started_path(int)
{
    return nullptr;
}
void khdays_rooms_load(void) {}
void khdays_saveworld_card_read(u32) {}
const char *khdays_host_user_folder(void)
{
    return "saves";
}
}

bool khdays_rooms_serves(const std::string &)
{
    return false;
}

bool khdays_rooms_merge(const std::string &, const std::vector<unsigned char> &, std::vector<unsigned char> &,
                        const KhdaysGameFiles &)
{
    return false;
}
