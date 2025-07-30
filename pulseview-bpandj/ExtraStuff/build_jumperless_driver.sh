#!/bin/bash

# Jumperless LibSigrok Driver Build and Development Script
# Author: Kevin Santo Cappuccio
# This script handles building, testing, and development workflow for the Jumperless libsigrok driver

set -e  # Exit on any error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIBSIGROK_DIR="${SCRIPT_DIR}/examples/libsigrok-master"
BUILD_DIR="${LIBSIGROK_DIR}/build"
INSTALL_PREFIX="/usr/local"
JUMPERLESS_DRIVER_DIR="${LIBSIGROK_DIR}/src/hardware/jumperless"

# Print colored output
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

# Check if required tools are installed
check_dependencies() {
    print_header "Checking Dependencies"
    
    local missing_deps=()
    
    for cmd in git autoconf automake libtool pkg-config make; do
        if ! command -v "$cmd" &> /dev/null; then
            missing_deps+=("$cmd")
        fi
    done
    
    # Check for compiler (gcc or clang)
    if ! command -v gcc &> /dev/null && ! command -v clang &> /dev/null; then
        missing_deps+=("gcc or clang")
    fi
    
    # Check for libsigrok dependencies
    for lib in glib-2.0 libserialport libusb-1.0; do
        if ! pkg-config --exists "$lib" 2>/dev/null; then
            missing_deps+=("$lib")
        fi
    done
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        print_error "Missing dependencies: ${missing_deps[*]}"
        
        # Detect platform and show appropriate instructions
        if [[ "$OSTYPE" == "darwin"* ]]; then
            print_status "On macOS, install with Homebrew:"
            echo "    brew install autoconf automake libtool pkg-config"
            echo "    brew install glib libserialport libusb libftdi hidapi"
        elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
            print_status "On Ubuntu/Debian, install with:"
            echo "    sudo apt-get install build-essential autoconf automake libtool pkg-config"
            echo "    sudo apt-get install libglib2.0-dev libserialport-dev libusb-1.0-0-dev"
            echo "    sudo apt-get install libftdi1-dev libhidapi-dev"
            print_status "On Fedora/RHEL, install with:"
            echo "    sudo dnf install gcc autoconf automake libtool pkgconfig"
            echo "    sudo dnf install glib2-devel libserialport-devel libusb1-devel"
            echo "    sudo dnf install libftdi-devel hidapi-devel"
        fi
        exit 1
    else
        print_success "All dependencies are available"
    fi
}

# Display usage information
usage() {
    echo "Jumperless LibSigrok Driver Build Script"
    echo ""
    echo "Usage: $0 [command] [options]"
    echo ""
    echo "Commands:"
    echo "  build          - Clean build of libsigrok with Jumperless driver"
    echo "  quick-build    - Incremental build (faster)"
    echo "  configure      - Just run configure step"
    echo "  clean          - Clean build directory"
    echo "  install        - Install to system (requires sudo)"
    echo "  test           - Run basic tests"
    echo "  dev-edit       - Open driver source files in editor"
    echo "  check-patches  - Verify patches are applied"
    echo "  status         - Show build status and configuration"
    echo "  pulseview-app  - Setup PulseView.app to use custom libsigrok (macOS)"
    echo "  help           - Show this help"
    echo ""
    echo "Options:"
    echo "  --prefix PATH  - Set install prefix (default: $INSTALL_PREFIX)"
    echo "  --debug        - Enable debug build"
    echo "  --verbose      - Verbose output"
    echo ""
    echo "Examples:"
    echo "  $0 build                    # Clean build with default settings"
    echo "  $0 build --debug --verbose  # Debug build with verbose output"
    echo "  $0 quick-build             # Fast incremental build"
    echo "  $0 install --prefix=/opt   # Install to /opt instead of /usr/local"
}

