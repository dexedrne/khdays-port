#include "../runtime/core/rom.h"
#include "../runtime/core/enemy_overlays.h"
#include "../runtime/core/text_patches.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

extern "C" void khdays_host_tell_no_data(const char *where, const char *why)
{
    std::fprintf(stderr, "%s: %s\n", where, why ? why : "missing game data");
}
// The extracted-file code must not be reached in cartridge mode.
extern "C" void khdays_log(const char *, ...) {}
extern "C" void khdays_origin_want(void)
{
    std::abort();
}
bool khdays_text_patch(const std::string &, const std::filesystem::path &, std::vector<uint8_t> &)
{
    std::abort();
}

int main(int argc, char **argv)
{
    if (argc != 2 || setenv("KHDAYS_ROM", argv[1], 1) || !khdays_rom_init())
        return 1;
    std::ifstream input(argv[1], std::ios::binary);
    std::array<unsigned char, 512> expected{}, actual{};
    input.read(reinterpret_cast<char *>(expected.data()), expected.size());
    khdays_rom_read(0, actual.data(), actual.size());
    if (!input || actual != expected || std::memcmp(khdays_rom_header(), expected.data(), 512))
        return 1;
    u32 pos, len;
    if (!khdays_rom_find("/snd/sound_data.sdat", &pos, &len) || len < actual.size())
        return 1;
    input.seekg(pos).read(reinterpret_cast<char *>(expected.data()), expected.size());
    khdays_rom_read(pos, actual.data(), actual.size());
    if (!input || actual != expected || khdays_rom_file_count() < 1)
        return 1;
    // The overlay table as the game reads it off the card: the enemies' overlays (114-301) each at its own place
    // (enemy_overlays.h), the DS's address kept for khdays_rom_overlay_home; the others as the card has them. A
    // read that begins before the table and ends inside it is laid over the same way.
    u32 table_at, table_size;
    std::memcpy(&table_at, khdays_rom_header() + 0x50, 4);
    std::memcpy(&table_size, khdays_rom_header() + 0x54, 4);
    std::vector<unsigned char> card(table_size + 64), read(table_size + 64);
    input.clear();
    input.seekg(table_at - 32).read(reinterpret_cast<char *>(card.data()), card.size());
    khdays_rom_read(table_at - 32, read.data(), static_cast<u32>(read.size()));
    if (!input || table_size % 32 || table_size / 32 <= KHDAYS_ENEMY_OVERLAY_LAST ||
        std::memcmp(card.data(), read.data(), 32) || std::memcmp(card.data() + 32 + table_size, read.data() + 32 + table_size, 32))
        return 2;
    int moved = 0;
    for (u32 at = 32; at < 32 + table_size; at += 32) {
        u32 on_card[8], seen[8];
        std::memcpy(on_card, card.data() + at, 32);
        std::memcpy(seen, read.data() + at, 32);
        const u32 id = on_card[0];
        if (id < KHDAYS_ENEMY_OVERLAY_FIRST || id > KHDAYS_ENEMY_OVERLAY_LAST) {
            if (std::memcmp(on_card, seen, 32) || khdays_rom_overlay_home(static_cast<int>(id)) != 0)
                return 3;
            continue;
        }
        const u32 place = KHDAYS_ENEMY_OVERLAYS + (id - KHDAYS_ENEMY_OVERLAY_FIRST) * KHDAYS_ENEMY_OVERLAY_SPAN;
        if (seen[0] != id || seen[1] != place || seen[2] != on_card[2] || seen[3] != on_card[3] ||
            seen[6] != on_card[6] || seen[7] != on_card[7] || khdays_rom_overlay_home(static_cast<int>(id)) != on_card[1])
            return 4;
        for (const int field : {4, 5}) {
            if (seen[field] != (on_card[field] ? on_card[field] - on_card[1] + place : 0))
                return 5;
        }
        ++moved;
    }
    std::printf("cartridge: %u overlays in the table, %d enemies' moved to their own places\n", table_size / 32, moved);
    return moved == KHDAYS_ENEMY_OVERLAY_LAST - KHDAYS_ENEMY_OVERLAY_FIRST + 1 ? 0 : 6;
}
