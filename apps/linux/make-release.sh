#!/bin/sh
# Builds the Linux Flatpak bundle and, with PUBLISH=1, uploads it to GitHub Releases and updates the update list. See
# docs/updates.md. Run it on Linux (or in WSL) with flatpak, flatpak-builder, python3, and the gh tool signed in.
#
#   sh apps/linux/make-release.sh                  build into apps/linux/build/release
#   PUBLISH=1 sh apps/linux/make-release.sh        also publish with the gh tool
#
# Set REPO to publish somewhere other than Kettle-Logic/Kettle-Logic.
set -e

repo="${REPO:-Kettle-Logic/Kettle-Logic}"
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
out="$here/build/release"
work="$here/build/flatpak"
app_id="org.kettlelogic.KettleLogic"

version="$(sed -n 's/^project(KettleLogicGtk VERSION \([0-9.]*\) .*/\1/p' "$here/CMakeLists.txt")"
if [ -z "$version" ]; then
    echo "Could not read the app's version from apps/linux/CMakeLists.txt." >&2
    exit 1
fi
arch="$(uname -m)"
bundle="KettleLogic-$version-$arch.flatpak"

for tool in flatpak flatpak-builder python3; do
    command -v "$tool" > /dev/null || { echo "$tool is needed. See docs/updates.md." >&2; exit 1; }
done
if [ "${PUBLISH:-0}" = 1 ]; then
    command -v gh > /dev/null || { echo "The gh command-line tool is needed to publish." >&2; exit 1; }
fi

# 1. Build the Flatpak into a repository, then bundle it into one file.
rm -rf "$out" "$work"
mkdir -p "$out"
flatpak-builder --force-clean --repo="$work/repo" "$work/build" "$here/flatpak/$app_id.yml"
flatpak build-bundle "$work/repo" "$out/$bundle" "$app_id"

# 2. The update list. Start from the published one so the other platforms' entries stay.
manifest="$out/updates.json"
if [ "${PUBLISH:-0}" = 1 ]; then
    gh release download updates --repo "$repo" --pattern updates.json --output "$manifest" --clobber 2> /dev/null \
        || { echo "No update list is published yet, so a new one is started." >&2; rm -f "$manifest"; }
fi
python3 "$root/tools/update_manifest.py" "$manifest" linux --version "$version" \
    --url "https://github.com/$repo/releases/tag/app-v$version"

echo "Built the release files in $out:"
ls -1 "$out"
if [ "${PUBLISH:-0}" != 1 ]; then
    echo "Not published. Set PUBLISH=1 to upload them to $repo."
    exit 0
fi

# 3. Publish. The bundle goes on app-v<version>, and the list replaces the one on the fixed "updates" release.
if ! gh release view "app-v$version" --repo "$repo" > /dev/null 2>&1; then
    gh release create "app-v$version" "$out/$bundle" --repo "$repo" \
        --title "Kettle Logic $version" --notes "Kettle Logic $version for Linux."
else
    # The release exists, for example because the macOS build published it first. Add this bundle to it.
    gh release upload "app-v$version" "$out/$bundle" --repo "$repo" --clobber
fi
if ! gh release view updates --repo "$repo" > /dev/null 2>&1; then
    gh release create updates --repo "$repo" --prerelease --title "Update list" \
        --notes "Where Kettle Logic looks for updates. Do not delete this release."
fi
gh release upload updates "$manifest" --repo "$repo" --clobber
echo "Published. The apps will find the update list at https://github.com/$repo/releases/download/updates/updates.json"
