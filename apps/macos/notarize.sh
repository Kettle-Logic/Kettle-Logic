#!/bin/sh
# Sends a signed Kettle Logic.app or DMG to Apple for notarization, then staples the ticket to it, so it opens without a
# network check. The signature comes from Xcode (Product > Archive, then Distribute App > Developer ID) for the app, and
# from SIGN_IDENTITY in make-dmg.sh for the DMG. Set up the notarytool keychain profile once:
#
#   xcrun notarytool store-credentials kettlelogic --apple-id YOU@EXAMPLE.COM --team-id YOUR_TEAM_ID
#
# (it asks for an app-specific password; YOUR_TEAM_ID is the 10-character Team ID under Membership details at
# https://developer.apple.com/account), then:
#
#   NOTARY_PROFILE=kettlelogic sh notarize.sh path/to/KettleLogic.app
#   NOTARY_PROFILE=kettlelogic sh notarize.sh path/to/KettleLogic-0.2.0-macos.dmg
#
# notarytool --wait returns when Apple has decided. A rejected item stops this script, because stapling fails.
set -e

item="${1:?usage: notarize.sh <KettleLogic.app or .dmg>}"
profile="${NOTARY_PROFILE:?Set NOTARY_PROFILE to the notarytool keychain profile}"

case "$item" in
    *.app)
        # An app that already carries a valid ticket (from an earlier run) needs nothing more, and stapling it again can fail.
        if xcrun stapler validate "$item" > /dev/null 2>&1; then
            echo "Already notarized and stapled: $item"
            exit 0
        fi
        # Apple takes a zip. The ticket is then stapled to the app itself, so the app keeps it after it is unzipped.
        work="$(mktemp -d)"
        trap 'rm -rf "$work"' EXIT
        ditto -c -k --keepParent "$item" "$work/notarize.zip"
        xcrun notarytool submit "$work/notarize.zip" --keychain-profile "$profile" --wait
        xcrun stapler staple "$item"
        spctl --assess --type execute --verbose=2 "$item"
        ;;
    *.dmg)
        xcrun notarytool submit "$item" --keychain-profile "$profile" --wait
        xcrun stapler staple "$item"
        spctl --assess --type open --context context:primary-signature --verbose=2 "$item"
        ;;
    *)
        echo "notarize.sh handles a .app or a .dmg, not: $item" >&2
        exit 1
        ;;
esac
echo "Notarized and stapled: $item"