# Configure libsigrok with Jumperless driver enabled
configure_libsigrok() {
    print_header "Configuring LibSigrok"
    
    cd "$LIBSIGROK_DIR"
    
    # Generate configure script if needed
    if [ ! -f "configure" ] || [ "configure.ac" -nt "configure" ]; then
        print_status "Generating configure script..."
        if [ -f "autogen.sh" ]; then
            ./autogen.sh
        else
            autoreconf -fiv
        fi
    fi
    
    # Create build directory
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Configure options
    local configure_opts=(
        "--prefix=$INSTALL_PREFIX"
        "--enable-jumperless-mixed-signal"
        "--disable-all-drivers"
        "--enable-demo"  # Keep demo driver for testing
        "--enable-jumperless-mixed-signal"
        "--enable-bindings"  # Enable language bindings (C++)
        "--enable-cxx"       # Explicitly enable C++ bindings
    )
    
    # macOS specific options
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # Use Homebrew paths (prioritize Apple Silicon paths first)
        configure_opts+=(
            "PKG_CONFIG_PATH=/opt/homebrew/lib/pkgconfig:/usr/local/lib/pkgconfig"
            "CPPFLAGS=-I/opt/homebrew/include -I/usr/local/include"
            "LDFLAGS=-L/opt/homebrew/lib -L/usr/local/lib"
        )
    fi
    
    if [ "$DEBUG_BUILD" = "yes" ]; then
        configure_opts+=(
            "--enable-debug"
            "CFLAGS=-g -O0"
        )
        print_status "Debug build enabled"
    fi
    
    print_status "Running configure with options: ${configure_opts[*]}"
    
    if [ "$VERBOSE" = "yes" ]; then
        ../configure "${configure_opts[@]}"
    else
        ../configure "${configure_opts[@]}" > configure.log 2>&1
        if [ $? -ne 0 ]; then
            print_error "Configure failed. Check $BUILD_DIR/configure.log"
            tail -20 configure.log
            exit 1
        fi
    fi
    
    print_success "Configuration completed"
}

# Build libsigrok
build_libsigrok() {
    print_header "Building LibSigrok"
    
    if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/Makefile" ]; then
        configure_libsigrok
    fi
    
    cd "$BUILD_DIR"
    
    # Determine number of cores for parallel build
    local cores=$(nproc 2>/dev/null || echo 4)
    print_status "Building with $cores parallel jobs..."
    
    if [ "$VERBOSE" = "yes" ]; then
        make -j"$cores"
    else
        make -j"$cores" > build.log 2>&1
        if [ $? -ne 0 ]; then
            print_error "Build failed. Last 30 lines of build log:"
            tail -30 build.log
            exit 1
        fi
    fi
    
    print_success "Build completed successfully"
}

# Quick incremental build
quick_build() {
    print_header "Quick Incremental Build"
    
    if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/Makefile" ]; then
        print_warning "No existing build found, doing full build..."
        build_libsigrok
        return
    fi
    
    cd "$BUILD_DIR"
    print_status "Running incremental build..."
    
    if [ "$VERBOSE" = "yes" ]; then
        make
    else
        make > quick_build.log 2>&1
        if [ $? -ne 0 ]; then
            print_error "Quick build failed. Build log:"
            cat quick_build.log
            exit 1
        fi
    fi
    
    print_success "Quick build completed"
}

# Clean build directory
clean_build() {
    print_header "Cleaning Build Directory"
    
    if [ -d "$BUILD_DIR" ]; then
        print_status "Removing $BUILD_DIR..."
        rm -rf "$BUILD_DIR"
        print_success "Build directory cleaned"
    else
        print_status "Build directory already clean"
    fi
}

# Install libsigrok
install_libsigrok() {
    print_header "Installing LibSigrok"
    
    if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/Makefile" ]; then
        print_error "No build found. Run 'build' first."
        exit 1
    fi
    
    cd "$BUILD_DIR"
    
    print_status "Installing to $INSTALL_PREFIX..."
    print_warning "This may require sudo privileges"
    
    if [ "$EUID" -eq 0 ]; then
        make install
    else
        sudo make install
    fi
    
    # Update library cache
    if command -v ldconfig &> /dev/null; then
        print_status "Updating library cache..."
        sudo ldconfig
    fi
    
    print_success "Installation completed"
    print_status "You may need to set PKG_CONFIG_PATH or LD_LIBRARY_PATH"
}

# Run basic tests
test_driver() {
    print_header "Testing Jumperless Driver"
    
    if [ ! -f "$BUILD_DIR/.libs/libsigrok.so" ] && [ ! -f "$BUILD_DIR/libsigrok.la" ]; then
        print_error "No build found. Run 'build' first."
        exit 1
    fi
    
    cd "$BUILD_DIR"
    
    print_status "Testing if Jumperless driver loads..."
    
    # Basic test - check if driver is compiled in
    if grep -q "jumperless" src/drivers.c 2>/dev/null; then
        print_success "Jumperless driver found in drivers list"
    else
        print_warning "Jumperless driver not found in compiled drivers"
    fi
    
    # Test using sigrok-cli if available
    if command -v sigrok-cli &> /dev/null; then
        print_status "Testing with sigrok-cli..."
        
        # List available drivers
        if sigrok-cli --list-drivers | grep -q jumperless; then
            print_success "Jumperless driver is available in sigrok-cli"
        else
            print_warning "Jumperless driver not listed in sigrok-cli"
        fi
    else
        print_status "sigrok-cli not available for testing"
    fi
    
    print_success "Basic tests completed"
}

