#!/bin/bash
set -eu
. "$(dirname "$0")/remote.sh"
sdk=${2:-${1:-iphonesimulator}}
case "$sdk" in iphoneos|iphonesimulator) ;; *) exit 2;; esac
exec ssh -o BatchMode=yes "$target" "cd ~/$remote_dir/src && exec bash tools/ios/mac/build.sh $sdk"
