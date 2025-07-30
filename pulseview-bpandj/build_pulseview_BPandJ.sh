#!/bin/bash

# Build JulsView-BPandJ.app with Bus Pirate and Jumperless FALA Support  
# This script builds JulsView (local fork) using our enhanced libsigrok-falaj with all drivers

set -e

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="$SCRIPT_DIR/pulseview_BPandJ_build"
LIBSIGROK_FALA_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JULSEVIEW_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../julseview" && pwd)"
PULSEVIEW_BRANCH="main"
INSTALL_PREFIX="/usr/local"

print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_header() {
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN} $1${NC}"
    echo -e "${CYAN}========================================${NC}"
}

# Cache sudo credentials to avoid repeated password prompts
cache_sudo() {
    print_status "Caching sudo credentials..."
    sudo -v
    
    # Keep sudo alive in background
    while true; do sudo -n true; sleep 6000; kill -0 "$$" || exit; done 2>/dev/null &
}

# Close existing JulseView app if running
close_existing_app() {
    print_status "Checking for existing JulseView-BPandJ.app..."
    
    if pgrep -f "JulseView-BPandJ" > /dev/null; then
        print_status "Closing existing JulseView-BPandJ.app..."
        osascript -e 'tell application "JulseView-BPandJ" to quit' 2>/dev/null || true
        # Also try force quit if regular quit didn't work
        sleep 2
        if pgrep -f "JulseView-BPandJ" > /dev/null; then
            pkill -f "JulseView-BPandJ" || true
        fi
        print_success "Closed existing app"
    fi
}

# Force save all files in common editors
save_all_files() {
    print_status "Auto-saving files in common editors..."
    
    # # VSCode
    # osascript -e 'tell application "Visual Studio Code" to activate' 2>/dev/null || true
    # osascript -e 'tell application "System Events" to keystroke "s" using {command down, option down}' 2>/dev/null || true
    
    # Cursor
    osascript -e 'tell application "Cursor" to activate' 2>/dev/null || true
    osascript -e 'tell application "System Events" to keystroke "s" using {command down, option down}' 2>/dev/null || true
    
    # Other editors
    # for app in "Xcode" "TextEdit" "Sublime Text" "Atom"; do
    #     osascript -e "tell application \"$app\" to activate" 2>/dev/null || true
    #     osascript -e 'tell application "System Events" to keystroke "s" using command down' 2>/dev/null || true
    # done
    
    sleep 1  # Give apps time to save
    print_success "Auto-save completed"
}

