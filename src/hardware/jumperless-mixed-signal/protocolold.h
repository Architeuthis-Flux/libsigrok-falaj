/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024 Jumperless Project
 * Based on SUMP implementation patterns
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

#define LOG_PREFIX "jumperless-fala"
#define LOGIC_BUFSIZE 4096
#define MIN_NUM_SAMPLES 1024

/* Jumperless Protocol Constants */
#define JUMPERLESS_HEADER_MAGIC "$JLDATA"

/* Jumperless Commands */
#define JUMPERLESS_CMD_RESET        0x00
#define JUMPERLESS_CMD_RUN          0x01
#define JUMPERLESS_CMD_ID           0x02
#define JUMPERLESS_CMD_GET_HEADER   0x03
#define JUMPERLESS_CMD_SET_CHANNELS 0x04
#define JUMPERLESS_CMD_ARM          0x05

/* Jumperless Responses - Match firmware exactly */
#define JUMPERLESS_RESP_HEADER      0x80
#define JUMPERLESS_RESP_DATA        0x81
#define JUMPERLESS_RESP_STATUS      0x82
#define JUMPERLESS_RESP_ERROR       0x83

/* Jumperless Modes */
#define JUMPERLESS_MODE_DIGITAL_ONLY  0x01
#define JUMPERLESS_MODE_ANALOG_ONLY   0x02
#define JUMPERLESS_MODE_MIXED_SIGNAL  0x03

/* Maximum channels */
#define JUMPERLESS_MAX_DIGITAL_CHANNELS 8
#define JUMPERLESS_MAX_ANALOG_CHANNELS  8

/* Jumperless header structure */
typedef struct {
    char magic[8];                      // "$JLDATA\0"
    uint8_t version;                    // Protocol version
    uint8_t capture_mode;               // Digital/Analog/Mixed
    uint16_t num_digital_channels;      // Number of digital channels
    uint16_t num_analog_channels;       // Number of analog channels
    uint32_t sample_rate;               // Actual sample rate
    uint32_t sample_count;              // Number of samples to capture
    uint32_t digital_channel_mask;      // Which digital channels enabled
    uint32_t analog_channel_mask;       // Which analog channels enabled
    uint8_t bytes_per_sample;           // Total bytes per sample
    uint8_t digital_bytes_per_sample;   // Digital bytes per sample
    uint8_t analog_bytes_per_sample;    // Analog bytes per sample
    uint8_t analog_resolution;          // ADC resolution
    uint32_t trigger_channel_mask;      // Trigger channels
    uint32_t trigger_pattern;           // Trigger pattern
    uint32_t trigger_edge_mask;         // Edge trigger mask
    uint32_t pre_trigger_samples;       // Pre-trigger sample count
    float analog_voltage_range;         // Analog voltage range
    uint32_t max_sample_rate;           // Device max sample rate
    uint32_t max_samples;               // Device max samples
    uint8_t supported_modes;            // Supported capture modes
    char firmware_version[16];          // Firmware version string
    char device_id[16];                 // Device identifier
    uint32_t checksum;                  // Header checksum
} jumperless_header;

struct dev_context
{
    uint64_t cur_samplerate;
    uint64_t limit_samples;
    uint64_t limit_msec;
    uint64_t limit_frames;
    uint64_t num_samples;
    uint64_t sent_frame_samples; /* Number of samples that were sent for current frame. */
    uint32_t num_transfers;
    int64_t start_us;
    int64_t spent_us;
    uint64_t step;
    /* Logic */
    int32_t num_logic_channels;
    size_t logic_unitsize;
    uint64_t all_logic_channels_mask;
    uint8_t *raw_sample_buf;
    size_t raw_sample_buf_size;
    uint32_t before_trigger_sample_count;
    uint32_t trigger_channel_mask;
    uint32_t trigger_mask;
    /* Jumperless protocol specific */
    gboolean header_received;
    gboolean device_configured;
    gboolean acquisition_active;
    uint8_t capture_mode;
    uint32_t digital_channel_mask;
    uint32_t analog_channel_mask;
    int32_t num_analog_channels;
    uint8_t bytes_per_sample;
    uint8_t digital_bytes_per_sample;
    uint8_t analog_bytes_per_sample;
    uint8_t analog_resolution;
    float analog_voltage_range;
    uint32_t max_sample_rate;
    uint32_t max_samples;
    uint8_t supported_modes;
    char firmware_version[32];
    char device_id[32];
};

/* Function prototypes */
SR_PRIV gboolean jumperless_detect_device(struct sr_serial_dev_inst *serial);
SR_PRIV int jumperless_send_command(struct sr_serial_dev_inst *serial, uint8_t command, const void *data, size_t len);
SR_PRIV int jumperless_wait_for_response(struct sr_serial_dev_inst *serial, uint8_t expected_response, uint8_t *buf, size_t buf_size, size_t *received_len, int timeout_ms);
SR_PRIV int jumperless_receive_header(const struct sr_dev_inst *sdi);
SR_PRIV int jumperless_configure_channels(const struct sr_dev_inst *sdi);
SR_PRIV int jumperless_fala_receive_data(int fd, int revents, void *cb_data);
#endif
