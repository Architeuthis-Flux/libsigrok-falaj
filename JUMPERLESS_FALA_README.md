# Jumperless FALA (Follow Along Logic Analyzer) Implementation

## Overview

This implementation adds Jumperless support to the Bus Pirate FALA fork of libsigrok, enabling the Jumperless V5 to function as a "follow along" logic analyzer in PulseView. The implementation is based on the Bus Pirate V5+ FALA driver but has been enhanced to support Jumperless-specific features.

## Key Features

1. **Logic Analyzer Mode**: Up to 32 digital channels (A0-A7, B0-B7, C0-C7, D0-D7)
2. **Mixed Signal Support**: Combined logic + analog channels (ADC0-ADC3)
3. **Real-time Streaming**: Follow along mode for continuous monitoring
4. **Bus Pirate Compatibility**: Maintains compatibility with existing FALA protocol
5. **Jumperless Extensions**: Enhanced protocol for mixed-signal capabilities

## Protocol Differences from Bus Pirate

### Header Format

**Bus Pirate FALA Header:**
```
$FALADATA;logic_channels;trigger_ch_mask;trigger_mask;edge;rate;count;pre_trigger;
```

**Jumperless FALA Header:**
```
$JFALADATA;logic_channels;analog_channels;trigger_ch_mask;trigger_mask;edge;rate;count;pre_trigger;
```

### Key Differences

1. **Header Identifier**: `$JFALADATA` instead of `$FALADATA`
2. **Analog Channel Support**: Additional field for number of analog channels
3. **Mixed Signal Data**: Interleaved logic and analog sample data
4. **Query Command**: Uses `?fala` instead of `?` for device identification

## Channel Configuration

### Logic Channels (Digital)
- **A0-A7**: GPIO pins on connector A (channels 0-7)
- **B0-B7**: GPIO pins on connector B (channels 8-15)  
- **C0-C7**: GPIO pins on connector C (channels 16-23)
- **D0-D7**: GPIO pins on connector D (channels 24-31)

### Analog Channels (Mixed Signal Mode)
- **ADC0-ADC3**: Analog-to-digital converter channels
- **Sample Format**: 32-bit float voltage values
- **Range**: Configurable based on Jumperless hardware capabilities

## Data Format

### Logic-Only Mode
- **Sample Size**: 1 byte per sample
- **Format**: Standard SUMP-compatible digital samples
- **Compatible**: With existing Bus Pirate FALA clients

### Mixed Signal Mode  
- **Sample Size**: 1 byte (logic) + (N × 4 bytes) (analog channels)
- **Logic Data**: Single byte containing digital channel states
- **Analog Data**: IEEE 754 32-bit float values for each analog channel
- **Interleaved**: Logic and analog data sent together per sample

## Implementation Details

### Driver Components

1. **`protocol.h`**: Header definitions and data structures
   - Extended `dev_context` with mixed signal support
   - Enhanced `fala_header` structure with analog channel info

2. **`api.c`**: Device detection and configuration
   - Modified scan procedure for Jumperless identification
   - Enhanced channel creation for logic + analog
   - Updated device information strings

3. **`protocol.c`**: Data reception and processing
   - Support for mixed signal data streams
   - Separate logic and analog data packet generation
   - Maintains backward compatibility with logic-only mode

### Build System Integration

- **`configure.ac`**: Added `SR_DRIVER([Jumperless FALA], [jumperless-fala], [serial_comm])`
- **`Makefile.am`**: Added conditional build rules for `HW_JUMPERLESS_FALA`

## Usage Examples

### PulseView
```bash
# Scan for Jumperless FALA devices
pulseview -d jumperless-fala:conn=/dev/ttyACM0

# Manual connection string
pulseview -d jumperless-fala:conn=/dev/cu.usbmodemJLV5port5
```

### sigrok-cli
```bash
# Device scan
sigrok-cli -d jumperless-fala:conn=/dev/ttyACM0 --scan

# Capture logic data
sigrok-cli -d jumperless-fala:conn=/dev/ttyACM0 -o capture.sr --time 10s

# Mixed signal capture (when supported by firmware)
sigrok-cli -d jumperless-fala:conn=/dev/ttyACM0 -o mixed.sr --samples 1000
```

## Firmware Requirements

The Jumperless firmware must implement the FALA protocol to work with this driver:

1. **Identification Response**: Respond to `?fala` with proper header
2. **Data Streaming**: Send continuous sample data after receiving `+`
3. **Protocol Compliance**: Follow the Jumperless FALA header format
4. **Mixed Signal** (Optional): Support for analog channel data

## Differences from Standard SUMP Protocol

While maintaining SUMP compatibility, the FALA protocol adds:

1. **Streaming Mode**: Continuous data transmission vs. triggered capture
2. **Header Exchange**: Metadata negotiation before data transfer
3. **Mixed Signal**: Support for both logic and analog data
4. **Real-time**: Optimized for continuous monitoring vs. single captures

## Future Enhancements

1. **Trigger Support**: Hardware triggering capabilities
2. **Variable Sample Rates**: Runtime sample rate configuration
3. **Channel Selection**: Enable/disable specific channels
4. **Compression**: Data compression for higher throughput
5. **Buffer Management**: Configurable capture buffer sizes

## Compatibility Notes

- **Bus Pirate Compatible**: Logic-only mode works with Bus Pirate FALA firmware
- **PulseView Ready**: Full integration with PulseView's mixed signal view
- **Cross Platform**: Works on Linux, macOS, and Windows
- **Serial Interface**: Standard USB CDC ACM communication

## Installation

1. **Build libsigrok**: Configure with `--enable-jumperless-fala`
2. **Install Rules**: Copy udev rules for device permissions (Linux)
3. **Firmware Update**: Ensure Jumperless has FALA-compatible firmware
4. **PulseView**: Use updated PulseView with mixed signal support

This implementation provides a solid foundation for Jumperless logic analyzer functionality while maintaining compatibility with the broader FALA ecosystem.