# Fast rebuild - optimized version that skips some steps
fast_rebuild() {
    print_header "Fast Rebuild JulsView-BPandJ"
    
    # Cache sudo early
    cache_sudo
    
    # Close existing app
    close_existing_app
    
    # Save all files
    save_all_files
    
    # Quick dependency check (skip full check)
    if ! command -v cmake &> /dev/null; then
        print_error "cmake not found. Run '$0 deps' first."
        exit 1
    fi
    
    # Always build libsigrok for rebuild to ensure consistency
    print_status "Building libsigrok-falaj for clean rebuild..."
    cd "$LIBSIGROK_FALA_DIR"
    
    # Clean and rebuild libsigrok
    make clean 2>/dev/null || true
    ./autogen.sh
    ./configure \
        --prefix=/usr/local \
        --enable-bp5-binmode-fala \
        --enable-jumperless-mixed-signal \
        --disable-all-drivers \
        --enable-demo \
        --enable-fx2lafw \
        --enable-dreamsourcelab-dslogic \
        --enable-kingst-la2016 \
        --enable-saleae-logic16 \
        --enable-openbench-logic-sniffer \
        PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:/usr/local/lib/pkgconfig"
    
    make -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
    sudo make install
    
    # Setup build environment (lightweight)
    print_status "Setting up build environment..."
    mkdir -p "$WORK_DIR"
    cd "$WORK_DIR"
    
    if [[ "$OSTYPE" == "darwin"* ]]; then
        export PKG_CONFIG_PATH="/opt/homebrew/opt/qt@5/lib/pkgconfig:$PKG_CONFIG_PATH"
        export CMAKE_PREFIX_PATH="/opt/homebrew/opt/qt@5:$CMAKE_PREFIX_PATH"
        export PATH="/opt/homebrew/opt/qt@5/bin:$PATH"
    fi
    
    # Use local source (ensure fresh copy)
    print_status "Updating JulsView source link..."
    if [ -d "pulseview" ]; then
        rm -rf pulseview
    fi
    ln -sf "$JULSEVIEW_DIR" pulseview
    
    # Configure (only if needed)
    cd "$WORK_DIR/pulseview"
    if [ ! -d "build/CMakeFiles" ]; then
        print_status "Configuring build (first time)..."
        rm -rf build
        mkdir -p build
        cd build
        
        local cmake_opts=(
            "-DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5"
            "-DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX"
            "-DCMAKE_BUILD_TYPE=Release"
            "-DENABLE_DECODE=ON"
        )
        
        if [[ "$OSTYPE" == "darwin"* ]]; then
            cmake_opts+=(
                "-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15"
                "-DQt5_DIR=/opt/homebrew/opt/qt@5/lib/cmake/Qt5"
            )
        fi
        
        cmake .. "${cmake_opts[@]}"
    else
        cd build
        print_status "Using existing build configuration..."
    fi
    
    # Fast build
    print_status "Building JulsView (optimized)..."
    make -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
    
    # Create app bundle
    print_status "Creating app bundle..."
    local app_name="JulseView-BPandJ.app"
    local app_dir="$app_name/Contents"
    
    rm -rf "$app_name"
    mkdir -p "$app_dir/MacOS" "$app_dir/Frameworks" "$app_dir/Resources"
    
    # Copy executable with correct name
    cp pulseview "$app_dir/MacOS/JulseView-BPandJ"
    chmod +x "$app_dir/MacOS/JulseView-BPandJ"
    
    # Copy app icon
    local icon_source="$SCRIPT_DIR/julseviewIcon/iconJulseview1024.icns"
    if [ -f "$icon_source" ]; then
        print_status "Adding app icon..."
        cp "$icon_source" "$app_dir/Resources/iconJulseview1024.icns"
        chmod 644 "$app_dir/Resources/iconJulseview1024.icns"
        print_status "Icon copied and permissions set"
        # Verify the icon was copied
        if [ -f "$app_dir/Resources/iconJulseview1024.icns" ]; then
            local icon_size=$(stat -f%z "$app_dir/Resources/iconJulseview1024.icns" 2>/dev/null || echo "unknown")
            print_status "Icon file size: $icon_size bytes"
        fi
    else
        print_warning "App icon not found at: $icon_source"
        print_status "Looking for alternative icon formats..."
        # Try PNG version and convert if needed
        local png_source="$SCRIPT_DIR/julseviewIcon/iconJulseview1024.png"
        if [ -f "$png_source" ] && command -v sips &> /dev/null; then
            print_status "Converting PNG to ICNS..."
            sips -s format icns "$png_source" --out "$app_dir/Resources/iconJulseview1024.icns"
            chmod 644 "$app_dir/Resources/iconJulseview1024.icns"
        fi
    fi
    
    # Create Info.plist
    cat > "$app_dir/Info.plist" << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>JulseView-BPandJ</string>
    <key>CFBundleIdentifier</key>
    <string>org.sigrok.JulseView.BPandJ</string>
    <key>CFBundleName</key>
    <string>JulseView-BPandJ</string>
    <key>CFBundleDisplayName</key>
    <string>JulseView BP &amp; J</string>
    <key>CFBundleVersion</key>
    <string>1.0.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleIconFile</key>
    <string>iconJulseview1024.icns</string>
    <key>LSMinimumSystemVersion</key>
    <string>10.15</string>
</dict>
</plist>
EOF
    
    # Validate the Info.plist
    if command -v plutil &> /dev/null; then
        print_status "Validating Info.plist..."
        if plutil -lint "$app_dir/Info.plist" >/dev/null 2>&1; then
            print_success "Info.plist is valid"
        else
            print_error "Info.plist validation failed"
            plutil -lint "$app_dir/Info.plist"
        fi
    fi
    
    # Bundle Qt libraries
    if command -v macdeployqt &> /dev/null; then
        print_status "Bundling Qt libraries..."
        macdeployqt "$app_name"
    fi
    
    # Fix macOS "damaged app" issues
    print_status "Fixing macOS app permissions and signing..."
    
    # Remove quarantine attributes
    xattr -dr com.apple.quarantine "$app_name" 2>/dev/null || true
    
    # Set proper permissions
    chmod -R 755 "$app_name"
    chmod +x "$app_name/Contents/MacOS/JulseView-BPandJ"
    
    # Sign the app (ad-hoc signing to avoid "damaged" warnings)
    if command -v codesign &> /dev/null; then
        print_status "Code signing the app..."
        codesign --force --deep --sign - "$app_name" 2>/dev/null || true
    fi
    
    # Force macOS to refresh the icon cache
    # print_status "Refreshing macOS icon cache..."
    # sudo find /private/var/folders -name com.apple.dock.iconcache -delete 2>/dev/null || true
    # sudo find /private/var/folders -name com.apple.iconservices -exec rm -rf {} \; 2>/dev/null || true
    # killall Dock 2>/dev/null || true
    # killall Finder 2>/dev/null || true
    
    # Install
    print_status "Installing JulseView-BPandJ.app..."
    sudo rm -rf "/Applications/JulseView-BPandJ.app"  # Remove old version first
    sudo cp -R "$app_name" "/Applications/"
    sudo ln -sf "/Applications/JulseView-BPandJ.app/Contents/MacOS/JulseView-BPandJ" "/usr/local/bin/julseview-bpandj"
    
    # Clear quarantine on installed app too
    sudo xattr -dr com.apple.quarantine "/Applications/JulseView-BPandJ.app" 2>/dev/null || true
    
    # Verify the installation
    if [ -x "/Applications/JulseView-BPandJ.app/Contents/MacOS/JulseView-BPandJ" ]; then
        print_success "✓ App installed and executable verified"
    else
        print_error "App installation failed - executable not found or not executable"
        exit 1
    fi
    
    print_success "Fast rebuild completed!"
    
    # Auto-launch the app
    print_status "Launching JulseView-BPandJ..."
    open -a JulseView-BPandJ &
    
    print_success "✓ JulseView-BPandJ launched!"
}

