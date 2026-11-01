#!/bin/sh
# Builds the engine, then the app, and wraps the app in KettleLogic.app so macOS
# starts it as an app: with a menu bar and keyboard focus. Open the result with:
#   open apps/macos/build/KettleLogic.app
#
# The engine is a separate library, which the app loads at launch. The copy built
# here goes into the app as the built-in engine. Updates can install a newer one
# (see docs/updates.md).
set -e

# The app's own version, shown in About Kettle Logic. The engine's version is kettlelogic/version.hpp, separately.
app_version="0.2.0"
app_build="1"

# Where the app looks for updates: the update list on GitHub. Set UPDATE_MANIFEST_URL to use another, or to an empty
# string for a build with no update address.
update_manifest_url="${UPDATE_MANIFEST_URL-https://github.com/Kettle-Logic/Kettle-Logic/releases/download/updates/updates.json}"

here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
engine_version="$(sed -n 's/.*kEngineVersion = "\(.*\)";.*/\1/p' "$root/core/include/kettlelogic/version.hpp")"

# The signing identity, shared by the engine and the app. Set SIGN_IDENTITY (see below).
identity="${SIGN_IDENTITY:--}"
sign() {
    if [ "$identity" = "-" ]; then
        codesign --force --sign - "$1"
    else
        codesign --force --options runtime --timestamp --sign "$identity" "$1"
    fi
}

cmake -S "$root/core" -B "$root/build/core" > /dev/null
cmake --build "$root/build/core" --target kettlelogic_engine > /dev/null
engine="$root/build/core/libkettlelogic_engine.dylib"

cd "$here"
swift build -c release
bin="$(swift build -c release --show-bin-path)/KettleLogic"

app="$here/build/KettleLogic.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Frameworks"
cp "$bin" "$app/Contents/MacOS/KettleLogic"

# The built-in engine. It is signed before the app, which seals it in.
cp "$engine" "$app/Contents/Frameworks/libkettlelogic_engine.dylib"
sign "$app/Contents/Frameworks/libkettlelogic_engine.dylib"

# The user guide, read by the Help window.
mkdir -p "$app/Contents/Resources"
cp -R "$here/Help" "$app/Contents/Resources/Help"

# The app icon: a square 1024x1024 PNG at apps/macos/AppIcon.png, turned into AppIcon.icns.
# Without one the app gets macOS's generic icon.
icon_plist=""
if [ -f "$here/AppIcon.png" ]; then
    iconset="$(mktemp -d)/AppIcon.iconset"
    mkdir -p "$iconset"
    for size in 16 32 128 256 512; do
        sips -z $size $size "$here/AppIcon.png" --out "$iconset/icon_${size}x${size}.png" > /dev/null
        sips -z $((size * 2)) $((size * 2)) "$here/AppIcon.png" --out "$iconset/icon_${size}x${size}@2x.png" > /dev/null
    done
    iconutil -c icns "$iconset" -o "$app/Contents/Resources/AppIcon.icns"
    icon_plist="<key>CFBundleIconFile</key><string>AppIcon</string>"
fi

# Anonymous usage reports go to Google Analytics only in a build given its ids (see docs/privacy.md):
#   KL_GA_MEASUREMENT_ID=G-XXXXXXXXXX KL_GA_API_SECRET=... [KL_GA_DEBUG=YES] sh make-app.sh
ga_plist=""
if [ -n "${KL_GA_MEASUREMENT_ID:-}" ] && [ -n "${KL_GA_API_SECRET:-}" ]; then
    ga_plist="<key>KLGAMeasurementID</key><string>$KL_GA_MEASUREMENT_ID</string><key>KLGAAPISecret</key><string>$KL_GA_API_SECRET</string>"
    if [ "${KL_GA_DEBUG:-}" = "YES" ]; then
        ga_plist="$ga_plist<key>KLGADebug</key><string>YES</string>"
    fi
fi

cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key><string>KettleLogic</string>
    <key>CFBundleIdentifier</key><string>com.kettlelogic.KettleLogic</string>
    <key>CFBundleName</key><string>Kettle Logic</string>
    <key>CFBundleDisplayName</key><string>Kettle Logic</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    $icon_plist
    <key>CFBundleShortVersionString</key><string>$app_version</string>
    <key>CFBundleVersion</key><string>$app_build</string>
    <key>KLEngineVersion</key><string>$engine_version</string>
    <key>KLUpdateManifestURL</key><string>$update_manifest_url</string>
    $ga_plist
    <key>LSMinimumSystemVersion</key><string>26.0</string>
    <key>NSPrincipalClass</key><string>NSApplication</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

# Code signing. SIGN_IDENTITY names a certificate from your keychain, for example
#   SIGN_IDENTITY="Developer ID Application: Your Name (YOUR_TEAM_ID)" sh make-app.sh
# YOUR_TEAM_ID is the 10-character Team ID in the identity's parentheses. List the ones you have with: security find-identity -v -p codesigning
# Without it the app is signed ad hoc, which runs on this Mac but isn't distributable.
sign "$app"

echo "Built $app"
