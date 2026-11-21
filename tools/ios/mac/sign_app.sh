#!/bin/bash
set -eu
cd "$(dirname "$0")/../../.."
: "${TEAM_ID:?Set TEAM_ID to the Apple development team}"
project="$PWD/../build/iphoneos/game/khdays_native.xcodeproj"
exec nice -n 10 xcodebuild -project "$project" -target khdays-native -configuration Release -sdk iphoneos \
     -jobs 2 ARCHS=arm64 CODE_SIGNING_ALLOWED=YES CODE_SIGNING_REQUIRED=YES CODE_SIGN_STYLE=Automatic \
     "DEVELOPMENT_TEAM=$TEAM_ID" 'CODE_SIGN_IDENTITY=Apple Development' build
