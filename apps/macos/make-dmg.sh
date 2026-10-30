#!/bin/sh
# Packs Kettle Logic.app into a DMG, with a link to Applications beside it, so the user drags the app across.
# The window shows dmg-background.png (with its @2x twin) and the two icons are placed on top of it.
#
#   sh make-dmg.sh path/to/KettleLogic.app path/to/KettleLogic-0.1.0-macos.dmg
#
# SIGN_IDENTITY signs the DMG with a Developer ID, which notarization needs. Left unset, the DMG is not signed.
# Laying out the window asks Finder to run, so the first build may prompt to let Terminal control Finder.
set -e

app="${1:?usage: make-dmg.sh <KettleLogic.app> <output.dmg>}"
out="${2:?usage: make-dmg.sh <KettleLogic.app> <output.dmg>}"
here="$(cd "$(dirname "$0")" && pwd)"
name="$(basename "$app")"

work="$(mktemp -d)"
trap 'hdiutil detach "$vol" -quiet 2>/dev/null || true; rm -rf "$work"' EXIT

stage="$work/stage"
mkdir "$stage"
# ditto keeps the signature and the notarization ticket, which a plain copy can lose.
ditto "$app" "$stage/$name"
ln -s /Applications "$stage/Applications"

# Build a writable image first, so Finder can save the window layout into it.
rw="$work/rw.dmg"
size_mb=$(( $(du -sm "$stage" | cut -f1) + 30 ))
hdiutil create -volname "Kettle Logic" -srcfolder "$stage" -size "${size_mb}m" -ov -format UDRW "$rw" > /dev/null
# Finder only lists volumes under /Volumes, so let hdiutil mount it there and read back where.
vol="$(hdiutil attach "$rw" -noverify -noautoopen | tail -n 1 | cut -f 3-)"

if [ -f "$here/dmg-background.png" ] && [ -f "$here/dmg-background@2x.png" ]; then
    mkdir "$vol/.background"
    # One TIFF holding both resolutions, so Retina displays get the sharp image.
    tiffutil -cathidpicheck "$here/dmg-background.png" "$here/dmg-background@2x.png" -out "$vol/.background/background.tiff" > /dev/null
    # Window is 600x400 points. Icon centres match the arrow in the background.
    osascript <<EOF || echo "warning: could not lay out the DMG window; it will open without the background" >&2
tell application "Finder"
    tell disk "$(basename "$vol")"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {200, 120, 800, 520}
        set viewOptions to the icon view options of container window
        set arrangement of viewOptions to not arranged
        set icon size of viewOptions to 112
        set text size of viewOptions to 13
        set background picture of viewOptions to file ".background:background.tiff"
        set position of item "$name" of container window to {150, 190}
        set position of item "Applications" of container window to {450, 190}
        update without registering applications
        delay 2
        close
    end tell
end tell
EOF
    # Finder writes its layout to .DS_Store when the window closes; wait for that before detaching.
    sync
    sleep 2
fi

hdiutil detach "$vol" -quiet || hdiutil detach "$vol" -force -quiet

rm -f "$out"
hdiutil convert "$rw" -format UDZO -imagekey zlib-level=9 -o "$out" > /dev/null
if [ -n "${SIGN_IDENTITY:-}" ] && [ "$SIGN_IDENTITY" != "-" ]; then
    codesign --force --timestamp --sign "$SIGN_IDENTITY" "$out"
fi
echo "Built $out"