# Check dependencies for building PulseView
check_dependencies() {
    print_header "Checking Build Dependencies"
    
    local missing_deps=()
    
    # Check for basic build tools
    for cmd in git cmake make pkg-config autoconf automake libtool; do
        if ! command -v "$cmd" &> /dev/null; then
            missing_deps+=("$cmd")
        fi
    done
    
    # Check for Qt5 (required for PulseView)
    if ! pkg-config --exists Qt5Widgets 2>/dev/null; then
        missing_deps+=("Qt5")
    fi
    
    # Check for boost
    if ! brew list boost &>/dev/null; then
        missing_deps+=("boost")
    fi
    
    # Check for libsigrok-falaj repository
    if [ ! -d "$LIBSIGROK_FALA_DIR" ]; then
        print_error "libsigrok-falaj repository not found at: $LIBSIGROK_FALA_DIR"
        print_status "Clone it with: git clone https://github.com/Architeuthis-Flux/libsigrok-falaj.git"
        exit 1
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        print_error "Missing dependencies: ${missing_deps[*]}"
        print_status "Install with:"
        echo "    brew install cmake pkg-config qt@5 boost autoconf automake libtool"
        echo "    export PKG_CONFIG_PATH=\"/opt/homebrew/opt/qt@5/lib/pkgconfig:\$PKG_CONFIG_PATH\""
        exit 1
    else
        print_success "All build dependencies available"
    fi
}

