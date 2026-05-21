#!/bin/bash
# build.sh — Compile KelvinShift, bundle as a .app, and package a
# release-ready .zip for GitHub.
#
# Usage:
#   ./build.sh
#   # App: KelvinShift.app
#   # Zip: KelvinShift.app.zip   (upload this to GitHub releases)

set -euo pipefail
cd "$(dirname "$0")"

echo "Building KelvinShift (release)..."
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

cat > "$APP/Contents/Info.plist" << 'PLIST'
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
    <string>1.0.2</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0.2</string>
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
    <string>MIT License</string>
</dict>
</plist>
PLIST

# Strip xattrs and AppleDouble (._*) files. If either remains when we sign,
# they get sealed in (or worse, appear later and invalidate the seal — which
# macOS reports as "the app is damaged and can't be opened").
find "$APP" -name '._*' -delete
xattr -cr "$APP"

# Ad-hoc sign (required for Gatekeeper on recent macOS). --deep covers nested
# resources; we let failures surface instead of swallowing them.
codesign --force --deep --sign - "$APP"
codesign --verify --verbose=2 "$APP"

# Package a release-ready zip. `ditto` is the only zipper that preserves the
# exec bit reliably across the GitHub upload/download path. --norsrc/--noextattr
# prevent xattrs from being smuggled into the archive as AppleDouble entries
# that would re-materialize on extract and break the signature seal.
ZIP="${APP}.zip"
rm -f "$ZIP"
COPYFILE_DISABLE=1 ditto --norsrc --noextattr --noacl -c -k --keepParent "$APP" "$ZIP"

# Round-trip the zip to confirm what downloaders will see.
VERIFY_DIR=$(mktemp -d)
ditto -x -k "$ZIP" "$VERIFY_DIR"
codesign --verify --verbose=2 "$VERIFY_DIR/$APP"
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
