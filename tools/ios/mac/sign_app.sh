#!/bin/bash
# Sign the Ninja-built app using an installed development identity and profile.
set -euo pipefail
cd "$(dirname "$0")/../../.."
: "${PROFILE:?Set PROFILE to the development provisioning profile for beer.vyvanse.khdays}"
identity=${SIGN_IDENTITY:-Apple Development}
app=../build/iphoneos/game-ninja/khdays-native.app
test -x "$app/khdays-native"
work=$(mktemp -d ../build/iphoneos/sign.XXXXXX)
trap 'rm -rf "$work"' EXIT
security cms -D -i "$PROFILE" >"$work/profile.plist"
python3 - "$work/profile.plist" "$app/Info.plist" "$work/entitlements.plist" <<'PYTHON'
import fnmatch
import plistlib
import sys

with open(sys.argv[1], "rb") as f:
    profile = plistlib.load(f)
with open(sys.argv[2], "rb") as f:
    bundle = plistlib.load(f)["CFBundleIdentifier"]
allowed = profile["Entitlements"]
identifier = profile["ApplicationIdentifierPrefix"][0] + "." + bundle
if bundle != "beer.vyvanse.khdays" or not fnmatch.fnmatchcase(identifier, allowed["application-identifier"]):
    sys.exit("The profile does not authorize this app")
entitlements = {"application-identifier": identifier}
for name in ("get-task-allow", "com.apple.developer.team-identifier"):
    if name in allowed:
        entitlements[name] = allowed[name]
with open(sys.argv[3], "wb") as f:
    plistlib.dump(entitlements, f)
PYTHON
keys=()
if [[ -n "${SIGN_KEYCHAIN:-}" ]]; then keys=(--keychain "$SIGN_KEYCHAIN"); fi
cp "$PROFILE" "$app/embedded.mobileprovision"
while IFS= read -r -d '' item; do
    nice -n 10 codesign --force --sign "$identity" ${keys[@]+"${keys[@]}"} --timestamp=none "$item"
done < <(find "$app" -depth \( -name '*.framework' -o -name '*.dylib' \) -print0)
nice -n 10 codesign --force --sign "$identity" ${keys[@]+"${keys[@]}"} --timestamp=none \
    --entitlements "$work/entitlements.plist" --generate-entitlement-der "$app"
codesign --verify --strict "$app"