# Setup build environment
setup_build_env() {
    print_header "Setting Up Build Environment"
    
    # Create work directory
    mkdir -p "$WORK_DIR"
    cd "$WORK_DIR"
    
    # Set Qt5 paths for macOS
    if [[ "$OSTYPE" == "darwin"* ]]; then
        export PKG_CONFIG_PATH="/opt/homebrew/opt/qt@5/lib/pkgconfig:$PKG_CONFIG_PATH"
        export CMAKE_PREFIX_PATH="/opt/homebrew/opt/qt@5:$CMAKE_PREFIX_PATH"
        export PATH="/opt/homebrew/opt/qt@5/bin:$PATH"
    fi
    
    print_success "Build environment configured"
}

# Build libsigrok-falaj with both Bus Pirate and Jumperless FALA drivers
build_libsigrok_fala() {
    print_header "Building LibSigrok-FALA with BP5 and Jumperless Support"
    
    cd "$LIBSIGROK_FALA_DIR"
    
    # Make sure we're on the right branch with our changes
    print_status "Ensuring we're using bp5-fala branch with Jumperless support..."
    git checkout bp5-fala
    git pull origin bp5-fala
    
    # Clean any previous build
    make clean 2>/dev/null || true
    
    # Generate build files
    print_status "Generating build files..."
    ./autogen.sh
    
    # Configure with all FALA drivers enabled
    print_status "Configuring with BP5 and Jumperless Mixed-Signal drivers..."
    ./configure \
        --prefix=/usr/local \
        --enable-bp5-binmode-fala \
        --enable-jumperless-mixed-signal \
        --disable-all-drivers \
        --enable-demo \
        --enable-fx2lafw \
        --enable-dreamsourcelab-dslogic \
        --enable-kingst-la2016 \
        --enable-saleae-logic16 \
        --enable-openbench-logic-sniffer \
        PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:/usr/local/lib/pkgconfig"
    
    # Build
    print_status "Building libsigrok-falaj..."
    make -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
    
    # Install
    print_status "Installing libsigrok-falaj..."
    sudo make install
    
    print_success "LibSigrok-FALA with BP5, Jumperless FALA, and Jumperless Logic drivers installed"
}

# Build compatible libsigrokdecode
build_compatible_libsigrokdecode() {
    print_header "Building Compatible LibSigrokdecode"
    
    cd "$WORK_DIR"
    
    if [ ! -d "libsigrokdecode" ]; then
        print_status "Cloning libsigrokdecode..."
        git clone https://github.com/sigrokproject/libsigrokdecode.git
    fi
    
    cd libsigrokdecode
    
    # Use a compatible version/branch
    print_status "Checking out compatible version..."
    git checkout master
    git pull
    
    # Clean any previous build
    make clean 2>/dev/null || true
    
    # Build libsigrokdecode
    print_status "Building libsigrokdecode..."
    ./autogen.sh
    ./configure --prefix=/usr/local
    make -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
    sudo make install
    
    print_success "Compatible libsigrokdecode installed"
}

# Use local julseview fork
setup_julseview_source() {
    print_header "Setting Up JulsView Source"
    
    # Check if julseview directory exists
    if [ ! -d "$JULSEVIEW_DIR" ]; then
        print_error "JulsView fork not found at: $JULSEVIEW_DIR"
        print_status "Make sure your julseview fork is in the correct location"
        exit 1
    fi
    
    cd "$WORK_DIR"
    
    # Create a symbolic link or copy the source
    if [ -d "pulseview" ]; then
        print_status "Removing existing PulseView build directory..."
        rm -rf pulseview
    fi
    
    print_status "Creating link to local JulsView fork..."
    ln -sf "$JULSEVIEW_DIR" pulseview
    
    # Verify the source is ready
    if [ -f "pulseview/CMakeLists.txt" ]; then
        print_success "JulsView source ready from local fork"
        print_status "Using source from: $JULSEVIEW_DIR"
    else
        print_error "JulsView source appears to be invalid (no CMakeLists.txt found)"
        exit 1
    fi
}