# Open driver files in editor
dev_edit() {
    print_header "Opening Driver Files for Editing"
    
    local editor="${EDITOR:-nano}"
    local files=(
        "$JUMPERLESS_DRIVER_DIR/api.c"
        "$JUMPERLESS_DRIVER_DIR/protocol.h"
        "$JUMPERLESS_DRIVER_DIR/protocol.c"
    )
    
    print_status "Opening files in $editor..."
    
    for file in "${files[@]}"; do
        if [ -f "$file" ]; then
            print_status "Opening $file"
        else
            print_error "File not found: $file"
        fi
    done
    
    "$editor" "${files[@]}"
}

# Check if patches are applied
check_patches() {
    print_header "Checking Patch Status"
    
    # Check configure.ac
    if grep -q "SR_DRIVER.*Jumperless.*jumperless.*serial_comm" "$LIBSIGROK_DIR/configure.ac"; then
        print_success "✓ Jumperless driver added to configure.ac"
    else
        print_error "✗ Jumperless driver missing from configure.ac"
    fi
    
    # Check Makefile.am
    if grep -q "HW_JUMPERLESS" "$LIBSIGROK_DIR/Makefile.am"; then
        print_success "✓ Jumperless driver added to Makefile.am"
    else
        print_error "✗ Jumperless driver missing from Makefile.am"
    fi
    
    # Check driver files exist
    local files=("api.c" "protocol.h" "protocol.c")
    for file in "${files[@]}"; do
        if [ -f "$JUMPERLESS_DRIVER_DIR/$file" ]; then
            print_success "✓ Driver file exists: $file"
        else
            print_error "✗ Driver file missing: $file"
        fi
    done
    
    # Check driver registration
    if grep -q "SR_REGISTER_DEV_DRIVER.*jumperless" "$JUMPERLESS_DRIVER_DIR/api.c" 2>/dev/null; then
        print_success "✓ Driver properly registered"
    else
        print_warning "? Driver registration not found or incomplete"
    fi
}

# Show build status
show_status() {
    print_header "Build Status"
    
    echo "LibSigrok Directory: $LIBSIGROK_DIR"
    echo "Build Directory: $BUILD_DIR"
    echo "Install Prefix: $INSTALL_PREFIX"
    echo "Jumperless Driver: $JUMPERLESS_DRIVER_DIR"
    echo ""
    
    if [ -d "$BUILD_DIR" ]; then
        if [ -f "$BUILD_DIR/Makefile" ]; then
            print_success "✓ Configured"
        else
            print_warning "? Build directory exists but not configured"
        fi
        
        if [ -f "$BUILD_DIR/.libs/libsigrok.so" ] || [ -f "$BUILD_DIR/libsigrok.la" ]; then
            print_success "✓ Built"
        else
            print_warning "? Not built yet"
        fi
    else
        print_warning "? Not configured"
    fi
    
    echo ""
    check_patches
}

