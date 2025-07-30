/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024 Jumperless Project
 * Dual-Mode Logic Analyzer Driver (SUMP + Enhanced Protocol)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSIGROK_HARDWARE_JUMPERLESS_MIXED_SIGNAL_PROTOCOL_H
#define LIBSIGROK_HARDWARE_JUMPERLESS_MIXED_SIGNAL_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

struct sr_sw_limits;

#define LOG_PREFIX "jumperless-mixed-signal"






// Buffer sizes
#define LOGIC_BUFSIZE 65536
#define MIN_NUM_SAMPLES 1
#define MAX_NUM_SAMPLES 100000

// Timeout constants
#define JUMPERLESS_DETECT_TIMEOUT   1000   // 1 second
#define JUMPERLESS_COMMAND_TIMEOUT  5000   // 5 seconds
#define JUMPERLESS_RESPONSE_TIMEOUT 5000   // 5 seconds

// Hardware limits
#define JUMPERLESS_MAX_DIGITAL_CHANNELS 16  // Updated to match firmware capability
#define JUMPERLESS_MAX_ANALOG_CHANNELS  14  // Updated to match firmware capability
#define JUMPERLESS_DEFAULT_DIGITAL_CHANNELS 8   // Default number of digital channels to use
#define JUMPERLESS_DEFAULT_ANALOG_CHANNELS 5    // Default number of analog channels to use
#define JUMPERLESS_MAX_SAMPLE_RATE      50000000  // 50 MHz
#define JUMPERLESS_MIN_SAMPLE_RATE      25      // 25 Hz

// =============================================================================
// PROTOCOL DETECTION AND MODE SELECTION
// =============================================================================

// Protocol modes
typedef enum {
    JUMPERLESS_PROTOCOL_AUTO = 0,    // Auto-detect based on device response
    JUMPERLESS_PROTOCOL_SUMP = 1,    // SUMP/OLS compatibility mode
    JUMPERLESS_PROTOCOL_ENHANCED = 2 // Enhanced Jumperless protocol
} JumperlessProtocolMode;

// Device capabilities flags
#define JUMPERLESS_CAP_DIGITAL_ONLY     0x01
#define JUMPERLESS_CAP_ANALOG_ONLY      0x02
#define JUMPERLESS_CAP_MIXED_SIGNAL     0x03

// =============================================================================
// SUMP PROTOCOL DEFINITIONS
// =============================================================================

// SUMP commands (standard OLS/SUMP protocol)
#define SUMP_CMD_RESET                  0x00
#define SUMP_CMD_RUN                    0x01
#define SUMP_CMD_ID                     0x02
#define SUMP_CMD_METADATA               0x04
#define SUMP_CMD_SET_DIVIDER            0x80
#define SUMP_CMD_SET_READ_DELAY_COUNT   0x81  // Sets both read count and delay count
#define SUMP_CMD_SET_FLAGS              0x82

// SUMP response constants
#define SUMP_ID_RESPONSE            "1SLO"
#define SUMP_CLOCK_BASE             100000000  // 100 MHz

// SUMP flags
#define SUMP_FLAG_DEMUX             (1 << 0)
#define SUMP_FLAG_FILTER            (1 << 1)
#define SUMP_FLAG_EXTERNAL_CLOCK    (1 << 6)
#define SUMP_FLAG_INVERTED_CLOCK    (1 << 7)

// =============================================================================
// ENHANCED JUMPERLESS PROTOCOL DEFINITIONS
// =============================================================================

// Enhanced protocol magic and constants
#define JUMPERLESS_MAGIC            "$JLDATA"
#define JUMPERLESS_PROTOCOL_VERSION 2
#define JUMPERLESS_DEVICE_ID        "Jumperless Logic Analyzer v2.0"

