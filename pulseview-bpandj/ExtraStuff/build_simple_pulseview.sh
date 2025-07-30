#!/bin/bash

# Simple PulseView Build (Logic Analyzer Only)
# Builds PulseView without decoder support to avoid API compatibility issues

set -e

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_header() {
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN} $1${NC}"
    echo -e "${CYAN}========================================${NC}"
}

# Clean up previous build
print_header "Cleaning Previous Build"
cd pulseview_build/pulseview/build 2>/dev/null || {
    print_error "Build directory not found. Run the main build script first."
    exit 1
}

make clean 2>/dev/null || true

# Reconfigure without decoder support
print_header "Reconfiguring PulseView (Logic Analyzer Only)"

# Set Qt5 paths
export PKG_CONFIG_PATH="/opt/homebrew/opt/qt@5/lib/pkgconfig:$PKG_CONFIG_PATH"
export CMAKE_PREFIX_PATH="/opt/homebrew/opt/qt@5:$CMAKE_PREFIX_PATH"
export PATH="/opt/homebrew/opt/qt@5/bin:$PATH"

# Configure cmake without decoder support
cmake .. \
    -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5 \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DPKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/opt/homebrew/lib/pkgconfig \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_DECODE=OFF \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 \
    -DQt5_DIR=/opt/homebrew/opt/qt@5/lib/cmake/Qt5

print_success "Reconfigured without decoder support"

# Build PulseView
print_header "Building PulseView"
make -j$(nproc 2>/dev/null || echo 4)

print_success "Build completed!"

# Create app bundle
print_header "Creating PulseView-Jumperless.app"

# Create app bundle structure
app_name="PulseView-Jumperless.app"
app_dir="$app_name/Contents"

rm -rf "$app_name"
mkdir -p "$app_dir/MacOS"
mkdir -p "$app_dir/Frameworks"
mkdir -p "$app_dir/Resources"

# Copy executable
cp pulseview "$app_dir/MacOS/"

# Copy Info.plist
cat > "$app_dir/Info.plist" << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>pulseview</string>
    <key>CFBundleIdentifier</key>
    <string>org.sigrok.PulseView.Jumperless</string>
    <key>CFBundleName</key>
    <string>PulseView-Jumperless</string>
    <key>CFBundleVersion</key>
    <string>1.0.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
</dict>
</plist>
EOF

# Use macdeployqt to bundle Qt libraries
if command -v macdeployqt &> /dev/null; then
    print_status "Bundling Qt libraries..."
    macdeployqt "$app_name"
fi

print_success "Created $app_name"

# Install
print_header "Installing PulseView-Jumperless.app"
sudo cp -R "$app_name" "/Applications/"
print_success "✓ Installed to /Applications/PulseView-Jumperless.app"

# Test
print_header "Testing Installation"
open -a PulseView-Jumperless &
sleep 3
if pgrep -f "PulseView-Jumperless" > /dev/null; then
    print_success "✓ PulseView-Jumperless.app launched successfully!"
    print_status "The Jumperless driver should be available in the device list."
else
    print_error "✗ App may have failed to launch"
fi

echo ""
print_success "🎉 PulseView-Jumperless.app is ready!"
echo -e "${CYAN}Features:${NC}"
echo "  ✓ Logic Analyzer functionality"
echo "  ✓ Jumperless driver support"
echo "  ✓ All standard PulseView features"
echo "  ✗ Protocol decoders (disabled to avoid API issues)"
echo ""
echo -e "${YELLOW}Note:${NC} This version focuses on logic analysis."
echo "Protocol decoding can be added later with compatible libsigrokdecode." 