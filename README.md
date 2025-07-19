# LibSigrok-FALAJ: Bus Pirate + Jumperless FALA Support

This is a fork of [libsigrok](https://sigrok.org/) with enhanced FALA (Follow Along Logic Analyzer) support for both **Bus Pirate V5+** and **Jumperless V5** devices.

## 🚀 What's Included

### Hardware Drivers
- **Bus Pirate V5+ FALA** (`bp5-binmode-fala`) - Original Bus Pirate FALA support
- **Jumperless V5 FALA** (`jumperless-fala`) - Enhanced mixed-signal FALA support
- **All Standard LibSigrok Drivers** - Complete compatibility with existing devices

### PulseView Application
- **Custom PulseView Build** with both FALA drivers integrated
- **macOS App Bundle** ready for distribution
- **Enhanced Protocol Support** for mixed-signal analysis

## 📁 Repository Structure

```
libsigrok-falaj/
├── src/hardware/
│   ├── bp5-binmode-fala/          # Bus Pirate V5+ FALA driver
│   └── jumperless-fala/           # Jumperless V5 FALA driver
├── pulseview-bpandj/              # PulseView build system
│   ├── build_pulseview_BPandJ.sh  # Build script
│   ├── PulseView-BPandJ.app/      # macOS application bundle
│   └── PULSEVIEW_BPANDJ_README.md # Usage documentation
├── JUMPERLESS_FALA_README.md      # Driver implementation details
└── README.md                      # This file
```

## 🛠 Building PulseView-BPandJ

### Prerequisites (macOS)
```bash
# Install dependencies
brew install qt@5 glib libusb libftdi cmake pkg-config autoconf automake libtool
```

### Build Instructions
```bash
# Clone this repository
git clone https://github.com/Architeuthis-Flux/libsigrok-falaj.git
cd libsigrok-falaj

# Run the build script
cd pulseview-bpandj
export PATH="/opt/homebrew/opt/qt@5/bin:$PATH"
export PKG_CONFIG_PATH="/opt/homebrew/opt/qt@5/lib/pkgconfig:$PKG_CONFIG_PATH"

# Install dependencies (first time only)
./build_pulseview_BPandJ.sh deps

# Build everything
./build_pulseview_BPandJ.sh build

# Install and create app bundle
./build_pulseview_BPandJ.sh install
```

### Launch the Application
```bash
# Launch from command line
open PulseView-BPandJ.app

# Or double-click in Finder
```

## 🔧 Using the FALA Drivers

### Bus Pirate V5+ FALA
1. Connect Bus Pirate via USB
2. In PulseView: **Connect** → **Driver: bp5-binmode-fala**
3. Configure serial port (usually `/dev/cu.usbmodem*`)
4. Start acquisition for follow-along mode

### Jumperless V5 FALA
1. Connect Jumperless via USB
2. In PulseView: **Connect** → **Driver: jumperless-fala**
3. Configure serial port
4. Supports mixed-signal mode (logic + analog channels)

## 📊 Protocol Details

### Bus Pirate FALA Protocol
- **Header**: `$FALADATA;logic_ch;trigger_ch_mask;trigger_mask;edge;rate;count;pre_trigger;`
- **Channels**: Up to 8 logic channels
- **Sample Format**: Pure logic data stream

### Jumperless FALA Protocol
- **Header**: `$JFALADATA;logic_ch;analog_ch;trigger_ch_mask;trigger_mask;edge;rate;count;pre_trigger;`
- **Logic Channels**: Up to 32 channels (A0-A7, B0-B7, C0-C7, D0-D7)
- **Analog Channels**: Up to 4 channels (ADC0-ADC3)
- **Sample Format**: Interleaved logic + analog data with 32-bit float precision

## 🔗 Integration with Jumperless Firmware

To enable FALA mode in your Jumperless V5 firmware:

1. **Implement Protocol Handler**
   ```c
   // Example: Handle $JFALADATA commands
   void handle_jfala_command(const char* header) {
       // Parse mixed-signal configuration
       // Configure logic and analog channels
       // Start streaming in FALA format
   }
   ```

2. **Stream Data Format**
   ```
   For each sample:
   - Logic data: 4 bytes (32 channels as bitmask)
   - Analog data: 16 bytes (4 channels × 4 bytes each, IEEE 754 float)
   ```

3. **Real-time Streaming**
   - Send samples continuously over serial
   - Use hardware buffering for smooth operation
   - Handle flow control and backpressure

## 📚 Documentation

- **[PulseView Usage Guide](pulseview-bpandj/PULSEVIEW_BPANDJ_README.md)** - How to use the application
- **[Jumperless FALA Implementation](JUMPERLESS_FALA_README.md)** - Driver technical details
- **[Bus Pirate FALA Docs](https://docs.buspirate.com/docs/logic-analyzer/pulseview-fala/)** - Original FALA documentation

## 🤝 Contributing

This fork maintains compatibility with the upstream libsigrok project while adding enhanced FALA support. When contributing:

1. Keep Bus Pirate compatibility intact
2. Document any protocol extensions
3. Test with both drivers before submitting
4. Follow libsigrok coding standards

## 📄 License

This project inherits the libsigrok license (GPL v3+). The Jumperless FALA driver is licensed under the same terms.

## 🎯 Status

- ✅ **LibSigrok with FALA drivers** - Complete and tested
- ✅ **PulseView-BPandJ application** - Built and working  
- ✅ **Bus Pirate FALA support** - Fully functional
- 🔄 **Jumperless FALA support** - Driver ready, needs firmware implementation
- ✅ **macOS app bundle** - Ready for distribution

---

**Ready to enhance your logic analysis workflow with FALA support!** 🎉 