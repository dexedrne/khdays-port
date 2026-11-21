#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
. tools/ios/remote.sh
ssh -o BatchMode=yes "$target" "mkdir -p ~/$remote_dir/src"
# The upstream files plus only the new port sources. Private data is never a sync input.
{
    git ls-files -z -- ':!external/khdays-decomp'
    git ls-files --others --exclude-standard -z -- 'tools/ios/*.sh' 'tools/ios/*.plist' 'tools/ios/*.m' 'tools/ios/*.md' 'tools/ios/*.py' 'tools/ios/CMakeLists.txt' 'port/host/app/*.c' 'port/host/app/*.S' \
        'port/host/movies/movie_hd_unavailable.c' 'port/runtime/sound/dump_path_posix.cpp' 'port/compat/sections.h' 'port/host/app/*.h' 'port/sound/CMakeLists.txt' 'port/tools/game/CMakeLists.txt' 'port/tools/embed_text.cmake' 'port/runtime/movies/video_mod_cartridge.c' 'port/tests/*.cmake' 'port/tests/*_test.c' 'port/tests/*_test.cpp' 'port/runtime/core/*.c'
} | rsync --from0 --files-from=- --exclude='*.nds' --exclude='*.sav' --exclude='__pycache__/' \
        --exclude='*.pyc' -art -e 'ssh -o BatchMode=yes' ./ "$target:$remote_dir/src/"
# If populated, ship only the pinned decompilation's tracked source tree.
if git -C external/khdays-decomp rev-parse --show-toplevel | grep -Fxq "$PWD/external/khdays-decomp"; then
    pin=$(git rev-parse HEAD:external/khdays-decomp)
    git -C external/khdays-decomp archive "$pin" | ssh -o BatchMode=yes "$target" \
        "mkdir -p ~/$remote_dir/src/external/khdays-decomp && tar -xf - -C ~/$remote_dir/src/external/khdays-decomp"
fi
