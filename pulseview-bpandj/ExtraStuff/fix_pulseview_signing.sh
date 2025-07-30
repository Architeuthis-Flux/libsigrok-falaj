#!/bin/bash

# Fix PulseView-Jumperless.app Code Signing Issues
# This script properly signs the custom PulseView app to avoid macOS crashes

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

APP_PATH="/Applications/PulseView-Jumperless.app"

# Check if app exists
if [ ! -d "$APP_PATH" ]; then
    print_error "PulseView-Jumperless.app not found. Run the build script first."
    exit 1
fi

print_header "Fixing Code Signing Issues"

# Method 1: Remove existing signatures and re-sign
fix_signing_method1() {
    print_status "Method 1: Re-signing the app bundle"
    
    # Remove any existing signatures
    sudo codesign --remove-signature "$APP_PATH" 2>/dev/null || true
    
    # Sign all frameworks first
    if [ -d "$APP_PATH/Contents/Frameworks" ]; then
        for framework in "$APP_PATH/Contents/Frameworks"/*.dylib "$APP_PATH/Contents/Frameworks"/*.framework; do
            if [ -e "$framework" ]; then
                print_status "Signing $(basename "$framework")"
                sudo codesign --force --sign - "$framework" 2>/dev/null || true
            fi
        done
    fi
    
    # Sign the main executable
    if [ -f "$APP_PATH/Contents/MacOS/pulseview" ]; then
        print_status "Signing main executable"
        sudo codesign --force --sign - "$APP_PATH/Contents/MacOS/pulseview"
    fi
    
    # Sign the entire app bundle
    print_status "Signing app bundle"
    sudo codesign --force --sign - --deep "$APP_PATH"
    
    # Verify
    if codesign --verify "$APP_PATH" 2>/dev/null; then
        print_success "✓ App successfully signed"
        return 0
    else
        print_error "✗ Signing failed"
        return 1
    fi
}

# Method 2: Create a command-line launcher
create_command_launcher() {
    print_status "Method 2: Creating command-line launcher"
    
    # Create a simple launcher script
    sudo tee /usr/local/bin/pulseview-jumperless > /dev/null << 'EOF'
#!/bin/bash
export DYLD_LIBRARY_PATH="/usr/local/lib:$DYLD_LIBRARY_PATH"
export QT_PLUGIN_PATH="/opt/homebrew/opt/qt@5/plugins:$QT_PLUGIN_PATH"
exec "/Applications/PulseView-Jumperless.app/Contents/MacOS/pulseview" "$@"
EOF
    
    sudo chmod +x /usr/local/bin/pulseview-jumperless
    
    print_success "✓ Command launcher created: pulseview-jumperless"
    print_status "You can now run: pulseview-jumperless"
}

# Method 3: Copy to unsigned location
create_local_copy() {
    print_status "Method 3: Creating local unsigned copy"
    
    LOCAL_PATH="$HOME/Applications/PulseView-Jumperless.app"
    
    # Create local Applications directory
    mkdir -p "$HOME/Applications"
    
    # Copy app to user's Applications (no admin signature required)
    cp -R "$APP_PATH" "$LOCAL_PATH"
    
    # Remove signatures (unsigned apps can run with user permission)
    codesign --remove-signature "$LOCAL_PATH" 2>/dev/null || true
    
    print_success "✓ Unsigned copy created at: $LOCAL_PATH"
    print_status "This version will ask for permission but should run."
}

# Test the app
test_app() {
    local app_path="$1"
    print_status "Testing app: $(basename "$app_path")"
    
    # Try to launch and check if it stays running
    open "$app_path" &
    local pid=$!
    sleep 3
    
    if pgrep -f "PulseView-Jumperless" > /dev/null; then
        print_success "✓ App launched successfully!"
        return 0
    else
        print_error "✗ App failed to launch or crashed"
        return 1
    fi
}

# Show current status
print_header "Current App Status"
codesign --verify --verbose "$APP_PATH" 2>&1 || echo "No valid signature"

# Try Method 1: Re-signing
print_header "Attempting Fix Method 1: Re-signing"
if fix_signing_method1; then
    print_success "Re-signing successful! Testing..."
    if test_app "$APP_PATH"; then
        print_success "🎉 PulseView-Jumperless.app is now working!"
        exit 0
    fi
fi

# Try Method 2: Command launcher
print_header "Attempting Fix Method 2: Command Launcher"
create_command_launcher

# Try Method 3: Local unsigned copy
print_header "Attempting Fix Method 3: Local Unsigned Copy"
create_local_copy

LOCAL_APP="$HOME/Applications/PulseView-Jumperless.app"
if test_app "$LOCAL_APP"; then
    print_success "🎉 Local unsigned copy is working!"
else
    print_status "Testing command launcher..."
    export DYLD_LIBRARY_PATH="/usr/local/lib:$DYLD_LIBRARY_PATH"
    timeout 5s pulseview-jumperless --help &>/dev/null && {
        print_success "✓ Command launcher works!"
        print_success "🎉 Run 'pulseview-jumperless' from terminal"
    } || {
        print_error "Command launcher test failed"
    }
fi

# Final instructions
print_header "Usage Instructions"
echo -e "${CYAN}Option 1:${NC} Command Line"
echo "  Run: pulseview-jumperless"
echo ""
echo -e "${CYAN}Option 2:${NC} Local App Bundle"
echo "  Open: $HOME/Applications/PulseView-Jumperless.app"
echo "  (May require security permission)"
echo ""
echo -e "${CYAN}Option 3:${NC} Direct Execution"
echo "  export DYLD_LIBRARY_PATH=/usr/local/lib:\$DYLD_LIBRARY_PATH"
echo "  $APP_PATH/Contents/MacOS/pulseview"
echo ""
print_success "The Jumperless driver is ready in all versions!" 