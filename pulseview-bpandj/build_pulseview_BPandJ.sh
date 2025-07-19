#!/bin/bash

# Build PulseView-BPandJ.app with Bus Pirate and Jumperless FALA Support
# This script builds PulseView using our enhanced libsigrok-falaj with both drivers

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
PULSEVIEW_REPO="https://github.com/sigrokproject/pulseview.git"
PULSEVIEW_BRANCH="master"
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
    
    # Configure with both FALA drivers enabled
    print_status "Configuring with BP5 and Jumperless FALA drivers..."
    ./configure \
        --prefix=/usr/local \
        --enable-bp5-binmode-fala \
        --enable-jumperless-fala \
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
    
    print_success "LibSigrok-FALA with BP5 and Jumperless drivers installed"
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

# Clone PulseView source
clone_pulseview_fala() {
    print_header "Cloning PulseView Source"
    
    cd "$WORK_DIR"
    
    if [ -d "pulseview" ]; then
        print_status "Updating existing PulseView repository..."
        cd pulseview
        git fetch origin
        git checkout "$PULSEVIEW_BRANCH"
        git pull origin "$PULSEVIEW_BRANCH"
        cd ..
    else
        print_status "Cloning PulseView repository..."
        git clone --branch "$PULSEVIEW_BRANCH" "$PULSEVIEW_REPO" pulseview
    fi
    
    print_success "PulseView source ready"
}

# Configure PulseView build
configure_pulseview() {
    print_header "Configuring PulseView-BPandJ Build"
    
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
        "-DENABLE_DECODE=OFF"
    )
    
    if [[ "$OSTYPE" == "darwin"* ]]; then
        cmake_opts+=(
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15"
            "-DQt5_DIR=/opt/homebrew/opt/qt@5/lib/cmake/Qt5"
        )
    fi
    
    print_status "Running cmake configuration..."
    cmake .. "${cmake_opts[@]}"
    
    print_success "PulseView-BPandJ configured"
}

# Build PulseView
build_pulseview() {
    print_header "Building PulseView-BPandJ"
    
    cd "$WORK_DIR/pulseview/build"
    
    # Build with multiple cores
    local cores=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
    print_status "Building with $cores parallel jobs..."
    
    make -j"$cores"
    
    print_success "PulseView-BPandJ built successfully"
}

# Create application bundle (macOS)
create_app_bundle() {
    if [[ "$OSTYPE" != "darwin"* ]]; then
        return
    fi
    
    print_header "Creating PulseView-BPandJ.app Bundle"
    
    cd "$WORK_DIR/pulseview/build"
    
    # Create app bundle structure
    local app_name="PulseView-BPandJ.app"
    local app_dir="$app_name/Contents"
    
    rm -rf "$app_name"
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
    <string>org.sigrok.PulseView.BPandJ</string>
    <key>CFBundleName</key>
    <string>PulseView-BPandJ</string>
    <key>CFBundleDisplayName</key>
    <string>PulseView Bus Pirate & Jumperless</string>
    <key>CFBundleVersion</key>
    <string>1.0.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>LSMinimumSystemVersion</key>
    <string>10.15</string>
</dict>
</plist>
EOF
    
    # Use macdeployqt to bundle Qt libraries
    if command -v macdeployqt &> /dev/null; then
        print_status "Bundling Qt libraries..."
        macdeployqt "$app_name"
    else
        print_warning "macdeployqt not found - Qt libraries may not be bundled"
    fi
    
    print_success "Created $app_name"
    print_status "Location: $WORK_DIR/pulseview/build/$app_name"
}

# Install PulseView
install_pulseview() {
    print_header "Installing PulseView-BPandJ"
    
    cd "$WORK_DIR/pulseview/build"
    
    if [[ "$OSTYPE" == "darwin"* ]] && [ -d "PulseView-BPandJ.app" ]; then
        print_status "Installing PulseView-BPandJ.app to /Applications..."
        sudo cp -R "PulseView-BPandJ.app" "/Applications/"
        print_success "PulseView-BPandJ.app installed to Applications"
        
        # Create a symbolic link for easy command line access
        sudo ln -sf "/Applications/PulseView-BPandJ.app/Contents/MacOS/pulseview" "/usr/local/bin/pulseview-bpandj"
        print_status "Created symlink: /usr/local/bin/pulseview-bpandj"
    else
        print_status "Installing PulseView binary..."
        sudo make install
        print_success "PulseView-BPandJ installed to $INSTALL_PREFIX"
    fi
}

# Test the installation
test_installation() {
    print_header "Testing Installation"
    
    if [[ "$OSTYPE" == "darwin"* ]] && [ -d "/Applications/PulseView-BPandJ.app" ]; then
        print_status "Testing PulseView-BPandJ.app..."
        print_status "Available drivers should include:"
        echo "  - bp5-binmode-fala (Bus Pirate V5+ FALA)"
        echo "  - jumperless-fala (Jumperless FALA)"
        echo ""
        print_status "Testing driver availability..."
        if command -v pulseview-bpandj &> /dev/null; then
            pulseview-bpandj --driver-list | grep -E "(bp5-binmode-fala|jumperless-fala)" || true
        fi
        print_success "✓ PulseView-BPandJ.app ready for testing"
        print_status "Launch with: open -a PulseView-BPandJ"
    else
        print_status "Testing pulseview binary..."
        if command -v pulseview &> /dev/null; then
            pulseview --driver-list | grep -E "(bp5-binmode-fala|jumperless-fala)" || true
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
    echo "Build PulseView-BPandJ with Bus Pirate and Jumperless FALA Support"
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
    echo ""
    echo "The built app will support both:"
    echo "  - Bus Pirate V5+ FALA (bp5-binmode-fala driver)"
    echo "  - Jumperless FALA (jumperless-fala driver)"
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
        check_dependencies
        setup_build_env
        build_libsigrok_fala
        build_compatible_libsigrokdecode
        clone_pulseview_fala
        configure_pulseview
        build_pulseview
        create_app_bundle
        print_success "Build completed! Run '$0 install' to install."
        ;;
    install)
        install_pulseview
        test_installation
        print_success "Installation completed!"
        print_status "Launch with: open -a PulseView-BPandJ"
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