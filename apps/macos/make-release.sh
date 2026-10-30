#!/bin/sh
# Builds the files for a release of Kettle Logic on GitHub: the app zip that updates use, the DMG people install from,
# the engine, and the update list (updates.json) that the apps read. See docs/updates.md.
#
# The normal route is Xcode: archive the app, export it as a Developer ID app, and give the exported app with APP. Xcode
# signs it, and this notarizes and staples it with NOTARY_PROFILE. Without APP, the app is built from the command line,
# signed ad hoc, and cannot be notarized, so it is only for testing.
#
#   APP=/path/to/Exported/KettleLogic.app NOTARY_PROFILE=kettlelogic SIGN_IDENTITY="Developer ID Application: ..." \
#     REPO=kettle-logic/kettle-logic sh make-release.sh                       build into apps/macos/build/release
#   ... PUBLISH=1 sh make-release.sh                                          also publish with the gh tool
#
# Bump the version in Xcode (MARKETING_VERSION), and in core/include/kettlelogic/version.hpp for the engine, before a
# release. The script stops if the app's version files disagree with each other.
set -e

repo="${REPO:-kettle-logic/kettle-logic}"
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
out="$here/build/release"
manifest_url="https://github.com/$repo/releases/download/updates/updates.json"
base="https://github.com/$repo/releases/download"

engine_version="$(sed -n 's/.*kEngineVersion = "\(.*\)";.*/\1/p' "$root/core/include/kettlelogic/version.hpp")"
abi="$(sed -n 's/^#define KL_ENGINE_ABI_VERSION \([0-9][0-9]*\)$/\1/p' "$root/core/include/kettlelogic/c/kettlelogic.h")"
if [ -z "$engine_version" ] || [ -z "$abi" ]; then
    echo "Could not read the engine version or interface number from the engine's headers." >&2
    exit 1
fi

if [ -n "${APP:-}" ]; then
    app="$APP"
    if [ -z "${NOTARY_PROFILE:-}" ]; then
        echo "Warning: NOTARY_PROFILE is not set, so the app is not notarized. Users who download it will see a warning." >&2
    fi
else
    echo "Warning: no APP given, so this builds and signs the app from the command line. For a release, export it from Xcode." >&2
    UPDATE_MANIFEST_URL="$manifest_url" sh "$here/make-app.sh"
    app="$here/build/KettleLogic.app"
fi

# The version files must agree before anything is built, so a published update says what it contains.
plist() { /usr/libexec/PlistBuddy -c "Print :$1" "$app/Contents/Info.plist"; }
app_version="$(plist CFBundleShortVersionString)"
if [ "$(plist KLEngineVersion)" != "$engine_version" ]; then
    echo "The app's engine is $(plist KLEngineVersion), but version.hpp says $engine_version. Set KL_ENGINE_VERSION in the Xcode project to match." >&2
    exit 1
fi
if [ "$(plist KLUpdateManifestURL)" != "$manifest_url" ]; then
    echo "The app checks $(plist KLUpdateManifestURL), not $manifest_url. Set KL_UPDATE_MANIFEST_URL in the Xcode project, or the app will not find this release." >&2
    exit 1
fi
codesign --verify --deep --strict "$app"

# Notarize the app first: stapling changes it, so the update zip has to come after.
if [ -n "${NOTARY_PROFILE:-}" ] && [ -n "${APP:-}" ]; then
    sh "$here/notarize.sh" "$app"
fi

base_manifest="${BASE_MANIFEST:-}"
if [ -z "$base_manifest" ] && [ "${PUBLISH:-0}" = 1 ] && command -v gh > /dev/null; then
    mkdir -p "$here/build"
    base_manifest="$here/build/published-updates.json"
    gh release download updates --repo "$repo" --pattern updates.json --output "$base_manifest" --clobber 2> /dev/null \
        || { echo "No update list is published yet, so a new one is started." >&2; rm -f "$base_manifest"; }
fi

rm -rf "$out"
mkdir -p "$out"
app_zip="KettleLogic-$app_version-macos.zip"
dmg="KettleLogic-$app_version-macos.dmg"
engine_file="KettleLogic-engine-$engine_version-macos.dylib"
ditto -c -k --keepParent "$app" "$out/$app_zip"
cp "$app/Contents/Frameworks/libkettlelogic_engine.dylib" "$out/$engine_file"
sh "$here/make-dmg.sh" "$app" "$out/$dmg"
if [ -n "${NOTARY_PROFILE:-}" ] && [ -n "${APP:-}" ]; then
    sh "$here/notarize.sh" "$out/$dmg"
fi

# The update list is built from the one already published, so the entries for Windows and Linux stay. When publishing,
# that list is fetched first. BASE_MANIFEST can name a list to start from instead.
manifest="$out/updates.json"
if [ -n "$base_manifest" ] && [ -f "$base_manifest" ]; then
    cp "$base_manifest" "$manifest"
fi
python3 "$root/tools/update_manifest.py" "$manifest" app --version "$app_version" \
    --url "$base/app-v$app_version/$app_zip" --file "$out/$app_zip" --extra "engineAbi=$abi"
python3 "$root/tools/update_manifest.py" "$manifest" engine --version "$engine_version" \
    --url "$base/engine-v$engine_version/$engine_file" --file "$out/$engine_file" --extra "abi=$abi"

echo "Built the release files in $out:"
ls -1 "$out"

if [ "${PUBLISH:-0}" != 1 ]; then
    echo "Not published. Set PUBLISH=1 to upload them to $repo."
    exit 0
fi

command -v gh > /dev/null || { echo "The gh command-line tool is needed to publish." >&2; exit 1; }

if ! gh release view "app-v$app_version" --repo "$repo" > /dev/null 2>&1; then
    gh release create "app-v$app_version" "$out/$app_zip" "$out/$dmg" --repo "$repo" \
        --title "Kettle Logic $app_version" --notes "Kettle Logic $app_version for macOS. Install from the DMG."
else
    echo "app-v$app_version already exists, so it was not replaced. Bump the version for a new app release." >&2
fi
if ! gh release view "engine-v$engine_version" --repo "$repo" > /dev/null 2>&1; then
    gh release create "engine-v$engine_version" "$out/$engine_file" --repo "$repo" \
        --title "Engine $engine_version" --notes "The Kettle Logic engine, version $engine_version."
fi
# The update list has a fixed address, so it lives in one pre-release that is replaced each time. A pre-release is
# never the repository's "latest", so it does not hide the app release.
if ! gh release view updates --repo "$repo" > /dev/null 2>&1; then
    gh release create updates --repo "$repo" --prerelease --title "Update list" \
        --notes "Where Kettle Logic looks for updates. Do not delete this release."
fi
gh release upload updates "$out/updates.json" --repo "$repo" --clobber
echo "Published. The apps will find the update list at $manifest_url"