// Enhanced protocol commands (offset by 0xA0 to avoid conflicts)
#define JUMPERLESS_CMD_RESET        0xA0
#define JUMPERLESS_CMD_RUN          0xA1
#define JUMPERLESS_CMD_ID           0xA2
#define JUMPERLESS_CMD_GET_HEADER   0xA3
#define JUMPERLESS_CMD_SET_CHANNELS 0xA4
#define JUMPERLESS_CMD_ARM          0xA5
#define JUMPERLESS_CMD_GET_STATUS   0xA6
#define JUMPERLESS_CMD_CONFIGURE    0xA7
#define JUMPERLESS_CMD_SET_SAMPLES  0xA8
#define JUMPERLESS_CMD_SET_MODE     0xA9
#define JUMPERLESS_CMD_END_DATA     0xAC  // Signal end of data transmission

// Enhanced protocol responses
#define JUMPERLESS_RESP_ID          0x7F
#define JUMPERLESS_RESP_HEADER      0x80
#define JUMPERLESS_RESP_DATA        0x81
#define JUMPERLESS_RESP_STATUS      0x82
#define JUMPERLESS_RESP_ERROR       0x83
#define JUMPERLESS_RESP_END_DATA    0x84  // End of data transmission response

// Header framing characters for proper alignment
#define JUMPERLESS_HEADER_SOH       0xAF  // Start of Header
#define JUMPERLESS_HEADER_EOH       0xBF  // End of Header

// Enhanced protocol status codes
#define JUMPERLESS_STATUS_OK                0x00
#define JUMPERLESS_ERROR_INSUFFICIENT_DATA  0x01
#define JUMPERLESS_ERROR_INVALID_STATE      0x02
#define JUMPERLESS_ERROR_UNKNOWN_COMMAND    0xFF

// Enhanced protocol modes
#define JUMPERLESS_MODE_DIGITAL_ONLY  0x00
#define JUMPERLESS_MODE_MIXED_SIGNAL  0x01
#define JUMPERLESS_MODE_ANALOG_ONLY   0x02

// =============================================================================
// DEVICE CAPABILITIES HEADER
// =============================================================================

// Device capabilities header structure (107 bytes - matches firmware)
typedef struct {
    char magic[8];                      // "$JLDATA\0"
    uint8_t version;                    // Protocol version (2)
    uint8_t capture_mode;               // Current mode (0=digital, 1=mixed, 2=analog)
    uint8_t max_digital_channels;       // Maximum digital channels (16)
    uint8_t max_analog_channels;        // Maximum analog channels (14)
    uint32_t sample_rate;               // Current sample rate (Hz)
    uint32_t sample_count;              // Number of samples to capture
    uint32_t digital_channel_mask;      // Available digital channels (0xFF)
    uint32_t analog_channel_mask;       // Enabled analog channels
    uint8_t bytes_per_sample;           // Total bytes per sample
    uint8_t digital_bytes_per_sample;   // Digital bytes per sample (1)
    uint8_t analog_bytes_per_sample;    // Analog bytes per sample (N*2)
    uint8_t adc_resolution_bits;        // ADC resolution (12 bits)
    uint32_t trigger_channel_mask;      // Trigger channels (future)
    uint32_t trigger_pattern;           // Trigger pattern (future)
    uint32_t trigger_edge_mask;         // Edge trigger mask (future)
    uint32_t pre_trigger_samples;       // Pre-trigger samples (future)
    float analog_voltage_range;         // ADC voltage range (18.28V)
    uint64_t max_sample_rate;           // Maximum sample rate (50MHz)
    uint64_t max_memory_depth;          // Maximum memory depth
    uint8_t supports_triggers;          // Supports triggers (boolean)
    uint8_t supports_compression;       // Supports compression (boolean)
    uint8_t supported_modes;            // Supported modes bitmask
    char firmware_version[16];          // Firmware version string
    char device_id[16];                 // Device identifier
    uint32_t checksum;                  // Header checksum (XOR)
} __attribute__((packed)) jumperless_header;

// =============================================================================
// DEVICE CONTEXT STRUCTURE
// =============================================================================

