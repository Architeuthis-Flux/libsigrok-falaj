#!/bin/bash

# Build PulseView from Source with Jumperless Driver
# This script builds a custom PulseView that includes the Jumperless driver

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
WORK_DIR="$SCRIPT_DIR/pulseview_build"
PULSEVIEW_REPO="https://github.com/sigrokproject/pulseview.git"
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

# Clone PulseView source
clone_pulseview() {
    print_header "Cloning PulseView Source"
    
    if [ -d "pulseview" ]; then
        print_status "Updating existing PulseView repository..."
        cd pulseview
        git pull
        cd ..
    else
        print_status "Cloning PulseView repository..."
        git clone "$PULSEVIEW_REPO"
    fi
    
    print_success "PulseView source ready"
}

# Build our custom libsigrok first
build_custom_libsigrok() {
    print_header "Building Custom LibSigrok with Jumperless Driver"
    
    # Use our existing build script
    cd "$SCRIPT_DIR"
    ./build_jumperless_driver.sh build
    ./build_jumperless_driver.sh install
    
    print_success "Custom libsigrok with Jumperless driver installed"
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
    
    # Build libsigrokdecode
    print_status "Building libsigrokdecode..."
    ./autogen.sh
    ./configure --prefix=/usr/local
    make -j$(nproc 2>/dev/null || echo 4)
    sudo make install
    
    print_success "Compatible libsigrokdecode installed"
}

# Configure PulseView build
configure_pulseview() {
    print_header "Configuring PulseView Build"
    
    cd "$WORK_DIR/pulseview"
    
    # Use a more stable branch/tag instead of latest master
    print_status "Using stable PulseView version..."
    git checkout $(git describe --tags --abbrev=0 2>/dev/null || echo "master")
    
    # Create build directory
    mkdir -p build
    cd build
    
    # Configure with cmake - disable problematic features if needed
    local cmake_opts=(
        "-DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5"
        "-DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX"
        "-DPKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/opt/homebrew/lib/pkgconfig"
        "-DCMAKE_BUILD_TYPE=Release"
        "-DENABLE_DECODE=OFF"  # Disable decode to avoid API issues
    )
    
    if [[ "$OSTYPE" == "darwin"* ]]; then
        cmake_opts+=(
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15"
            "-DQt5_DIR=/opt/homebrew/opt/qt@5/lib/cmake/Qt5"
        )
    fi
    
    print_status "Running cmake configuration..."
    cmake .. "${cmake_opts[@]}"
    
    print_success "PulseView configured"
}

# Build PulseView
build_pulseview() {
    print_header "Building PulseView"
    
    cd "$WORK_DIR/pulseview/build"
    
    # Build with multiple cores
    local cores=$(nproc 2>/dev/null || echo 4)
    print_status "Building with $cores parallel jobs..."
    
    make -j"$cores"
    
    print_success "PulseView built successfully"
}

# Create application bundle (macOS)
create_app_bundle() {
    if [[ "$OSTYPE" != "darwin"* ]]; then
        return
    fi
    
    print_header "Creating PulseView.app Bundle"
    
    cd "$WORK_DIR/pulseview/build"
    
    # Create app bundle structure
    local app_name="PulseView-Jumperless.app"
    local app_dir="$app_name/Contents"
    
    mkdir -p "$app_dir/MacOS"
    mkdir -p "$app_dir/Frameworks"
    mkdir -p "$app_dir/Resources"
    
    # Copy executable
    cp pulseview "$app_dir/MacOS/"
    
    # Copy Info.plist
    cat > "$app_dir/Info.plist" << EOF
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
    print_status "Location: $WORK_DIR/pulseview/build/$app_name"
}

# Install PulseView
install_pulseview() {
    print_header "Installing PulseView"
    
    cd "$WORK_DIR/pulseview/build"
    
    if [[ "$OSTYPE" == "darwin"* ]] && [ -d "PulseView-Jumperless.app" ]; then
        print_status "Installing PulseView-Jumperless.app to /Applications..."
        sudo cp -R "PulseView-Jumperless.app" "/Applications/"
        print_success "PulseView-Jumperless.app installed to Applications"
    else
        print_status "Installing PulseView binary..."
        sudo make install
        print_success "PulseView installed to $INSTALL_PREFIX"
    fi
}

# Test the installation
test_installation() {
    print_header "Testing Installation"
    
    if [[ "$OSTYPE" == "darwin"* ]] && [ -d "/Applications/PulseView-Jumperless.app" ]; then
        print_status "Testing PulseView-Jumperless.app..."
        open -a PulseView-Jumperless &
        sleep 3
        if pgrep -f "PulseView-Jumperless" > /dev/null; then
            print_success "✓ PulseView-Jumperless.app launched successfully"
        else
            print_warning "? PulseView may have issues - check manually"
        fi
    else
        print_status "Testing pulseview binary..."
        if command -v pulseview &> /dev/null; then
            print_success "✓ PulseView binary available"
        else
            print_warning "? PulseView binary not found in PATH"
        fi
    fi
}

# Usage information
usage() {
    echo "Build PulseView from Source with Jumperless Driver"
    echo ""
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  deps       - Install build dependencies"
    echo "  build      - Full build process"
    echo "  install    - Install after building"
    echo "  clean      - Clean build directory"
    echo "  test       - Test installation"
    echo "  help       - Show this help"
    echo ""
    echo "Examples:"
    echo "  $0 deps      # Install dependencies first"
    echo "  $0 build     # Complete build process"
    echo "  $0 install   # Install the built app"
}

# Install dependencies
install_deps() {
    print_header "Installing Build Dependencies"
    
    if [[ "$OSTYPE" == "darwin"* ]]; then
        print_status "Installing macOS dependencies..."
        brew install cmake pkg-config qt@5 boost autoconf automake libtool
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
        check_dependencies
        setup_build_env
        clone_pulseview
        build_custom_libsigrok
        build_compatible_libsigrokdecode
        configure_pulseview
        build_pulseview
        create_app_bundle
        print_success "Build completed! Run '$0 install' to install."
        ;;
    install)
        install_pulseview
        test_installation
        print_success "Installation completed!"
        ;;
    clean)
        print_status "Cleaning build directory..."
        rm -rf "$WORK_DIR"
        print_success "Build directory cleaned"
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