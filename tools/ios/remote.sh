#!/bin/bash
set -eu
# The Mac that builds and signs: KHDAYS_MAC_HOST=user@host (required), KHDAYS_MAC_DIR=folder in its home (default khdays).
target="${KHDAYS_MAC_HOST:?set KHDAYS_MAC_HOST=user@host of your Mac}"; remote_dir="${KHDAYS_MAC_DIR:-khdays}"