struct dev_context {
    // === STANDARD LIBSIGROK FIELDS ===
    uint64_t cur_samplerate;
    uint64_t limit_samples;
    uint64_t limit_msec;
    uint64_t limit_frames;
    uint64_t num_samples;           // Total samples processed (matches old implementation)
    uint64_t sent_frame_samples;    // Number of samples sent for current frame
    uint64_t capture_ratio;
    uint32_t num_transfers;
    int64_t start_us;
    int64_t spent_us;
    uint64_t step;
    
    // === LOGIC ANALYZER FIELDS (from old implementation) ===
    int32_t num_logic_channels;
    size_t logic_unitsize;
    uint64_t all_logic_channels_mask;
    uint8_t *raw_sample_buf;
    size_t raw_sample_buf_size;
    uint32_t before_trigger_sample_count;
    uint32_t trigger_channel_mask;
    uint32_t trigger_mask;
    
    // === PROTOCOL MANAGEMENT ===
    JumperlessProtocolMode protocol_mode;
    gboolean protocol_detected;
    gboolean device_identified;
    gboolean header_received;
    gboolean device_configured;     // From old implementation
    gboolean device_armed;
    gboolean acquisition_running;   // Maps to acquisition_active in old implementation
    
    // === DEVICE CAPABILITIES ===
    char *device_id;                    // Dynamically allocated
    uint8_t device_version;
    uint8_t device_capture_mode;        // Maps to capture_mode in old implementation
    uint8_t max_digital_channels;
    uint8_t max_analog_channels;
    uint64_t max_sample_rate;
    uint64_t max_memory_depth;
    gboolean supports_triggers;
    gboolean supports_compression;
    uint8_t adc_resolution_bits;
    
    // === CHANNEL CONFIGURATION ===
    uint32_t digital_channel_mask;      // Changed to uint32_t to match old implementation
    uint32_t analog_channel_mask;       // Changed to uint32_t to match old implementation
    int32_t num_digital_channels;       // Changed to int32_t to match old implementation
    int32_t num_analog_channels;        // Changed to int32_t to match old implementation
    uint8_t bytes_per_sample;
    uint8_t digital_bytes_per_sample;   // From old implementation
    uint8_t analog_bytes_per_sample;    // From old implementation
    uint8_t analog_resolution;          // From old implementation
    float analog_voltage_range;         // From old implementation
    uint32_t max_samples;               // From old implementation
    uint8_t supported_modes;            // From old implementation
    char firmware_version[32];          // From old implementation (increased size)
    
    // === ACQUISITION STATE ===
    // num_samples is already defined above in standard libsigrok fields
    
    // === SAMPLE BUFFERING FOR UNIFIED FORMAT ===
    uint8_t *sample_buffer;             // Buffer for accumulating partial samples
    size_t sample_buffer_size;          // Size of the sample buffer
    size_t sample_buffer_fill;          // Current fill level of the buffer
    size_t expected_sample_size;        // Expected size for current sample (3 or 32 bytes)
    
    // === SAMPLE ACCUMULATION FOR BATCH PROCESSING ===
    uint8_t *accumulated_samples;       // Buffer for complete samples ready for batch processing
    size_t accumulated_samples_size;    // Size of accumulation buffer
    size_t accumulated_samples_fill;    // Current fill level of accumulation buffer
    size_t accumulated_sample_count;    // Number of complete samples accumulated
};

// =============================================================================
// FUNCTION PROTOTYPES
// =============================================================================

// Device detection and identification
SR_PRIV gboolean jlms_detect_device(struct sr_serial_dev_inst *serial);
SR_PRIV int jlms_identify_device(const struct sr_dev_inst *sdi);
SR_PRIV int jlms_determine_protocol(const struct sr_dev_inst *sdi);

// Protocol command functions
SR_PRIV int jlms_send_command(struct sr_serial_dev_inst *serial, 
                             uint8_t command, const void *data, size_t len);
SR_PRIV int jlms_wait_for_response(struct sr_serial_dev_inst *serial, 
                                  uint8_t expected_response, 
                                  uint8_t *buf, size_t buf_size, 
                                  size_t *received_len, int timeout_ms);

