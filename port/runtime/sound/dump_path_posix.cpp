#include <cstdio>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

extern "C" int khdays_dump_allowed(const char *directory, char *full, size_t size)
{
    std::error_code ec;
    auto path = fs::absolute(directory, ec);
    if (!ec) path = fs::weakly_canonical(path, ec);
    if (ec || path.native().size() >= size) {
        std::fprintf(stderr, "dump: cannot resolve %s\n", directory);
        return 0;
    }
    for (auto parent = path; ; parent = parent.parent_path()) {
        if (fs::exists(parent / ".git", ec)) {
            auto relative = path.lexically_relative(parent);
            if (relative.begin() == relative.end() || *relative.begin() != "saves") {
                std::fprintf(stderr, "dump: game data in a repository belongs under saves/\n");
                return 0;
            }
            break;
        }
        if (ec || parent == parent.root_path()) break;
    }
    if (ec) {
        std::fprintf(stderr, "dump: cannot inspect repository: %s\n", ec.message().c_str());
        return 0;
    }
    std::snprintf(full, size, "%s", path.c_str());
    return 1;
}
