#include "../runtime/core/rom.h"
#include "../runtime/core/text_patches.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

extern "C" void khdays_host_tell_no_data(const char *where, const char *why)
{
    std::fprintf(stderr, "%s: %s\n", where, why ? why : "missing game data");
}
// The extracted-file code must not be reached in cartridge mode.
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
    return !input || actual != expected || khdays_rom_file_count() < 1;
}