/* USB device management */
SR_PRIV gboolean jlms_check_usb_device_present(const char *device_path);
SR_PRIV int jlms_attempt_device_reconnection(struct sr_dev_inst *sdi);
SR_PRIV gboolean jlms_is_device_error_recoverable(int error_code);

// Enhanced protocol functions
SR_PRIV int jlms_receive_header(const struct sr_dev_inst *sdi, gboolean send_request);
SR_PRIV int jlms_configure_channels(const struct sr_dev_inst *sdi);
SR_PRIV int jlms_configure_timing(const struct sr_dev_inst *sdi);
SR_PRIV int jlms_set_device_mode(const struct sr_dev_inst *sdi, uint8_t mode);
SR_PRIV int jlms_arm_device(const struct sr_dev_inst *sdi);
SR_PRIV int jlms_start_acquisition(const struct sr_dev_inst *sdi);

// SUMP protocol functions - REMOVED (Jumperless only supports Enhanced protocol)

// Data acquisition and processing
SR_PRIV int jlms_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int jlms_process_buffered_data(const struct sr_dev_inst *sdi,
                                      const uint8_t *buf, size_t len);
SR_PRIV int jlms_process_accumulated_samples(const struct sr_dev_inst *sdi);
SR_PRIV int jlms_process_unified_data(const struct sr_dev_inst *sdi,
                                     const uint8_t *buf, size_t len);
SR_PRIV int jlms_process_digital_data(const struct sr_dev_inst *sdi,
                                     const uint8_t *buf, size_t len);
SR_PRIV int jlms_process_mixed_signal_data(const struct sr_dev_inst *sdi,
                                          const uint8_t *buf, size_t len);
SR_PRIV int jlms_process_analog_data(const struct sr_dev_inst *sdi,
                                    const uint8_t *buf, size_t len);

// Sample buffer management
SR_PRIV void jlms_init_sample_buffer(struct dev_context *devc);
SR_PRIV void jlms_cleanup_sample_buffer(struct dev_context *devc);

// Utility functions
SR_PRIV int jlms_calculate_sample_rate(uint64_t requested_rate, uint64_t *actual_rate);
SR_PRIV int jlms_calculate_sample_count(uint64_t requested_count, uint64_t *actual_count);
SR_PRIV float jlms_convert_adc_to_voltage(uint16_t adc_value, uint8_t channel);
SR_PRIV uint32_t jlms_calculate_checksum(const void *data, size_t len);

// State management
SR_PRIV void jlms_reset_device_context(struct dev_context *devc);
SR_PRIV void jlms_cleanup_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int jlms_abort_acquisition(const struct sr_dev_inst *sdi);

// Debug and diagnostics
SR_PRIV void jlms_log_device_info(const struct sr_dev_inst *sdi);
SR_PRIV void jlms_log_channel_config(const struct sr_dev_inst *sdi);
SR_PRIV void jlms_log_timing_config(const struct sr_dev_inst *sdi);

// =============================================================================
// VOLTAGE CONVERSION CONSTANTS
// =============================================================================

// Jumperless ADC configuration
#define JUMPERLESS_ADC_RESOLUTION   12      // 12-bit ADC
#define JUMPERLESS_ADC_MAX_VALUE    4095    // 2^12 - 1

// Voltage ranges for different ADC channels
#define JUMPERLESS_ADC_CH4_MIN      0.0f    // ADC4: 0-5V range
#define JUMPERLESS_ADC_CH4_MAX      5.0f
#define JUMPERLESS_ADC_OTHER_MIN    -8.0f   // ADC 0-3,7: ±8V range  
#define JUMPERLESS_ADC_OTHER_MAX    8.0f
#define JUMPERLESS_ADC_SPREAD       18.28f  // Total ADC voltage spread

// Channel mapping
#define JUMPERLESS_ADC_CH4          4       // Special 0-5V channel



#endif /* LIBSIGROK_HARDWARE_JUMPERLESS_MIXED_SIGNAL_PROTOCOL_H */
