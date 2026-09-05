#!/bin/bash
# build.sh — Compile KelvinShift, bundle as a .app, Developer ID sign,
# notarize, staple, and package a release-ready .zip for GitHub.
#
# Usage:
#   ./build.sh                 # full pipeline: sign → notarize → staple → zip
#   ./build.sh --no-notarize   # sign only (fast local iteration)
#
# Signing identity and notarytool keychain profile are read from the local
# keychain — nothing personal is checked in. By default the script picks the
# first "Developer ID Application" identity it finds; override either with:
#   KS_SIGN_ID="Developer ID Application: ..." KS_NOTARY_PROFILE=my-profile ./build.sh
#
#   # App: KelvinShift.app   (stapled)
#   # Zip: KelvinShift.zip   (upload this to GitHub releases)

set -euo pipefail
cd "$(dirname "$0")"

VERSION="3.0.5"
# Resolve the signing identity from the login keychain rather than hardcoding
# it, so this script carries no account details.
SIGN_ID="${KS_SIGN_ID:-$(security find-identity -v -p codesigning \
    | sed -n 's/.*"\(Developer ID Application: .*\)"/\1/p' | head -1)}"
NOTARY_PROFILE="${KS_NOTARY_PROFILE:-notary}"

NOTARIZE=1
for arg in "$@"; do
    case "$arg" in
        --no-notarize) NOTARIZE=0 ;;
        *) echo "ERROR: unknown argument: $arg"; exit 1 ;;
    esac
done

echo "Building KelvinShift $VERSION (release)..."
swift build -c release 2>&1

BIN=".build/release/KelvinShift"
if [ ! -f "$BIN" ]; then
    # SPM may place it in an arch-specific subdirectory
    BIN=$(find .build -name KelvinShift -type f -perm +111 2>/dev/null | head -1)
fi
if [ ! -f "$BIN" ]; then
    echo "ERROR: Build succeeded but binary not found"
    exit 1
fi

APP="KelvinShift.app"
echo "Creating $APP bundle..."
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
mkdir -p "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/KelvinShift"
cp "AppIcon.icns" "$APP/Contents/Resources/AppIcon.icns"

cat > "$APP/Contents/Info.plist" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>KelvinShift</string>
    <key>CFBundleDisplayName</key>
    <string>KelvinShift</string>
    <key>CFBundleIdentifier</key>
    <string>com.kelvinshift.app</string>
    <key>CFBundleVersion</key>
    <string>$VERSION</string>
    <key>CFBundleShortVersionString</key>
    <string>$VERSION</string>
    <key>CFBundleExecutable</key>
    <string>KelvinShift</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>LSUIElement</key>
    <true/>
    <key>NSLocationUsageDescription</key>
    <string>KelvinShift uses your location to calculate sunrise/sunset for automatic color temperature scheduling.</string>
    <key>NSHumanReadableCopyright</key>
    <string>Copyright © 2026 David Brustein — PolyForm Strict License 1.0.0</string>
</dict>
</plist>
PLIST

# Strip xattrs, AppleDouble (._*), and .DS_Store files. If any remain when
# we sign, they get sealed in (or worse, appear later and invalidate the
# seal — which macOS reports as "the app is damaged and can't be opened").
find "$APP" \( -name '._*' -o -name '.DS_Store' \) -delete
xattr -cr "$APP"

# Developer ID sign. --options runtime enables the hardened runtime and
# --timestamp gets a secure timestamp; notarization rejects the submission
# without both. No --deep: Apple discourages it, and this bundle is a single
# binary with no nested code to sign anyway.
if [ -n "$SIGN_ID" ] && security find-identity -v -p codesigning | grep -qF "$SIGN_ID"; then
    echo "Signing with: $SIGN_ID"
    codesign --force --timestamp --options runtime --sign "$SIGN_ID" "$APP"
else
    echo "WARNING: no Developer ID Application identity found — falling back to ad-hoc signing."
    echo "         The result cannot be notarized."
    codesign --force --sign - "$APP"
    NOTARIZE=0
fi
codesign --verify --strict --verbose=2 "$APP"

# Package. `ditto` is the only zipper that preserves the exec bit reliably
# across the GitHub upload/download path. --norsrc/--noextattr prevent xattrs
# from being smuggled into the archive as AppleDouble entries that would
# re-materialize on extract and break the signature seal.
ZIP="KelvinShift.zip"
make_zip() {
    rm -f "$ZIP"
    COPYFILE_DISABLE=1 ditto --norsrc --noextattr --noacl -c -k --keepParent "$APP" "$ZIP"
}
make_zip

if [ "$NOTARIZE" -eq 1 ]; then
    echo ""
    echo "Submitting to Apple for notarization (profile: $NOTARY_PROFILE)..."
    # Notarization takes the zip; the ticket is then stapled to the .app, so
    # the zip has to be rebuilt afterward to carry the stapled copy.
    xcrun notarytool submit "$ZIP" --keychain-profile "$NOTARY_PROFILE" --wait

    echo "Stapling ticket to $APP..."
    xcrun stapler staple "$APP"
    xcrun stapler validate "$APP"

    echo "Repackaging stapled app..."
    make_zip
fi

# Round-trip the zip to confirm what downloaders will actually get. spctl is
# the check that matters: codesign --verify only proves the signature is
# intact, not that Gatekeeper will accept the app.
VERIFY_DIR=$(mktemp -d)
ditto -x -k "$ZIP" "$VERIFY_DIR"
codesign --verify --strict --verbose=2 "$VERIFY_DIR/$APP"
if [ "$NOTARIZE" -eq 1 ]; then
    xcrun stapler validate "$VERIFY_DIR/$APP"
    spctl -a -vvv -t install "$VERIFY_DIR/$APP"
fi
rm -rf "$VERIFY_DIR"

echo ""
echo "App bundle: $(pwd)/$APP"
echo "Zip:        $(pwd)/$ZIP   (upload this to GitHub releases)"
echo ""
echo "   To install:"
echo "     cp -R KelvinShift.app /Applications/"
echo "     open /Applications/KelvinShift.app"
echo ""
echo "   To uninstall:"
echo "     rm -rf /Applications/KelvinShift.app"
echo "     defaults delete com.kelvinshift.app 2>/dev/null"
