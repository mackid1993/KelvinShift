#!/bin/bash
# build.sh – Compile KelvinShift and package as a .app bundle.
# Run from the project root:  ./build.sh
set -euo pipefail

echo "⏳  Building KelvinShift (release)…"
swift build -c release 2>&1

BIN=".build/release/KelvinShift"
if [ ! -f "$BIN" ]; then
    echo "❌  Build failed – binary not found."
    exit 1
fi

APP="KelvinShift.app"
echo "📦  Creating $APP bundle…"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
mkdir -p "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/KelvinShift"

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
    <string>1.0.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundleExecutable</key>
    <string>KelvinShift</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
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

# Ad-hoc codesign so macOS doesn't quarantine-block it
codesign --force --deep --sign - "$APP" 2>/dev/null || true

echo ""
echo "✅  Done → $APP"
echo ""
echo "   To install:"
echo "     cp -R KelvinShift.app /Applications/"
echo "     open /Applications/KelvinShift.app"
echo ""
echo "   To uninstall:"
echo "     rm -rf /Applications/KelvinShift.app"
echo "     defaults delete com.kelvinshift.app 2>/dev/null"