# Setup PulseView.app to use custom libsigrok (macOS)
setup_pulseview_app() {
    print_header "Setting Up PulseView.app for Custom LibSigrok"
    
    if [[ "$OSTYPE" != "darwin"* ]]; then
        print_error "This command is only for macOS"
        exit 1
    fi
    
    # Find PulseView.app
    local pulseview_app=""
    for app_path in "/Applications/PulseView.app" "$HOME/Applications/PulseView.app"; do
        if [ -d "$app_path" ]; then
            pulseview_app="$app_path"
            break
        fi
    done
    
    if [ -z "$pulseview_app" ]; then
        print_error "PulseView.app not found in /Applications or ~/Applications"
        print_status "Please install PulseView.app first or specify the correct path"
        exit 1
    fi
    
    print_success "Found PulseView.app at: $pulseview_app"
    
    # Check if custom libsigrok is built
    if [ ! -f "$BUILD_DIR/.libs/libsigrok.dylib" ] && [ ! -f "$INSTALL_PREFIX/lib/libsigrok.dylib" ]; then
        print_error "Custom libsigrok not found. Run 'build' and 'install' first."
        exit 1
    fi
    
    # Create backup of original libraries
    local frameworks_dir="$pulseview_app/Contents/Frameworks"
    local main_lib="$frameworks_dir/libsigrok.4.dylib"
    local cxx_lib="$frameworks_dir/libsigrokcxx.4.dylib"
    
    # Backup main library
    if [ -f "$main_lib" ] && [ ! -f "$main_lib.backup" ]; then
        print_status "Creating backup of original libsigrok.4.dylib..."
        cp "$main_lib" "$main_lib.backup"
    fi
    
    # Backup C++ library  
    if [ -f "$cxx_lib" ] && [ ! -f "$cxx_lib.backup" ]; then
        print_status "Creating backup of original libsigrokcxx.4.dylib..."
        cp "$cxx_lib" "$cxx_lib.backup"
    fi
    
    # Copy our custom libsigrok libraries
    local custom_lib=""
    local custom_cxx_lib=""
    
    if [ -f "$INSTALL_PREFIX/lib/libsigrok.dylib" ]; then
        custom_lib="$INSTALL_PREFIX/lib/libsigrok.dylib"
        custom_cxx_lib="$INSTALL_PREFIX/lib/libsigrokcxx.dylib"
    elif [ -f "$BUILD_DIR/.libs/libsigrok.dylib" ]; then
        custom_lib="$BUILD_DIR/.libs/libsigrok.dylib"
        custom_cxx_lib="$BUILD_DIR/.libs/libsigrokcxx.dylib"
    fi
    
    if [ -n "$custom_lib" ]; then
        print_status "Installing custom libsigrok into PulseView.app..."
        
        # Replace the libraries that PulseView actually uses
        cp "$custom_lib" "$main_lib"
        if [ -f "$custom_cxx_lib" ]; then
            cp "$custom_cxx_lib" "$cxx_lib"
            print_status "Updated libsigrokcxx.4.dylib"
        fi
        
        # Fix library paths if needed
        install_name_tool -id "@rpath/libsigrok.4.dylib" "$main_lib" 2>/dev/null || true
        if [ -f "$cxx_lib" ]; then
            install_name_tool -id "@rpath/libsigrokcxx.4.dylib" "$cxx_lib" 2>/dev/null || true
        fi
        
        print_success "PulseView.app updated with Jumperless driver!"
        print_status "Launch PulseView.app and look for 'Jumperless' in the device list"
    else
        print_error "Custom libsigrok library not found"
        exit 1
    fi
    
    # Show verification
    print_status "Verifying installation..."
    if strings "$main_lib" | grep -q "jumperless"; then
        print_success "✓ Jumperless driver found in PulseView.app library"
    else
        print_warning "? Jumperless driver not detected in library"
    fi
}

# Parse command line arguments
COMMAND=""
DEBUG_BUILD="no"
VERBOSE="no"

while [[ $# -gt 0 ]]; do
    case $1 in
        build|quick-build|configure|clean|install|test|dev-edit|check-patches|status|pulseview-app|help)
            COMMAND="$1"
            shift
            ;;
        --prefix)
            INSTALL_PREFIX="$2"
            shift 2
            ;;
        --debug)
            DEBUG_BUILD="yes"
            shift
            ;;
        --verbose)
            VERBOSE="yes"
            shift
            ;;
        *)
            print_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# If no command specified, show usage
if [ -z "$COMMAND" ]; then
    usage
    exit 0
fi

# Check if we're in the right directory
if [ ! -d "$LIBSIGROK_DIR" ]; then
    print_error "LibSigrok directory not found: $LIBSIGROK_DIR"
    print_status "Make sure you're running this script from the RP23V50firmware directory"
    exit 1
fi

# Execute the requested command
case $COMMAND in
    build)
        check_dependencies
        clean_build
        configure_libsigrok
        build_libsigrok
        print_success "Build completed! You can now run 'test' or 'install'"
        ;;
    quick-build)
        quick_build
        print_success "Quick build completed!"
        ;;
    configure)
        check_dependencies
        configure_libsigrok
        print_success "Configuration completed!"
        ;;
    clean)
        clean_build
        ;;
    install)
        install_libsigrok
        print_success "Installation completed!"
        ;;
    test)
        test_driver
        ;;
    dev-edit)
        dev_edit
        ;;
    check-patches)
        check_patches
        ;;
    status)
        show_status
        ;;
    pulseview-app)
        setup_pulseview_app
        ;;
    help)
        usage
        ;;
esac 