# Configure JulsView build
configure_pulseview() {
    print_header "Configuring JulsView-BPandJ Build"
    
    cd "$WORK_DIR/pulseview"
    
    # Create build directory
    rm -rf build
    mkdir -p build
    cd build
    
    # Configure with cmake
    local cmake_opts=(
        "-DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5"
        "-DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX"
        "-DPKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/opt/homebrew/lib/pkgconfig"
        "-DCMAKE_BUILD_TYPE=Release"
        "-DENABLE_DECODE=ON"
    )
    
    if [[ "$OSTYPE" == "darwin"* ]]; then
        cmake_opts+=(
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15"
            "-DQt5_DIR=/opt/homebrew/opt/qt@5/lib/cmake/Qt5"
        )
    fi
    
    print_status "Running cmake configuration..."
    cmake .. "${cmake_opts[@]}"
    
    print_success "JulsView-BPandJ configured"
}

# Build JulsView
build_pulseview() {
    print_header "Building JulsView-BPandJ"
    
    cd "$WORK_DIR/pulseview/build"
    
    # Build with multiple cores
    local cores=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
    print_status "Building with $cores parallel jobs..."
    
    make -j"$cores"
    
    print_success "JulsView-BPandJ built successfully"
}

# Create application bundle (macOS)
create_app_bundle() {
    if [[ "$OSTYPE" != "darwin"* ]]; then
        return
    fi
    
    print_header "Creating JulseView-BPandJ.app Bundle"
    
    cd "$WORK_DIR/pulseview/build"
    
    # Create app bundle structure
    local app_name="JulseView-BPandJ.app"
    local app_dir="$app_name/Contents"
    
    rm -rf "$app_name"
    mkdir -p "$app_dir/MacOS"
    mkdir -p "$app_dir/Frameworks"
    mkdir -p "$app_dir/Resources"
    
    # Copy executable with correct name
    cp pulseview "$app_dir/MacOS/JulseView-BPandJ"
    chmod +x "$app_dir/MacOS/JulseView-BPandJ"
    
    # Copy app icon
    local icon_source="$SCRIPT_DIR/julseviewIcon/iconJulseview1024.icns"
    if [ -f "$icon_source" ]; then
        print_status "Adding app icon..."
        cp "$icon_source" "$app_dir/Resources/iconJulseview1024.icns"
        chmod 644 "$app_dir/Resources/iconJulseview1024.icns"
        print_status "Icon copied and permissions set"
        # Verify the icon was copied
        if [ -f "$app_dir/Resources/iconJulseview1024.icns" ]; then
            local icon_size=$(stat -f%z "$app_dir/Resources/iconJulseview1024.icns" 2>/dev/null || echo "unknown")
            print_status "Icon file size: $icon_size bytes"
        fi
    else
        print_warning "App icon not found at: $icon_source"
        print_status "Looking for alternative icon formats..."
        # Try PNG version and convert if needed
        local png_source="$SCRIPT_DIR/julseviewIcon/iconJulseview1024.png"
        if [ -f "$png_source" ] && command -v sips &> /dev/null; then
            print_status "Converting PNG to ICNS..."
            sips -s format icns "$png_source" --out "$app_dir/Resources/iconJulseview1024.icns"
            chmod 644 "$app_dir/Resources/iconJulseview1024.icns"
        fi
    fi
    
    # Copy Info.plist
    cat > "$app_dir/Info.plist" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>JulseView-BPandJ</string>
    <key>CFBundleIdentifier</key>
    <string>org.sigrok.JulseView.BPandJ</string>
    <key>CFBundleName</key>
    <string>JulseView-BPandJ</string>
    <key>CFBundleDisplayName</key>
    <string>JulseView BP &amp; J</string>
    <key>CFBundleVersion</key>
    <string>1.0.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleIconFile</key>
    <string>iconJulseview1024.icns</string>
    <key>LSMinimumSystemVersion</key>
    <string>10.15</string>
</dict>
</plist>
EOF
    
    # Validate the Info.plist
    if command -v plutil &> /dev/null; then
        print_status "Validating Info.plist..."
        if plutil -lint "$app_dir/Info.plist" >/dev/null 2>&1; then
            print_success "Info.plist is valid"
        else
            print_error "Info.plist validation failed"
            plutil -lint "$app_dir/Info.plist"
        fi
    fi
    
    # Use macdeployqt to bundle Qt libraries
    if command -v macdeployqt &> /dev/null; then
        print_status "Bundling Qt libraries..."
        macdeployqt "$app_name"
    else
        print_warning "macdeployqt not found - Qt libraries may not be bundled"
    fi
    
    # Fix macOS "damaged app" issues
    print_status "Fixing macOS app permissions and signing..."
    
    # Remove quarantine attributes
    xattr -dr com.apple.quarantine "$app_name" 2>/dev/null || true
    
    # Set proper permissions
    chmod -R 755 "$app_name"
    chmod +x "$app_name/Contents/MacOS/JulseView-BPandJ"
    
    # Sign the app (ad-hoc signing to avoid "damaged" warnings)
    if command -v codesign &> /dev/null; then
        print_status "Code signing the app..."
        codesign --force --deep --sign - "$app_name" 2>/dev/null || true
    fi
    
    # Force macOS to refresh the icon cache
    print_status "Refreshing macOS icon cache..."
    sudo find /private/var/folders -name com.apple.dock.iconcache -delete 2>/dev/null || true
    sudo find /private/var/folders -name com.apple.iconservices -exec rm -rf {} \; 2>/dev/null || true
    killall Dock 2>/dev/null || true
    killall Finder 2>/dev/null || true
    
    print_success "Created $app_name"
    print_status "Location: $WORK_DIR/pulseview/build/$app_name"
}

