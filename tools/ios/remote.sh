#!/bin/bash
set -eu
target="${KHDAYS_MAC_HOST:?set KHDAYS_MAC_HOST=user@host of your Mac}"
default_dir=Developer/KHDAYS
remote_dir="${KHDAYS_MAC_DIR:-$default_dir}"
[[ "$remote_dir" =~ ^[A-Za-z0-9_./-]+$ && "$remote_dir" != /* && "$remote_dir" != *..* ]] ||
    { echo "KHDAYS_MAC_DIR must be a relative Mac work directory" >&2; exit 2; }
