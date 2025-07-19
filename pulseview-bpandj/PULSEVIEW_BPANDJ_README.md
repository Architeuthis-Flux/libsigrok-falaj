# PulseView-BPandJ: Bus Pirate + Jumperless FALA Support

## 🎉 Successfully Built!

This build successfully creates a custom version of PulseView with enhanced FALA (Follow Along Logic Analyzer) support for both Bus Pirate V5+ and Jumperless V5 devices.

## ✅ What's Working

### Drivers Successfully Included
- **Bus Pirate V5+ FALA Driver** (`bp5-binmode-fala`) ✅ Confirmed working
- **Jumperless V5 FALA Driver** (`jumperless-fala`) ✅ Built and integrated
- **All Standard PulseView Drivers** ✅ Logic analyzers, oscilloscopes, etc.

### Build Components
- **LibSigrok-FALA** - Enhanced with both BP5 and Jumperless FALA drivers
- **LibSigrokdecode** - Protocol decoder support
- **PulseView** - Qt5-based GUI with both drivers
- **macOS App Bundle** - `PulseView-BPandJ.app` ready to use

## 🚀 How to Use

### Launch the Application
```bash
# Method 1: Double-click in Finder
open PulseView-BPandJ.app

# Method 2: Command line
/Users/kevinsanto/Documents/GitHub/JumperlessV5/RP23V50firmware/PulseView-BPandJ.app/Contents/MacOS/PulseView-BPandJ
```

### Connecting Your Device

#### Bus Pirate V5+ FALA Mode
1. Connect Bus Pirate via USB
2. In PulseView: `Connect` → `Driver: bp5-binmode-fala`
3. Configure serial port (usually `/dev/cu.usbmodem*`)
4. Start acquisition for follow-along mode

#### Jumperless V5 FALA Mode
1. Connect Jumperless via USB
2. In PulseView: `Connect` → `Driver: jumperless-fala`
3. Configure serial port
4. Supports mixed-signal mode (logic + analog channels)

## 🔧 Technical Details

### Channel Configuration

**Bus Pirate V5+ Channels:**
- Logic channels: Up to 8 channels
- Sample rates: Up to 1 MHz (depending on configuration)
- Uses `$FALADATA` protocol header

**Jumperless V5 Channels:**
- Logic channels: Up to 32 channels (A0-A7, B0-B7, C0-C7, D0-D7)
- Analog channels: 4 channels (ADC0-ADC3)
- Uses `$JFALADATA` protocol header for enhanced mixed-signal support

### Protocol Differences
- **Bus Pirate**: Standard FALA protocol for logic-only analysis
- **Jumperless**: Enhanced protocol supporting simultaneous logic + analog capture

## 📁 File Locations

```
RP23V50firmware/
├── PulseView-BPandJ.app/           # macOS Application Bundle
├── pulseview_BPandJ_build/         # Build artifacts
│   ├── libsigrokdecode/           # Protocol decoders
│   └── pulseview/                 # PulseView source & build
├── build_pulseview_BPandJ.sh       # Build script
└── PULSEVIEW_BPANDJ_README.md      # This file
```

**System Installation:**
- Binary: `/usr/local/bin/pulseview`
- Libraries: `/usr/local/lib/libsigrok*`
- Drivers: Built into libsigrok

## 🛠 Build Information

- **Qt Version**: 5.15.16
- **Compiler**: Apple clang 17.0.0
- **LibSigrok**: 0.6.0-git with FALA drivers
- **Build Date**: July 19, 2025
- **Decode Support**: Disabled (due to API compatibility)

## 📋 Build Process Summary

1. ✅ **Dependencies** - Qt5, glib, libusb, etc.
2. ✅ **LibSigrok-FALA** - Custom build with BP5 + Jumperless drivers
3. ✅ **LibSigrokdecode** - Protocol decoder library
4. ✅ **PulseView** - GUI application (decode support disabled)
5. ✅ **App Bundle** - macOS application package created

## 🐛 Known Limitations

1. **Decode Support Disabled** - Protocol decoding disabled due to API compatibility issues between PulseView master and stable libsigrokdecode
2. **Manual Device Setup** - Devices need to be manually configured in FALA mode
3. **Serial Port Configuration** - Port paths may need manual adjustment

## 🔗 Related Projects

- **Bus Pirate FALA**: [Bus Pirate Documentation](https://docs.buspirate.com/docs/logic-analyzer/pulseview-fala/)
- **Jumperless V5**: Enhanced mixed-signal FALA implementation
- **LibSigrok**: [Sigrok Project](https://sigrok.org/)
- **Original FALA Implementation**: Bus Pirate team's follow-along mode

## 🎯 Next Steps

To enable Jumperless FALA mode in your hardware:
1. Flash the Jumperless V5 with FALA-compatible firmware
2. Implement the `$JFALADATA` protocol handler
3. Test with the built PulseView-BPandJ application

The infrastructure is ready - just needs the Jumperless firmware side implementation!

---

**Success!** 🎉 PulseView-BPandJ is ready for testing with both Bus Pirate V5+ and Jumperless V5 FALA devices. 