# Install JulseView
install_pulseview() {
    print_header "Installing JulseView-BPandJ"
    
    cd "$WORK_DIR/pulseview/build"
    
    if [[ "$OSTYPE" == "darwin"* ]] && [ -d "JulseView-BPandJ.app" ]; then
        print_status "Installing JulseView-BPandJ.app to /Applications..."
        
        # Remove old version first
        sudo rm -rf "/Applications/JulseView-BPandJ.app"
        sudo cp -R "JulseView-BPandJ.app" "/Applications/"
        
        # Clear quarantine on installed app
        sudo xattr -dr com.apple.quarantine "/Applications/JulseView-BPandJ.app" 2>/dev/null || true
        
        print_success "JulseView-BPandJ.app installed to Applications"
        
        # Create a symbolic link for easy command line access
        sudo ln -sf "/Applications/JulseView-BPandJ.app/Contents/MacOS/JulseView-BPandJ" "/usr/local/bin/julseview-bpandj"
        print_status "Created symlink: /usr/local/bin/julseview-bpandj"
    else
        print_status "Installing JulseView binary..."
        sudo make install
        print_success "JulseView-BPandJ installed to $INSTALL_PREFIX"
    fi
}

# Test the installation
test_installation() {
    print_header "Testing Installation"
    
    if [[ "$OSTYPE" == "darwin"* ]] && [ -d "/Applications/JulseView-BPandJ.app" ]; then
        print_status "Testing JulseView-BPandJ.app..."
        print_status "Available drivers should include:"
        echo "  - bp5-binmode-fala (Bus Pirate V5+ FALA)"
        echo "  - jumperless-mixed-signal (Jumperless Mixed-Signal)"
        echo ""
        print_status "Testing driver availability..."
        if command -v julseview-bpandj &> /dev/null; then
            julseview-bpandj --driver-list | grep -E "(bp5-binmode-fala|jumperless-mixed-signal)" || true
        fi
        print_success "✓ JulseView-BPandJ.app ready for testing"
        print_status "Launch with: open -a JulseView-BPandJ"
    else
        print_status "Testing pulseview binary..."
        if command -v pulseview &> /dev/null; then
            pulseview --driver-list | grep -E "(bp5-binmode-fala|jumperless-mixed-signal)" || true
            print_success "✓ PulseView binary available"
        else
            print_warning "? PulseView binary not found in PATH"
        fi
    fi
}

# Clean build artifacts
clean_build() {
    print_header "Cleaning Build Directory"
    
    print_status "Removing build directory: $WORK_DIR"
    rm -rf "$WORK_DIR"
    
    # Also clean libsigrok-falaj
    if [ -d "$LIBSIGROK_FALA_DIR" ]; then
        print_status "Cleaning libsigrok-falaj build artifacts..."
        cd "$LIBSIGROK_FALA_DIR"
        make clean 2>/dev/null || true
        rm -f config.log config.status Makefile 2>/dev/null || true
    fi
    
    print_success "Build directory cleaned"
}

# Usage information
usage() {
    echo "Build JulsView-BPandJ with Bus Pirate and Jumperless FALA Support"
    echo "Uses local julseview fork instead of cloning PulseView"
    echo ""
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  deps       - Install build dependencies"
    echo "  build      - Full build process"
    echo "  rebuild    - Fast rebuild: auto-save, close app, build, install & launch"
    echo "  install    - Install after building"
    echo "  clean      - Clean build directory"
    echo "  test       - Test installation"
    echo "  help       - Show this help"
    echo ""
    echo "Examples:"
    echo "  $0 deps      # Install dependencies first"
    echo "  $0 build     # Complete build process"
    echo "  $0 rebuild   # Quick rebuild from local changes (recommended)"
    echo "  $0 install   # Install the built app"
    echo ""
    echo "Prerequisites:"
    echo "  - julseview fork must be at: ../../julseview/"
    echo ""
    echo "The built app will support:"
    echo "  - Bus Pirate V5+ FALA (bp5-binmode-fala driver)"
    echo "  - Jumperless Mixed-Signal (jumperless-mixed-signal driver)"
}

# Install dependencies
install_deps() {
    print_header "Installing Build Dependencies"
    
    if [[ "$OSTYPE" == "darwin"* ]]; then
        print_status "Installing macOS dependencies..."
        brew install cmake pkg-config qt@5 boost autoconf automake libtool
        
        # Install macdeployqt if not available
        if ! command -v macdeployqt &> /dev/null; then
            print_status "Installing macdeployqt..."
            brew install qt@5
        fi
        
        print_success "Dependencies installed"
        print_warning "You may need to add Qt5 to your PATH:"
        echo "    export PATH=\"/opt/homebrew/opt/qt@5/bin:\$PATH\""
    else
        print_error "Dependency installation only supported on macOS"
        exit 1
    fi
}

# Main execution
case "${1:-build}" in
    deps)
        install_deps
        ;;
    build)
        cache_sudo
        close_existing_app
        save_all_files
        check_dependencies
        setup_build_env
        build_libsigrok_fala
        build_compatible_libsigrokdecode
        setup_julseview_source
        configure_pulseview
        build_pulseview
        create_app_bundle
        print_success "Build completed! Run '$0 install' to install."
        ;;
    rebuild)
        fast_rebuild
        ;;
    install)
        install_pulseview
        test_installation
        print_success "Installation completed!"
        print_status "Launch with: open -a JulseView-BPandJ"
        ;;
    clean)
        clean_build
        ;;
    test)
        test_installation
        ;;
    help)
        usage
        ;;
    *)
        print_error "Unknown command: $1"
        usage
        exit 1
        ;;
esac 