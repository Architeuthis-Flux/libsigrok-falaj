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

#include <config.h>
#include <fcntl.h>
#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <strings.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"
// #include "api.h"

// =============================================================================
// DEVICE DETECTION AND IDENTIFICATION
// =============================================================================

//jumperless_analog_names[] (and api.c) is not included in this file, so we need to define this here
SR_PRIV const char *analog_channel_names[] = {
    "ADC 0", "ADC 1", "ADC 2", "ADC 3", "ADC 4",
            "Pad Sense", "Supply", "Probe Measure", 
        "DAC 0 Output", "DAC 1 Output", "INA 0 Voltage", "INA 0 Current", "INA 1 Voltage", "INA 1 Current",
};



/* Detect if a Jumperless device is connected on the specified serial port */
SR_PRIV gboolean jlms_detect_device(struct sr_serial_dev_inst *serial)
{
    uint8_t response_header;
    uint16_t payload_len;
    char id_buf[256];
    int ret;
    
    sr_dbg("Attempting to detect Jumperless device...");
    
    /* Send ID command */
    ret = serial_write_blocking(serial, "\xA2", 1, JUMPERLESS_DETECT_TIMEOUT);
    if (ret != 1) {
        sr_dbg("Failed to send ID command");
        return FALSE;
    }
    
    /* Try to read enhanced protocol response first */
    ret = serial_read_blocking(serial, &response_header, 1, JUMPERLESS_DETECT_TIMEOUT);
    if (ret == 1 && response_header == JUMPERLESS_RESP_ID) {
        ret = serial_read_blocking(serial, (uint8_t *)&payload_len, 2, JUMPERLESS_DETECT_TIMEOUT);
        if (ret == 2 && payload_len > 0 && payload_len < sizeof(id_buf)) {
            ret = serial_read_blocking(serial, (uint8_t *)id_buf, payload_len, JUMPERLESS_DETECT_TIMEOUT);
            if (ret == payload_len) {
                id_buf[payload_len] = '\0';
                sr_info("Detected Jumperless device (Enhanced): %s", id_buf);
                return TRUE;
            }
        }
    } else if (ret == 1) {
        /* Try SUMP response: should be "1SLO" or "1ALS" */
        char sump_id[4];
        sump_id[0] = response_header;
        ret = serial_read_blocking(serial, (uint8_t *)&sump_id[1], 3, JUMPERLESS_DETECT_TIMEOUT);
        if (ret == 3) {
            if (memcmp(sump_id, "1SLO", 4) == 0 || memcmp(sump_id, "1ALS", 4) == 0) {
                sr_info("Detected Jumperless device (SUMP): %.4s", sump_id);
                return TRUE;
            }
        }
    }
    
    sr_dbg("No Jumperless device detected");
    return FALSE;
}

/* Identify device and determine protocol mode */
SR_PRIV int jlms_identify_device(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    struct sr_serial_dev_inst *serial = sdi->conn;
    uint8_t response_header;
    uint16_t payload_len;
    char id_buf[256];
    int ret;
    
    sr_dbg("Identifying device and determining protocol mode...");
    
    /* Send ID command */
    ret = serial_write_blocking(serial, "\xA2", 1, JUMPERLESS_DETECT_TIMEOUT);
    if (ret != 1) {
        sr_err("Failed to send ID command");
        return SR_ERR;
    }
    
    /* Read response header */
    ret = serial_read_blocking(serial, &response_header, 1, JUMPERLESS_DETECT_TIMEOUT);
    if (ret != 1) {
        sr_err("Failed to read response header");
        return SR_ERR;
    }
    
    if (response_header == JUMPERLESS_RESP_ID) {
        /* Enhanced protocol response */
        ret = serial_read_blocking(serial, (uint8_t *)&payload_len, 2, JUMPERLESS_DETECT_TIMEOUT);
        if (ret != 2) {
            sr_err("Failed to read payload length");
            return SR_ERR;
        }
        
        if (payload_len == 0 || payload_len >= sizeof(id_buf)) {
            sr_err("Invalid payload length: %u", payload_len);
            return SR_ERR;
        }
        
        ret = serial_read_blocking(serial, (uint8_t *)id_buf, payload_len, JUMPERLESS_DETECT_TIMEOUT);
        if (ret != payload_len) {
            sr_err("Failed to read device ID");
            return SR_ERR;
        }
        
        id_buf[payload_len] = '\0';
        devc->protocol_mode = JUMPERLESS_PROTOCOL_ENHANCED;
        g_free(devc->device_id);
        devc->device_id = g_strdup(id_buf);
        sr_info("Device ID (Enhanced): %s", devc->device_id);
        
    } else {
        /* Try SUMP protocol response */
        char sump_id[4];
        sump_id[0] = response_header;
        ret = serial_read_blocking(serial, (uint8_t *)&sump_id[1], 3, JUMPERLESS_DETECT_TIMEOUT);
        if (ret != 3) {
            sr_err("Failed to read SUMP ID");
            return SR_ERR;
        }
        
        if (memcmp(sump_id, "1SLO", 4) == 0 || memcmp(sump_id, "1ALS", 4) == 0) {
            /* Detected SUMP-compatible device, but probe for Enhanced protocol support */
            sr_info("Device ID (SUMP base): %.*s", 4, sump_id);
            
            /* Try to probe for Enhanced protocol capabilities */
            sr_dbg("Probing for Enhanced protocol support...");
            ret = serial_write_blocking(serial, "\xA3", 1, JUMPERLESS_DETECT_TIMEOUT);
            if (ret == 1) {
                /* Wait for Enhanced protocol header response */
                uint8_t enhanced_response;
                ret = serial_read_blocking(serial, &enhanced_response, 1, 500); /* Short timeout */
                if (ret == 1 && enhanced_response == JUMPERLESS_RESP_HEADER) {
                    /* Device supports Enhanced protocol! */
                    devc->protocol_mode = JUMPERLESS_PROTOCOL_ENHANCED;
                    g_free(devc->device_id);
                    devc->device_id = g_strdup("Jumperless Enhanced");
                    sr_info("Enhanced protocol support detected - enabling mixed-signal mode");
                    
                    /* Read and discard the header data for now - we'll get it properly later */
                    uint16_t header_len;
                    ret = serial_read_blocking(serial, (uint8_t *)&header_len, 2, JUMPERLESS_DETECT_TIMEOUT);
                    if (ret == 2 && header_len > 0 && header_len < 1024) {
                        uint8_t discard_buf[1024];
                        serial_read_blocking(serial, discard_buf, header_len, JUMPERLESS_DETECT_TIMEOUT);
                    }
                } else {
                    /* Only SUMP protocol supported */
                    devc->protocol_mode = JUMPERLESS_PROTOCOL_SUMP;
                    g_free(devc->device_id);
                    devc->device_id = g_strndup(sump_id, 4);
                    sr_info("SUMP-only protocol detected");
                }
            } else {
                /* Only SUMP protocol supported */
                devc->protocol_mode = JUMPERLESS_PROTOCOL_SUMP;
                g_free(devc->device_id);
                devc->device_id = g_strndup(sump_id, 4);
                sr_info("SUMP-only protocol detected");
            }
        } else {
            sr_err("Unknown device response: %02x %02x %02x %02x", 
                   sump_id[0], sump_id[1], sump_id[2], sump_id[3]);
            return SR_ERR;
        }
    }
    
    devc->protocol_detected = TRUE;
    devc->device_identified = TRUE;
    
    sr_info("Protocol mode: %s", 
            devc->protocol_mode == JUMPERLESS_PROTOCOL_ENHANCED ? "Enhanced" : "SUMP");
    
    return SR_OK;
}

/* Determine protocol based on initial communication */
SR_PRIV int jlms_determine_protocol(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    
    if (!devc->protocol_detected) {
        return jlms_identify_device(sdi);
    }
    
    return SR_OK;
}

// =============================================================================
// COMMAND HANDLING
// =============================================================================

/* Flush any stale data from serial buffer */
static void jlms_flush_serial_buffer(struct sr_serial_dev_inst *serial)
{
    uint8_t discard_buf[256];
    int bytes_discarded = 0;
    int attempts = 0;
    
    /* Try to flush any stale data with a very short timeout */
    while (attempts < 5) {
        int ret = serial_read_nonblocking(serial, discard_buf, sizeof(discard_buf));
        if (ret <= 0) {
            break;  /* No more data */
        }
        bytes_discarded += ret;
        attempts++;
    }
    
    if (bytes_discarded > 0) {
        sr_dbg("Flushed %d stale bytes from serial buffer", bytes_discarded);
    }
}

/* Send command to device */
SR_PRIV int jlms_send_command(struct sr_serial_dev_inst *serial,
                             uint8_t command, const void *data, size_t len)
{
    uint8_t cmd_buf[256];
    size_t total_len;
    int ret;
    
    if (len > sizeof(cmd_buf) - 1) {
        sr_err("Command data too large: %zu bytes", len);
        return SR_ERR_ARG;
    }
    
    /* Flush any stale data before sending command */
    jlms_flush_serial_buffer(serial);
    
    /* Build command packet */
    cmd_buf[0] = command;
    if (data && len > 0) {
        memcpy(&cmd_buf[1], data, len);
    }
    total_len = 1 + len;
    
    /* Send command */
    ret = serial_write_blocking(serial, cmd_buf, total_len, JUMPERLESS_COMMAND_TIMEOUT);
    if (ret != (int)total_len) {
        /* Check if this might be a USB disconnection */
        if (ret < 0 && jlms_is_device_error_recoverable(errno)) {
            sr_warn("Device appears to be disconnected (command 0x%02x failed with errno %d: %s)", 
                   command, errno, strerror(errno));
            return SR_ERR_IO; /* Special error code to indicate device disconnection */
        } else {
            sr_err("Failed to send command 0x%02x (%d of %zu bytes written)", 
                   command, ret, total_len);
            return SR_ERR;
        }
    }
    
    sr_spew("Sent command 0x%02x with %zu bytes of data", command, len);
    return SR_OK;
}

/* Wait for response from device - Enhanced for Jumperless firmware compatibility */
SR_PRIV int jlms_wait_for_response(struct sr_serial_dev_inst *serial,
                                  uint8_t expected_response,
                                  uint8_t *buf, size_t buf_size,
                                  size_t *received_len, int timeout_ms)
{
    uint8_t response_header;
    int ret;
    int retry_count = 0;
    const int max_retries = 3;
    
    /* Validate input parameters */
    if (!serial || !buf || buf_size == 0 || !received_len) {
        sr_err("Invalid response buffer parameters");
        return SR_ERR_ARG;
    }
    
    /* Initialize output */
    *received_len = 0;
    
    /* Use shorter timeout for cleanup operations */
    int actual_timeout = (timeout_ms > 2000) ? 2000 : timeout_ms;
    
retry_response:
    /* Read response header with timeout */
    ret = serial_read_blocking(serial, &response_header, 1, actual_timeout);
    if (ret != 1) {
        if (ret == 0) {
            sr_spew("Timeout waiting for response header");
        } else {
            sr_warn("Failed to read response header: %d", ret);
        }
        return SR_ERR_TIMEOUT;
    }
    
    /* Handle Jumperless firmware responses which may not have length headers */
    if (expected_response == JUMPERLESS_RESP_STATUS && response_header == 0x82) {
        /* Status response: expect 1 more byte (status code) */
        ret = serial_read_blocking(serial, buf, 1, actual_timeout);
        if (ret == 1) {
            *received_len = 1;
            sr_spew("Received Jumperless status response: 0x%02x", buf[0]);
            return SR_OK;
        } else {
            sr_warn("Failed to read status code");
            return SR_ERR_TIMEOUT;
        }
    }
    
    /* Handle header response - Jumperless sends SOH + 107 bytes + EOH */
    if (expected_response == JUMPERLESS_RESP_HEADER && response_header == 0x80) {
        /* First read SOH character */
        uint8_t soh;
        ret = serial_read_blocking(serial, &soh, 1, actual_timeout);
        if (ret != 1 || soh != JUMPERLESS_HEADER_SOH) {
            sr_warn("Failed to read SOH or invalid SOH: 0x%02x", soh);
            return SR_ERR_TIMEOUT;
        }
        
        /* Header response: expect fixed 107 bytes (Jumperless header format) */
        size_t header_size = 107;
        if (header_size > buf_size) {
            sr_warn("Header too large for buffer: %zu bytes (buffer: %zu)", header_size, buf_size);
            /* Read what we can, discard the rest */
            ret = serial_read_blocking(serial, buf, buf_size, actual_timeout);
            if (ret > 0) {
                *received_len = ret;
                /* Discard remaining bytes */
                uint8_t discard_buf[256];
                size_t remaining = header_size - ret;
                while (remaining > 0) {
                    size_t chunk = (remaining > sizeof(discard_buf)) ? sizeof(discard_buf) : remaining;
                    int discarded = serial_read_blocking(serial, discard_buf, chunk, 100);
                    if (discarded <= 0) break;
                    remaining -= discarded;
                }
                sr_info("Received partial header: %zu of %zu bytes", *received_len, header_size);
                return SR_OK;
            }
            return SR_ERR_TIMEOUT;
        }
        
        /* Read the full header */
        ret = serial_read_blocking(serial, buf, header_size, actual_timeout);
        if (ret != (int)header_size) {
            sr_warn("Failed to read complete header: %d of %zu bytes", ret, header_size);
            *received_len = (ret > 0) ? ret : 0;
            return SR_ERR_TIMEOUT;
        }
        
        /* Read EOH character */
        uint8_t eoh;
        ret = serial_read_blocking(serial, &eoh, 1, actual_timeout);
        if (ret != 1 || eoh != JUMPERLESS_HEADER_EOH) {
            sr_warn("Failed to read EOH or invalid EOH: 0x%02x", eoh);
            return SR_ERR_TIMEOUT;
        }
        
        *received_len = header_size;
        sr_spew("Received Jumperless header: %zu bytes (SOH+header+EOH)", header_size);
        return SR_OK;
    }
    
    /* Check response type */
    if (response_header != expected_response) {
        /* Handle error responses gracefully */
        if (response_header == JUMPERLESS_RESP_ERROR || response_header == 0x83) {
            uint8_t error_code = 0;
            if (serial_read_blocking(serial, &error_code, 1, 100) == 1) {
                sr_warn("Device error response: 0x%02x", error_code);
            } else {
                sr_warn("Device error response (no error code)");
            }
            return SR_ERR_DATA;
        } else {
            sr_warn("Unexpected response: 0x%02x (expected 0x%02x)", 
                   response_header, expected_response);
            
            /* For Jumperless, treat unexpected responses more tolerantly */
            if (retry_count < max_retries) {
                sr_dbg("Retrying response read after buffer flush (attempt %d/%d)", 
                       retry_count + 1, max_retries);
                jlms_flush_serial_buffer(serial);
                retry_count++;
                goto retry_response;
            }
            
            /* Return partial success for compatibility */
            buf[0] = response_header;
            *received_len = 1;
            sr_warn("Using unexpected response as data for compatibility");
            return SR_OK;
        }
    }
    
    /* Legacy path: shouldn't reach here with Jumperless firmware */
    sr_warn("Unexpected response format - treating as no-payload response");
    *received_len = 0;
    return SR_OK;
}

// =============================================================================
// ENHANCED PROTOCOL FUNCTIONS
// =============================================================================

/* Set device capture mode */
SR_PRIV int jlms_set_device_mode(const struct sr_dev_inst *sdi, uint8_t mode)
{
    struct dev_context *devc = sdi->priv;
    
    sr_info("Setting Jumperless device mode to %u (context-only, no channel changes)", mode);
    
    /* Validate mode */
    if (mode > JUMPERLESS_MODE_ANALOG_ONLY) {
        sr_err("Invalid capture mode: %u", mode);
        return SR_ERR_ARG;
    }
    
    /* Update device context - DON'T send SET_MODE command to firmware */
    /* The SET_CHANNELS command during acquisition will configure the actual hardware */
    /* DON'T automatically enable/disable channels - respect user's channel selection */
    devc->device_capture_mode = mode;
    
    sr_info("Device mode set to %u in context (user channel selection preserved)", mode);
    
    return SR_OK;
}

/* Receive device capabilities header */
SR_PRIV int jlms_receive_header(const struct sr_dev_inst *sdi, gboolean send_request)
{
    struct dev_context *devc = sdi->priv;
    struct sr_serial_dev_inst *serial = sdi->conn;
    uint8_t response_buf[512];
    size_t received_len;
    jumperless_header header;
    int ret;

    if (send_request) {
        sr_dbg("Requesting device capabilities header...");
        ret = jlms_send_command(serial, JUMPERLESS_CMD_GET_HEADER, NULL, 0);
        if (ret != SR_OK) {
            sr_warn("Failed to send header request, continuing with defaults");
            devc->header_received = FALSE;
            return SR_OK;
        }
    }

    int timeout = send_request ? JUMPERLESS_RESPONSE_TIMEOUT : 250;

    ret = jlms_wait_for_response(serial, JUMPERLESS_RESP_HEADER,
                                response_buf, sizeof(response_buf),
                                &received_len, timeout);
    if (ret != SR_OK) {
        if (!send_request) {
            sr_dbg("No unsolicited header received from firmware, which is acceptable.");
            return SR_OK;
        }
        sr_warn("Failed to receive header, using defaults: %d", ret);
        devc->header_received = FALSE;
        return SR_OK;
    }

    if (received_len < 50) {
        sr_warn("Header too small: %zu bytes (expected ~107), using defaults", received_len);
        devc->header_received = FALSE;
        return SR_OK;
    }

    sr_info("Header received: %zu bytes (expected %zu)", received_len, sizeof(jumperless_header));

    size_t copy_size = (received_len < sizeof(header)) ? received_len : sizeof(header);
    memset(&header, 0, sizeof(header));
    memcpy(&header, response_buf, copy_size);

    if (copy_size >= 8 && memcmp(header.magic, "$JLDATA", 7) == 0) {
        sr_info("Valid Jumperless header magic found");
    } else {
        sr_warn("Header magic not found, parsing as raw data");
    }
    
    sr_warn("Header received from firmware:");
    sr_warn("  max_digital_channels: %u (was: %u)", header.max_digital_channels, devc->max_digital_channels);
    sr_warn("  max_analog_channels: %u (was: %u)", header.max_analog_channels, devc->max_analog_channels);
    sr_warn("  max_memory_depth: %"PRIu64" (was: %"PRIu64")", header.max_memory_depth, devc->max_memory_depth);
    
    devc->header_received = TRUE;
    devc->device_version = header.version;
    devc->device_capture_mode = header.capture_mode;
    devc->max_digital_channels = header.max_digital_channels;
    devc->max_analog_channels = header.max_analog_channels;

    if (devc->max_memory_depth != header.max_memory_depth) {
        sr_info("Memory depth updated: %"PRIu64" -> %"PRIu64" samples", 
               devc->max_memory_depth, header.max_memory_depth);
        devc->max_memory_depth = header.max_memory_depth;
        sr_session_send_meta(sdi, SR_CONF_LIMIT_SAMPLES,
                             g_variant_new_uint64(devc->max_memory_depth));
    }

    devc->max_sample_rate = header.max_sample_rate;
    devc->supports_triggers = header.supports_triggers;
    devc->supports_compression = header.supports_compression;
    devc->adc_resolution_bits = header.adc_resolution_bits;
    devc->analog_voltage_range = header.analog_voltage_range;

    sr_info("Device capabilities received (v%u, %u digital + %u analog channels, range=%.2fV)",
            devc->device_version, devc->max_digital_channels, devc->max_analog_channels,
            devc->analog_voltage_range);

    return SR_OK;
}


/* Configure active channels */
SR_PRIV int jlms_configure_channels(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    struct sr_serial_dev_inst *serial = sdi->conn;
    GSList *l;
    uint32_t digital_mask = 0;  /* Changed from uint8_t to uint32_t */
    uint32_t analog_mask = 0;   /* Changed from uint8_t to uint32_t */
    uint8_t config_data[16];
    uint8_t *ptr = config_data;
    uint8_t response_buf[16];
    size_t received_len;
    int digital_count = 0, analog_count = 0;
    int ret;
    
    sr_dbg("Configuring active channels...");
    
    /* DEBUG: Log all channel info before processing */
    sr_dbg("=== Channel Analysis ===");
    int total_channels = 0, enabled_channels = 0, analog_channels_found = 0;
    for (l = sdi->channels; l; l = l->next) {
        struct sr_channel *ch = l->data;
        total_channels++;
        if (ch->enabled) {
            enabled_channels++;
            if (ch->type == SR_CHANNEL_ANALOG) {
                analog_channels_found++;
            }
        }
        sr_dbg("Channel '%s': index=%d, type=%s, enabled=%s", 
               ch->name, ch->index, 
               (ch->type == SR_CHANNEL_LOGIC) ? "LOGIC" : "ANALOG",
               ch->enabled ? "YES" : "NO");
    }
    sr_dbg("Summary: %d total channels, %d enabled, %d analog enabled", 
           total_channels, enabled_channels, analog_channels_found);
    sr_dbg("max_digital_channels=%d, max_analog_channels=%d", 
           devc->max_digital_channels, devc->max_analog_channels);
    sr_dbg("========================");
    
    /* Initialize analog mask and count */
    analog_mask = 0x00;
    analog_count = 0;
    
    /* Build channel masks from enabled channels */
    for (l = sdi->channels; l; l = l->next) {
        struct sr_channel *ch = l->data;
        if (!ch->enabled)
            continue;
            
        if (ch->type == SR_CHANNEL_LOGIC) {
            if (ch->index < 8) {
                uint32_t old_mask = digital_mask;
                digital_mask |= (1 << ch->index);
                digital_count++;
                sr_dbg("Digital channel '%s' (index %d): mask 0x%08X → 0x%08X", 
                       ch->name, ch->index, old_mask, digital_mask);
            }
        } else if (ch->type == SR_CHANNEL_ANALOG) {
            /* Find ADC index by matching channel name with analog_channel_names */
            int adc_index = -1;
            for (int i = 0; i < JUMPERLESS_MAX_ANALOG_CHANNELS; i++) {
                if (strcmp(ch->name, analog_channel_names[i]) == 0) {
                    adc_index = i;
                    break;
                }
            }
            
            sr_dbg("Processing analog channel '%s': ch->index=%d, adc_index=%d", 
                   ch->name, ch->index, adc_index);
            
            if (adc_index >= 0 && adc_index < devc->max_analog_channels) {
                /* Enable this analog channel */
                uint32_t old_mask = analog_mask;
                analog_mask |= (1 << adc_index);
                analog_count++;
                sr_dbg("Analog channel '%s' → ADC %d: mask 0x%08X → 0x%08X", 
                       ch->name, adc_index, old_mask, analog_mask);
            } else {
                sr_warn("Analog channel '%s' has invalid ADC index %d (should be 0-%d)", 
                        ch->name, adc_index, devc->max_analog_channels - 1);
            }
        }
    }
    
    sr_info("Channel configuration: digital_count=%d (mask=0x%08X), analog_count=%d (mask=0x%08X)", 
            digital_count, digital_mask, analog_count, analog_mask);
    
    /* Check if configuration has actually changed */
    if (digital_mask == devc->digital_channel_mask && analog_mask == devc->analog_channel_mask) {
        sr_dbg("Channel configuration unchanged (digital=0x%08X, analog=0x%08X) - skipping SET_CHANNELS command", 
               digital_mask, analog_mask);
        return SR_OK;
    }
    
    sr_info("Channel configuration changed: digital 0x%08X→0x%08X, analog 0x%08X→0x%08X - sending SET_CHANNELS", 
            devc->digital_channel_mask, digital_mask, devc->analog_channel_mask, analog_mask);
    
    /* Always use mixed-signal mode with unified 32-byte format */
    devc->device_capture_mode = JUMPERLESS_MODE_MIXED_SIGNAL;
    sr_info("Mode: Unified mixed-signal format (%d digital + %d analog channels)", digital_count, analog_count);
    
    /* Always use unified 32-byte format - CRITICAL: Must match firmware exactly */
    devc->bytes_per_sample = 32;  /* 3 digital + 28 analog + 1 EOF = 32 bytes total */
    sr_info("Unified format: Always 32 bytes per sample (%d analog channels enabled)", analog_count);
    
    /* Only send channel configuration for Enhanced protocol */
    if (devc->protocol_mode == JUMPERLESS_PROTOCOL_ENHANCED) {
        /* Build configuration packet */
        /* Digital mask (4 bytes, little-endian) */
        uint32_t digital_mask_32 = digital_mask;
        *ptr++ = (digital_mask_32 >>  0) & 0xFF;
        *ptr++ = (digital_mask_32 >>  8) & 0xFF;
        *ptr++ = (digital_mask_32 >> 16) & 0xFF;
        *ptr++ = (digital_mask_32 >> 24) & 0xFF;
        
        /* Analog mask (4 bytes, little-endian) */
        uint32_t analog_mask_32 = analog_mask;
        *ptr++ = (analog_mask_32 >>  0) & 0xFF;
        *ptr++ = (analog_mask_32 >>  8) & 0xFF;
        *ptr++ = (analog_mask_32 >> 16) & 0xFF;
        *ptr++ = (analog_mask_32 >> 24) & 0xFF;
        
        /* Send channel configuration */
        ret = jlms_send_command(serial, JUMPERLESS_CMD_SET_CHANNELS,
                               config_data, ptr - config_data);
        if (ret != SR_OK) {
            return ret;
        }
        
        /* DEBUG: Log the exact data sent to firmware */
        sr_dbg("Sent SET_CHANNELS command with %zd bytes:", ptr - config_data);
        sr_dbg("  Digital mask bytes: 0x%02X 0x%02X 0x%02X 0x%02X (= 0x%08X)", 
               config_data[0], config_data[1], config_data[2], config_data[3], digital_mask_32);
        sr_dbg("  Analog mask bytes:  0x%02X 0x%02X 0x%02X 0x%02X (= 0x%08X)", 
               config_data[4], config_data[5], config_data[6], config_data[7], analog_mask_32);
        
        /* Wait for acknowledgment */
        ret = jlms_wait_for_response(serial, JUMPERLESS_RESP_STATUS,
                                    response_buf, sizeof(response_buf),
                                    &received_len, JUMPERLESS_RESPONSE_TIMEOUT);
        if (ret != SR_OK) {
            sr_warn("Channel configuration acknowledgment failed, continuing anyway: %d", ret);
            /* Don't fail completely - firmware might be working */
        } else if (received_len < 1 || response_buf[0] != JUMPERLESS_STATUS_OK) {
            sr_warn("Channel configuration status not OK (got 0x%02x), continuing anyway", 
                   received_len > 0 ? response_buf[0] : 0xFF);
            /* Don't fail - firmware might still work */
        } else {
            sr_info("Channel configuration acknowledged successfully");
        }
        
        /* After channel configuration, the firmware sends an updated header.
         * We need to read it here to keep the communication synchronized. */
        sr_dbg("Waiting for potentially updated header from firmware...");
        ret = jlms_receive_header(sdi, FALSE);
        if (ret == SR_OK) {
            sr_info("Successfully processed updated header from firmware.");
        } else {
            sr_warn("Did not receive an updated header from firmware, continuing.");
        }
    }
    
    devc->digital_channel_mask = digital_mask;
    devc->analog_channel_mask = analog_mask;
    devc->num_digital_channels = digital_count;
    devc->num_analog_channels = analog_count;
    
    sr_info("Configured %d digital + %d analog channels (mode: %u, digital_mask: 0x%08X, analog_mask: 0x%08X)",
            digital_count, analog_count, devc->device_capture_mode, digital_mask, analog_mask);
    
    /* Debug: Log the channel-to-ADC mapping */
    for (GSList *l = sdi->channels; l; l = l->next) {
        struct sr_channel *ch = l->data;
        if (ch->enabled && ch->type == SR_CHANNEL_ANALOG) {
            /* Find ADC index by matching channel name with analog_channel_names */
            int adc_index = -1;
            for (int i = 0; i < JUMPERLESS_MAX_ANALOG_CHANNELS; i++) {
                if (strcmp(ch->name, analog_channel_names[i]) == 0) {
                    adc_index = i;
                    break;
                }
            }
            sr_dbg("Channel %s (index %d) -> ADC %d, enabled: %s", 
                   ch->name, ch->index, adc_index, ((analog_mask >> adc_index) & 1) ? "YES" : "NO");
        }
    }
    
    return SR_OK;
}

/* Configure timing parameters */
SR_PRIV int jlms_configure_timing(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    struct sr_serial_dev_inst *serial = sdi->conn;
    uint64_t actual_samplerate, actual_samples;
    uint8_t config_data[32];
    uint8_t *ptr = config_data;
    uint8_t response_buf[16];
    size_t received_len;
    int ret;
    
    sr_dbg("Configuring timing parameters...");
    
    /* Calculate actual sample rate and count */
    ret = jlms_calculate_sample_rate(devc->cur_samplerate, &actual_samplerate);
    if (ret != SR_OK) {
        return ret;
    }
    
    ret = jlms_calculate_sample_count(devc->limit_samples, &actual_samples);
    if (ret != SR_OK) {
        return ret;
    }
    
    devc->cur_samplerate = actual_samplerate;
    devc->limit_samples = actual_samples;
    
    /* Build timing configuration packet */
    /* Sample rate (4 bytes, little-endian) - firmware expects uint32_t */
    *ptr++ = (actual_samplerate >>  0) & 0xFF;
    *ptr++ = (actual_samplerate >>  8) & 0xFF;
    *ptr++ = (actual_samplerate >> 16) & 0xFF;
    *ptr++ = (actual_samplerate >> 24) & 0xFF;
    
    /* Sample count (4 bytes, little-endian) - firmware expects uint32_t */
    *ptr++ = (actual_samples >>  0) & 0xFF;
    *ptr++ = (actual_samples >>  8) & 0xFF;
    *ptr++ = (actual_samples >> 16) & 0xFF;
    *ptr++ = (actual_samples >> 24) & 0xFF;
    
    /* Send timing configuration */
    ret = jlms_send_command(serial, JUMPERLESS_CMD_CONFIGURE,
                           config_data, ptr - config_data);
    if (ret == SR_ERR_IO) {
        /* Device disconnection detected - attempt reconnection */
        sr_warn("Device disconnection detected during timing configuration");
        ret = jlms_attempt_device_reconnection((struct sr_dev_inst *)sdi);
        if (ret != SR_OK) {
            sr_warn("Failed to reconnect device, using current settings");
            return SR_OK;  /* Don't fail completely */
        }
        
        /* Retry the timing configuration after reconnection */
        sr_info("Device reconnected, retrying timing configuration...");
        ret = jlms_send_command(serial, JUMPERLESS_CMD_CONFIGURE,
                               config_data, ptr - config_data);
        if (ret != SR_OK) {
            sr_warn("Timing configuration failed after reconnection, using defaults");
            return SR_OK;  /* Don't fail completely */
        }
    } else if (ret != SR_OK) {
        sr_warn("Timing configuration send failed, using defaults: %d", ret);
        return SR_OK;  /* Don't fail completely */
    }
    
    /* Wait for acknowledgment */
    ret = jlms_wait_for_response(serial, JUMPERLESS_RESP_STATUS,
                                response_buf, sizeof(response_buf),
                                &received_len, JUMPERLESS_RESPONSE_TIMEOUT);
    if (ret != SR_OK) {
        sr_warn("Timing configuration acknowledgment failed, continuing anyway: %d", ret);
        return SR_OK;  /* Don't fail completely */
    }
    
    if (received_len < 1 || response_buf[0] != JUMPERLESS_STATUS_OK) {
        sr_warn("Timing configuration status not OK (got 0x%02x), continuing anyway", 
               received_len > 0 ? response_buf[0] : 0xFF);
        return SR_OK;  /* Don't fail completely */
    }
    
    sr_info("Configured timing: %"PRIu64" Hz, %"PRIu64" samples",
            actual_samplerate, actual_samples);
    
    /* After timing configuration, firmware sends an updated header with new
     * max sample depth, especially if oversampling was enabled/disabled. */
    sr_dbg("Waiting for potentially updated header from firmware after timing change...");
    ret = jlms_receive_header(sdi, FALSE);
    if (ret == SR_OK) {
        sr_info("Successfully processed updated header after timing change.");
    } else {
        sr_warn("Did not receive an updated header after timing change, continuing.");
    }
    
    return SR_OK;
}

/* Arm device for acquisition */
SR_PRIV int jlms_arm_device(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    struct sr_serial_dev_inst *serial = sdi->conn;
    uint8_t response_buf[16];
    size_t received_len;
    int ret;
    
    sr_dbg("Arming device for acquisition...");
    
    /* Send arm command */
    ret = jlms_send_command(serial, JUMPERLESS_CMD_ARM, NULL, 0);
    if (ret != SR_OK) {
        return ret;
    }
    
    /* Wait for acknowledgment */
    ret = jlms_wait_for_response(serial, JUMPERLESS_RESP_STATUS,
                                response_buf, sizeof(response_buf),
                                &received_len, JUMPERLESS_RESPONSE_TIMEOUT);
    if (ret != SR_OK) {
        return ret;
    }
    
    if (received_len < 1 || response_buf[0] != JUMPERLESS_STATUS_OK) {
        sr_err("Device arming failed");
        return SR_ERR;
    }
    
    devc->device_armed = TRUE;
    sr_info("Device armed successfully");
    
    /* Auto-RUN: If no triggers are configured, start acquisition immediately */
    if (devc->trigger_channel_mask == 0 && devc->trigger_mask == 0) {
        sr_dbg("No triggers configured - starting acquisition immediately");
        return jlms_start_acquisition(sdi);
    }
    
    return SR_OK;
}

/* Start acquisition */
SR_PRIV int jlms_start_acquisition(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    struct sr_serial_dev_inst *serial = sdi->conn;
    uint8_t response_buf[16];
    size_t received_len;
    int ret;
    
    sr_dbg("Starting acquisition...");
    
    /* Send run command */
    ret = jlms_send_command(serial, JUMPERLESS_CMD_RUN, NULL, 0);
    if (ret != SR_OK) {
        return ret;
    }
    
    /* Wait for acknowledgment */
    ret = jlms_wait_for_response(serial, JUMPERLESS_RESP_STATUS,
                                response_buf, sizeof(response_buf),
                                &received_len, JUMPERLESS_RESPONSE_TIMEOUT);
    if (ret != SR_OK) {
        return ret;
    }
    
    if (received_len < 1 || response_buf[0] != JUMPERLESS_STATUS_OK) {
        sr_err("Acquisition start failed");
        return SR_ERR;
    }
    
    devc->acquisition_running = TRUE;
    devc->num_samples = 0;
    
    /* Register serial data callback with proper file descriptor */
    ret = serial_source_add(sdi->session, serial, G_IO_IN, 50,
                           jlms_receive_data, (void *)sdi);
    if (ret != SR_OK) {
        sr_err("Failed to add serial data source");
        devc->acquisition_running = FALSE;
        return ret;
    }
    
    sr_info("Acquisition started successfully");
    
    return SR_OK;
}

// =============================================================================
// SUMP PROTOCOL FUNCTIONS - REMOVED
// Jumperless firmware does not support SUMP protocol commands (0x80, 0x81, etc.)
// These functions have been removed to prevent protocol mismatches
// =============================================================================

/* SUMP functions removed - Jumperless only supports Enhanced protocol */

// =============================================================================
// DATA PROCESSING
// =============================================================================

// Data format markers for new unified format (must match firmware)
#define DIGITAL_ONLY_MARKER    0xDD  // Digital only (end of sample)
#define MIXED_SIGNAL_MARKER    0xDA  // Mixed signal (expect 28 more bytes)
#define ANALOG_ONLY_MARKER     0xAA  // Analog only (digital bytes dummy, expect 28 more bytes)
#define ANALOG_EOF_MARKER      0xA0  // End of analog data

/* Process digital-only data using new unified format */
SR_PRIV int jlms_process_digital_data(const struct sr_dev_inst *sdi,
                                     const uint8_t *buf, size_t len)
{
    struct dev_context *devc = sdi->priv;
    struct sr_datafeed_packet packet;
    struct sr_datafeed_logic logic;
    uint8_t *digital_data = NULL;
    const uint8_t *ptr = buf;
    size_t remaining = len;
    size_t sample_count = 0;
    int i;
    
    if (len == 0) {
        return SR_OK;
    }
    
    /* New format: Each sample is exactly 3 bytes: [gpio][uart][marker] */
    /* For digital-only mode, marker should be 0xDD */
    sample_count = len / 3;
    if (sample_count == 0) {
        return SR_OK;
    }
    
    /* Allocate buffer for processed digital data */
    digital_data = g_malloc(sample_count);
    
    /* Parse samples in new 3-byte format */
    for (i = 0; i < (int)sample_count && remaining >= 3; i++) {
        uint8_t gpio_byte = ptr[0];
        uint8_t uart_byte = ptr[1];  /* UART data - currently unused */
        uint8_t marker = ptr[2];
        
        /* Verify this is digital-only data */
        if (marker != DIGITAL_ONLY_MARKER) {
            sr_warn("Expected digital-only marker (0x%02X) but got 0x%02X at sample %d", 
                    DIGITAL_ONLY_MARKER, marker, i);
        }
        
        /* Store GPIO data (8 digital channels) */
        digital_data[i] = gpio_byte;
        
        ptr += 3;
        remaining -= 3;
    }
    
    /* Send digital data packet */
    if (digital_data && sample_count > 0) {
        packet.type = SR_DF_LOGIC;
        packet.payload = &logic;
        logic.length = sample_count;
        logic.unitsize = 1;  /* 1 byte per sample (8 channels) */
        logic.data = digital_data;
        sr_session_send(sdi, &packet);
        
        sr_spew("Sent %zu digital samples (new 3-byte format)", sample_count);
    }
    
    /* Cleanup */
    g_free(digital_data);
    
    devc->num_samples += sample_count;
    
    /* Check if acquisition is complete */
    if (devc->limit_samples > 0 && devc->num_samples >= devc->limit_samples) {
        sr_info("Digital acquisition complete (%"PRIu64" samples)", devc->num_samples);
        return SR_ERR_DATA;  /* Signal completion */
    }
    
    return SR_OK;
}

/* Process mixed-signal data using new unified format */
SR_PRIV int jlms_process_mixed_signal_data(const struct sr_dev_inst *sdi,
                                          const uint8_t *buf, size_t len)
{
    struct dev_context *devc = sdi->priv;
    struct sr_datafeed_packet packet;
    struct sr_datafeed_logic logic;
    struct sr_datafeed_analog analog;
    struct sr_analog_encoding encoding;
    struct sr_analog_meaning meaning;
    struct sr_analog_spec spec;
    uint8_t *digital_data = NULL;
    float *analog_data = NULL;
    const uint8_t *ptr = buf;
    size_t remaining = len;
    size_t sample_count = 0;
    GSList *analog_channels = NULL;
    int i;
    
    if (len == 0) {
        return SR_OK;
    }
    
    /* New format: Each sample is either 3 bytes (digital-only) or 32 bytes (mixed/analog) */
    /* We need to parse the data dynamically based on the marker byte */
    
    /* First pass: count samples by examining marker bytes */
    const uint8_t *scan_ptr = ptr;
    size_t scan_remaining = remaining;
    size_t total_samples = 0;
    
    while (scan_remaining >= 3) {
        uint8_t marker = scan_ptr[2];
        
        if (marker == DIGITAL_ONLY_MARKER) {
            /* Digital-only sample: 3 bytes total */
            scan_ptr += 3;
            scan_remaining -= 3;
            total_samples++;
        } else if (marker == MIXED_SIGNAL_MARKER || marker == ANALOG_ONLY_MARKER) {
            /* Mixed-signal or analog-only sample: 32 bytes total (3 + 28 + 1) */
            if (scan_remaining >= 32) {
                /* Verify EOF marker */
                if (scan_ptr[31] == ANALOG_EOF_MARKER) {
                    scan_ptr += 32;
                    scan_remaining -= 32;
                    total_samples++;
                } else {
                    sr_err("Missing EOF marker (0x%02X) at expected position, got 0x%02X", 
                           ANALOG_EOF_MARKER, scan_ptr[31]);
                    break;
                }
            } else {
                sr_warn("Incomplete mixed-signal sample: need 32 bytes, got %zu", scan_remaining);
                break;
            }
        } else {
            sr_warn("Unknown marker byte 0x%02X, stopping parse", marker);
            break;
        }
    }
    
    sample_count = total_samples;
    if (sample_count == 0) {
        return SR_OK;
    }
    
    sr_spew("Processing %zu samples in new unified format", sample_count);
    
    /* Allocate buffers */
    if (devc->num_digital_channels > 0) {
        digital_data = g_malloc(sample_count);
    }
    
    if (devc->num_analog_channels > 0) {
        analog_data = g_malloc(sample_count * devc->num_analog_channels * sizeof(float));
        
        /* Build analog channel list */
        for (GSList *l = sdi->channels; l; l = l->next) {
            struct sr_channel *ch = l->data;
            if (ch->enabled && ch->type == SR_CHANNEL_ANALOG) {
                analog_channels = g_slist_append(analog_channels, ch);
            }
        }
    }
    
    /* Second pass: extract data */
    ptr = buf;
    remaining = len;
    int sample_idx = 0;
    
    while (remaining >= 3 && sample_idx < (int)sample_count) {
        uint8_t gpio_byte = ptr[0];
        uint8_t uart_byte = ptr[1];  /* UART data - currently unused */
        uint8_t marker = ptr[2];
        
        /* Extract digital data */
        if (digital_data) {
            digital_data[sample_idx] = gpio_byte;
        }
        
        ptr += 3;
        remaining -= 3;
        
        /* Handle analog data based on marker */
        if (marker == MIXED_SIGNAL_MARKER || marker == ANALOG_ONLY_MARKER) {
            /* Extract analog data: 14 channels * 2 bytes each = 28 bytes */
            if (remaining >= 29 && analog_data) {  /* 28 analog + 1 EOF */
                int enabled_ch_index = 0;  /* Index for enabled channels only */
                
                /* Process all 14 channels from firmware data stream */
                for (int ch = 0; ch < 14; ch++) {
                    if (remaining >= 2) {
                        uint16_t adc_value = ptr[0] | (ptr[1] << 8);  /* Little-endian */
                        
                        /* Check if this channel is enabled (should only store enabled channel data) */
                        int channel_enabled = 0;
                        if (ch < 8) {
                            /* ADC channels 0-7: check analog mask */
                            channel_enabled = (devc->analog_channel_mask & (1UL << ch)) != 0;
                        } else {
                            /* DAC/INA channels 8-13: check if any analog channels enabled (future expansion) */
                            channel_enabled = 0;  /* For now, only process real ADC channels 0-7 */
                        }
                        
                        /* Only process and store data for enabled channels */
                        if (channel_enabled && enabled_ch_index < devc->num_analog_channels) {
                            /* Convert ADC value to voltage based on channel */
                            float voltage;
                            if (ch < 8) {
                                /* ADC channels 0-7 */
                                voltage = jlms_convert_adc_to_voltage(adc_value, ch);
                            } else if (ch < 10) {
                                /* DAC channels 8-9 (voltage) */
                                voltage = jlms_convert_adc_to_voltage(adc_value, ch - 8);
                            } else {
                                /* INA channels 10-13 (voltage and current) */
                                if ((ch - 10) % 2 == 0) {
                                    /* Voltage measurement */
                                    voltage = jlms_convert_adc_to_voltage(adc_value, 0);  /* Use ADC0 scaling */
                                } else {
                                    /* Current measurement - different scaling */
                                    voltage = ((float)adc_value * 3.3f / 4095.0f) - 1.65f;  /* Example current scaling */
                                }
                            }
                            
                            /* Store in enabled channel index position */
                            analog_data[sample_idx * devc->num_analog_channels + enabled_ch_index] = voltage;
                            enabled_ch_index++;
                            
                            /* Debug for first few samples */
                            if (sample_idx < 3) {
                                sr_spew("Sample %d: Ch%d (enabled_idx=%d) = %.3fV (raw=%d)", 
                                        sample_idx, ch, enabled_ch_index - 1, voltage, adc_value);
                            }
                        }
                        
                        /* Always advance pointer regardless of whether channel is enabled */
                        ptr += 2;
                        remaining -= 2;
                    }
                }
                
                /* Skip EOF marker */
                if (remaining >= 1) {
                    if (*ptr != ANALOG_EOF_MARKER) {
                        sr_warn("Expected EOF marker (0x%02X) but got 0x%02X at sample %d", 
                                ANALOG_EOF_MARKER, *ptr, sample_idx);
                    }
                    ptr++;
                    remaining--;
                }
            }
        }
        
        sample_idx++;
    }
    
    /* Send digital data packet */
    if (digital_data && sample_count > 0 && devc->num_digital_channels > 0) {
        packet.type = SR_DF_LOGIC;
        packet.payload = &logic;
        logic.length = sample_count;
        logic.unitsize = 1;
        logic.data = digital_data;
        sr_session_send(sdi, &packet);
        sr_spew("Sent %zu digital samples (unified format)", sample_count);
    }
    
    /* Send analog data packet */
    if (analog_data && sample_count > 0 && analog_channels && devc->num_analog_channels > 0) {
        sr_analog_init(&analog, &encoding, &meaning, &spec, 2);  /* 2 decimal places */
        encoding.unitsize = sizeof(float);
        encoding.is_signed = FALSE;
        encoding.is_float = TRUE;
        encoding.is_bigendian = FALSE;
        
        meaning.channels = analog_channels;
        meaning.mq = SR_MQ_VOLTAGE;
        meaning.unit = SR_UNIT_VOLT;
        meaning.mqflags = 0;
        
        analog.num_samples = sample_count;
        analog.data = analog_data;
        
        packet.type = SR_DF_ANALOG;
        packet.payload = &analog;
        sr_session_send(sdi, &packet);
        sr_spew("Sent %zu analog samples (%d channels, unified format)", 
                sample_count, devc->num_analog_channels);
    }
    
    /* Cleanup */
    g_free(digital_data);
    g_free(analog_data);
    g_slist_free(analog_channels);
    
    devc->num_samples += sample_count;
    
    /* Check if acquisition is complete */
    if (devc->limit_samples > 0 && devc->num_samples >= devc->limit_samples) {
        sr_info("Mixed-signal acquisition complete (%"PRIu64" samples)", devc->num_samples);
        return SR_ERR_DATA;  /* Signal completion */
    }
    
    return SR_OK;
}

/* Process analog-only data using new unified format */
SR_PRIV int jlms_process_analog_data(const struct sr_dev_inst *sdi,
                                    const uint8_t *buf, size_t len)
{
    /* Analog-only mode uses the same unified format as mixed-signal mode */
    /* The digital bytes are dummy data, but we still process the analog channels */
    return jlms_process_mixed_signal_data(sdi, buf, len);
}

// =============================================================================
// DATA RECEPTION CALLBACK
// =============================================================================

/* Main data reception callback for new unified format */
SR_PRIV int jlms_receive_data(int fd, int revents, void *cb_data)
{
    const struct sr_dev_inst *sdi = cb_data;
    struct dev_context *devc = sdi->priv;
    struct sr_serial_dev_inst *serial = sdi->conn;
    uint8_t buf[LOGIC_BUFSIZE];
    int len;
    int ret = SR_OK;
    
    (void)fd;
    
    if (!(revents & G_IO_IN)) {
        return TRUE;
    }
    
    if (!devc->acquisition_running) {
        return FALSE;
    }
    
    /* Read available data */
    len = serial_read_nonblocking(serial, buf, sizeof(buf));
    if (len < 0) {
        sr_warn("Failed to read data from device, but continuing");
        return TRUE;  /* Continue anyway */
    }
    
    if (len == 0) {
        return TRUE;  /* No data available */
    }
    
    sr_spew("Received %d bytes of data (unified format)", len);
    
    /* JUMPERLESS TOLERANCE MODE: If we detect unified format data, process it regardless of protocol state */
    if (len >= 3) {
        uint8_t marker = buf[2];
        if (marker == 0xDD || marker == 0xDA || marker == 0xAA) {
            sr_info("Detected Jumperless unified format data (marker 0x%02X) - processing regardless of protocol state", marker);
            
            /* Initialize sample buffer if needed */
            if (!devc->sample_buffer) {
                jlms_init_sample_buffer(devc);
            }
            
            /* Process the data */
            ret = jlms_process_buffered_data(sdi, buf, len);
            
            if (ret == SR_ERR_DATA) {
                /* Acquisition complete */
                devc->acquisition_running = FALSE;
                jlms_cleanup_sample_buffer(devc);
                
                struct sr_datafeed_packet packet;
                packet.type = SR_DF_END;
                packet.payload = NULL;
                sr_session_send(sdi, &packet);
                
                sr_info("Jumperless data acquisition complete");
                return FALSE;
            }
            
            return TRUE;  /* Continue receiving */
        }
    }
    
    /* For enhanced protocol, handle structured data reception */
    if (devc->protocol_mode == JUMPERLESS_PROTOCOL_ENHANCED && len > 0) {
        /* Check if this is a data response header (0x81) */
        if (buf[0] == JUMPERLESS_RESP_DATA && len >= 5) {
            /* Parse data header: [0x81][length:4 bytes][data...] */
            uint32_t data_length = 0;
            memcpy(&data_length, &buf[1], 4);  /* Little-endian length */
            
            sr_info("Enhanced data packet: %u bytes expected, %d bytes in current packet", 
                    data_length, len - 5);
            
            /* Initialize sample buffer for this acquisition */
            if (!devc->sample_buffer) {
                jlms_init_sample_buffer(devc);
            }
            
            /* Process data payload if present in this packet */
            if (len > 5) {
                /* Use buffered processor to handle partial samples */
                ret = jlms_process_buffered_data(sdi, &buf[5], len - 5);
                
                if (ret == SR_ERR_DATA) {
                    devc->acquisition_running = FALSE;
                    
                    /* Cleanup sample buffer */
                    jlms_cleanup_sample_buffer(devc);
                    
                    /* Send end packet */
                    struct sr_datafeed_packet packet;
                    packet.type = SR_DF_END;
                    packet.payload = NULL;
                    sr_session_send(sdi, &packet);
                    
                    return FALSE;  /* Acquisition complete */
                }
            }
            return TRUE;  /* Continue receiving */
        }
        
        /* Handle continuation data (raw sample data without header) */
        if (buf[0] != JUMPERLESS_RESP_DATA && buf[0] != JUMPERLESS_RESP_STATUS && 
            buf[0] != JUMPERLESS_RESP_ERROR && buf[0] != JUMPERLESS_RESP_HEADER &&
            buf[0] != JUMPERLESS_RESP_ID && buf[0] != JUMPERLESS_RESP_END_DATA) {
            /* Treat as raw sample data continuation */
            sr_spew("Processing continuation data: %d bytes (buffered unified format)", len);
            
            /* Use buffered processor for all continuation data */
            ret = jlms_process_buffered_data(sdi, buf, len);
            
            if (ret == SR_ERR_DATA) {
                devc->acquisition_running = FALSE;
                
                /* Cleanup sample buffer */
                jlms_cleanup_sample_buffer(devc);
                
                /* Send end packet */
                struct sr_datafeed_packet packet;
                packet.type = SR_DF_END;
                packet.payload = NULL;
                sr_session_send(sdi, &packet);
                
                return FALSE;  /* Acquisition complete */
            }
            return TRUE;
        }
        
        /* Handle end-of-data signal */
        if (buf[0] == JUMPERLESS_RESP_END_DATA) {
            sr_info("Received end-of-data signal from firmware - acquisition complete");
            devc->acquisition_running = FALSE;
            
            /* Cleanup sample buffer */
            jlms_cleanup_sample_buffer(devc);
            
            /* Send end packet */
            struct sr_datafeed_packet packet;
            packet.type = SR_DF_END;
            packet.payload = NULL;
            sr_session_send(sdi, &packet);
            
            return FALSE;  /* Acquisition complete */
        }
        
        /* If we get here, it might be a protocol response - just ignore and continue */
        sr_spew("Ignoring protocol response 0x%02x during data acquisition", buf[0]);
        return TRUE;
    }
    
    /* SUMP mode or fallback: use buffered processor */
    if (!devc->sample_buffer) {
        jlms_init_sample_buffer(devc);
    }
    ret = jlms_process_buffered_data(sdi, buf, len);
    
    /* Check for acquisition completion or errors */
    if (ret == SR_ERR_DATA) {
        /* Acquisition complete */
        devc->acquisition_running = FALSE;
        
        sr_info("Unified format acquisition complete: %"PRIu64" samples captured", devc->num_samples);
        
        /* Send end packet */
        struct sr_datafeed_packet packet;
        packet.type = SR_DF_END;
        packet.payload = NULL;
        sr_session_send(sdi, &packet);
        
        return FALSE;  /* Remove source */
    } else if (ret != SR_OK) {
        /* Error occurred */
        sr_err("Data processing error, stopping acquisition");
        devc->acquisition_running = FALSE;
        return FALSE;
    }
    
    return TRUE;  /* Continue receiving */
}

/* Unified data processor that handles all formats based on marker bytes */
SR_PRIV int jlms_process_unified_data(const struct sr_dev_inst *sdi,
                                     const uint8_t *buf, size_t len)
{
    struct dev_context *devc = sdi->priv;
    struct sr_datafeed_packet packet;
    struct sr_datafeed_logic logic;
    struct sr_datafeed_analog analog;
    struct sr_analog_encoding encoding;
    struct sr_analog_meaning meaning;
    struct sr_analog_spec spec;
    const uint8_t *ptr = buf;
    size_t remaining = len;
    int total_ret = SR_OK;
    
    /* Accumulate samples for batch processing */
    uint8_t *digital_samples = NULL;
    float *analog_samples = NULL;
    size_t digital_count = 0;
    size_t analog_count = 0;
    GSList *analog_channels = NULL;
    
    if (len == 0) {
        return SR_OK;
    }
    
    sr_spew("Processing unified format data: %zu bytes", len);
    
    /* Determine sample type and calculate max samples correctly */
    size_t max_samples;
    if (len >= 3) {
        uint8_t first_marker = buf[2];
        if (first_marker == DIGITAL_ONLY_MARKER) {
            max_samples = len / 3;  /* 3 bytes per digital sample */
        } else if (first_marker == MIXED_SIGNAL_MARKER || first_marker == ANALOG_ONLY_MARKER) {
            max_samples = len / 32;  /* 32 bytes per mixed/analog sample */
        } else {
            sr_err("Unknown format marker 0x%02X in unified data", first_marker);
            return SR_ERR;
        }
    } else {
        sr_err("Insufficient data for unified format processing: %zu bytes", len);
        return SR_ERR;
    }
    
    sr_spew("Calculated max_samples: %zu (based on %zu bytes, marker 0x%02X)", 
            max_samples, len, len >= 3 ? buf[2] : 0);
    
    /* DEBUG: Show channel configuration when processing data */
    sr_spew("Channel counts: digital=%d, analog=%d (enabled channels only)", 
            devc->num_digital_channels, devc->num_analog_channels);
    
    /* Allocate buffers for batch processing with overflow protection */
    if (devc->num_digital_channels > 0) {
        /* Validate digital buffer size before allocation */
        if (max_samples > SIZE_MAX) {
            sr_err("Digital buffer size overflow: %zu samples", max_samples);
            return SR_ERR;
        }
        digital_samples = g_malloc0(max_samples);
        sr_spew("Allocated digital buffer: %zu samples", max_samples);
    }
    
    if (devc->num_analog_channels > 0) {
        /* Validate analog buffer size before allocation to prevent overflow */
        if (max_samples > SIZE_MAX / devc->num_analog_channels ||
            (max_samples * devc->num_analog_channels) > SIZE_MAX / sizeof(float)) {
            sr_err("Analog buffer size overflow: %zu samples × %d channels × %zu bytes = overflow", 
                   max_samples, devc->num_analog_channels, sizeof(float));
            g_free(digital_samples);  /* Cleanup already allocated buffer */
            return SR_ERR;
        }
        
        /* Allocate buffer only for enabled analog channels */
        size_t analog_buffer_size = max_samples * devc->num_analog_channels * sizeof(float);
        analog_samples = g_malloc0(analog_buffer_size);
        sr_spew("Allocated analog buffer: %zu samples × %d enabled channels = %zu floats (%zu bytes)", 
                max_samples, devc->num_analog_channels, max_samples * devc->num_analog_channels, analog_buffer_size);
        
        /* Build analog channel list - only enabled channels */
        for (GSList *l = sdi->channels; l; l = l->next) {
            struct sr_channel *ch = l->data;
            if (ch->enabled && ch->type == SR_CHANNEL_ANALOG) {
                analog_channels = g_slist_append(analog_channels, ch);
                /* Find ADC index by matching channel name with analog_channel_names */
                int adc_index = -1;
                for (int i = 0; i < JUMPERLESS_MAX_ANALOG_CHANNELS; i++) {
                    if (strcmp(ch->name, analog_channel_names[i]) == 0) {
                        adc_index = i;
                        break;
                    }
                }
                sr_spew("Added enabled analog channel %s (ADC %d)", ch->name, adc_index);
            }
        }
        
        sr_spew("Built analog channel list: %d enabled channels", g_slist_length(analog_channels));
    }
    
    /* Process data by examining marker bytes */
    while (remaining >= 3) {
        uint8_t gpio_byte = ptr[0];
        uint8_t uart_byte = ptr[1];  /* Currently unused */
        uint8_t marker = ptr[2];
        
        if (marker == DIGITAL_ONLY_MARKER) {
            /* Digital-only sample: 3 bytes */
            if (digital_samples) {
                digital_samples[digital_count] = gpio_byte;
                digital_count++;
            }
            ptr += 3;
            remaining -= 3;
            
        } else if (marker == MIXED_SIGNAL_MARKER || marker == ANALOG_ONLY_MARKER) {
            /* Mixed-signal or analog-only sample: 32 bytes (3 + 28 + 1) */
            if (remaining >= 32) {
                /* Verify EOF marker at position 31 */
                if (ptr[31] != ANALOG_EOF_MARKER) {
                    sr_err("Missing EOF marker (0x%02X) at position 31, got 0x%02X", 
                           ANALOG_EOF_MARKER, ptr[31]);
                    break;
                }
                
                /* Store digital data */
                if (digital_samples) {
                    digital_samples[digital_count] = gpio_byte;
                    digital_count++;
                }
                
                /* Store analog data */
                if (analog_samples) {
                    const uint8_t *analog_ptr = ptr + 3;  /* Skip 3-byte header */
                    
                    /* Unified format: firmware always sends 14 channels × 2 bytes = 28 bytes */
                    /* Parse all 14 channels but only store enabled ones */
                    int enabled_ch_index = 0;
                    
                    for (int fw_ch = 0; fw_ch < 14; fw_ch++) {
                        uint16_t adc_value = analog_ptr[0] | (analog_ptr[1] << 8);  /* Little-endian */
                        
                        /* Check if this firmware channel is enabled */
                        gboolean is_enabled = FALSE;
                        for (GSList *ch_list = analog_channels; ch_list; ch_list = ch_list->next) {
                            struct sr_channel *ch = ch_list->data;
                            /* Find ADC index by matching channel name with analog_channel_names */
                            int adc_index = -1;
                            for (int i = 0; i < JUMPERLESS_MAX_ANALOG_CHANNELS; i++) {
                                if (strcmp(ch->name, analog_channel_names[i]) == 0) {
                                    adc_index = i;
                                    break;
                                }
                            }
                            if (adc_index == fw_ch) {
                                is_enabled = TRUE;
                                break;
                            }
                        }
                        
                        /* Only store enabled channels */
                        if (is_enabled && enabled_ch_index < devc->num_analog_channels) {
                            /* Convert ADC value to voltage */
                            float voltage = jlms_convert_adc_to_voltage(adc_value, fw_ch);
                            
                            /* Store in buffer for enabled channels only */
                            analog_samples[analog_count * devc->num_analog_channels + enabled_ch_index] = voltage;
                            enabled_ch_index++;
                        }
                        
                        /* Always advance to next firmware channel (2 bytes) */
                        analog_ptr += 2;
                    }
                    analog_count++;
                }
                
                ptr += 32;
                remaining -= 32;
                
            } else {
                sr_warn("Incomplete mixed-signal sample: need 32 bytes, got %zu", remaining);
                break;
            }
        } else {
            /* Unknown marker - could be noise or protocol error */
            sr_warn("Unknown format marker 0x%02X, skipping byte", marker);
            ptr++;
            remaining--;
        }
    }
    
    /* Send accumulated digital data */
    if (digital_samples && digital_count > 0 && devc->num_digital_channels > 0) {
        packet.type = SR_DF_LOGIC;
        packet.payload = &logic;
        logic.length = digital_count;
        logic.unitsize = 1;
        logic.data = digital_samples;
        sr_session_send(sdi, &packet);
        sr_spew("Sent %zu digital samples (unified format batch)", digital_count);
    }
    
    /* Send accumulated analog data */
    if (analog_samples && analog_count > 0 && analog_channels && devc->num_analog_channels > 0) {
        int enabled_analog_count = g_slist_length(analog_channels);
        
        sr_analog_init(&analog, &encoding, &meaning, &spec, 2);  /* 2 decimal places */
        encoding.unitsize = sizeof(float);
        encoding.is_signed = FALSE;
        encoding.is_float = TRUE;
        encoding.is_bigendian = FALSE;
        
        meaning.channels = analog_channels;
        meaning.mq = SR_MQ_VOLTAGE;
        meaning.unit = SR_UNIT_VOLT;
        meaning.mqflags = 0;
        
        analog.num_samples = analog_count;
        analog.data = analog_samples;
        
        packet.type = SR_DF_ANALOG;
        packet.payload = &analog;
        sr_session_send(sdi, &packet);
        sr_spew("Sent %zu analog samples (%d enabled channels, unified format batch)", 
                analog_count, enabled_analog_count);
    }
    
    /* Update sample count */
    size_t total_samples = (digital_count > analog_count) ? digital_count : analog_count;
    devc->num_samples += total_samples;
    
    /* Check for acquisition completion */
    if (devc->limit_samples > 0 && devc->num_samples >= devc->limit_samples) {
        sr_info("Unified format acquisition complete (%"PRIu64" samples)", devc->num_samples);
        total_ret = SR_ERR_DATA;  /* Signal completion */
    }
    
    /* Cleanup */
    g_free(digital_samples);
    g_free(analog_samples);
    g_slist_free(analog_channels);
    
    /* Log any remaining bytes that couldn't be processed */
    if (remaining > 0) {
        sr_spew("Unified format processing: %zu bytes remaining (partial sample)", remaining);
    }
    
    return total_ret;
}

/* Buffered data processor that handles partial samples across packet boundaries */
SR_PRIV int jlms_process_buffered_data(const struct sr_dev_inst *sdi,
                                      const uint8_t *buf, size_t len)
{
    struct dev_context *devc = sdi->priv;
    const uint8_t *input_ptr = buf;
    size_t input_remaining = len;
    int total_ret = SR_OK;
    
    if (len == 0) {
        return SR_OK;
    }
    
    sr_spew("Processing buffered data: %zu bytes (buffer fill: %zu/%zu)", 
            len, devc->sample_buffer_fill, devc->sample_buffer_size);
    
    /* Initialize buffers if needed */
    if (!devc->sample_buffer) {
        /* Initialize sample buffers using the safe initialization function */
        jlms_init_sample_buffer(devc);
    }
    
    /* Process input data */
    while (input_remaining > 0) {
        /* If we have no partial sample, determine sample size from marker */
        if (devc->sample_buffer_fill == 0) {
            /* Need at least 3 bytes to determine sample type */
            if (input_remaining < 3) {
                /* Buffer this partial header */
                size_t copy_size = input_remaining;
                size_t required_size = devc->sample_buffer_fill + copy_size;
                if (required_size > devc->sample_buffer_size) {
                    /* Expand sample buffer to accommodate more data */
                    size_t new_size = required_size + 1024;  /* Add 1KB buffer for future expansion */
                    uint8_t *new_buffer = g_realloc(devc->sample_buffer, new_size);
                    if (!new_buffer) {
                        sr_err("Failed to expand sample buffer for header to %zu bytes", new_size);
                        return SR_ERR;
                    }
                    devc->sample_buffer = new_buffer;
                    devc->sample_buffer_size = new_size;
                    sr_spew("Expanded sample buffer for header to %zu bytes", new_size);
                }
                memcpy(devc->sample_buffer + devc->sample_buffer_fill, input_ptr, copy_size);
                devc->sample_buffer_fill += copy_size;
                break;
            }
            
            /* Determine sample size from marker */
            uint8_t marker = input_ptr[2];
            if (marker == DIGITAL_ONLY_MARKER) {
                devc->expected_sample_size = 3;
            } else if (marker == MIXED_SIGNAL_MARKER || marker == ANALOG_ONLY_MARKER) {
                devc->expected_sample_size = 32;
            } else {
                /* Unknown marker - skip this byte and try again */
                sr_warn("Unknown format marker 0x%02X in buffered data, skipping", marker);
                input_ptr++;
                input_remaining--;
                continue;
            }
        }
        
        /* Calculate how much data we need to complete current sample */
        size_t bytes_needed = devc->expected_sample_size - devc->sample_buffer_fill;
        size_t bytes_to_copy = (input_remaining < bytes_needed) ? input_remaining : bytes_needed;
        
        /* Copy data to sample buffer - expand buffer if needed */
        size_t required_size = devc->sample_buffer_fill + bytes_to_copy;
        if (required_size > devc->sample_buffer_size) {
            /* Expand sample buffer to accommodate more data */
            size_t new_size = required_size + 1024;  /* Add 1KB buffer for future expansion */
            uint8_t *new_buffer = g_realloc(devc->sample_buffer, new_size);
            if (!new_buffer) {
                sr_err("Failed to expand sample buffer to %zu bytes", new_size);
                return SR_ERR;
            }
            devc->sample_buffer = new_buffer;
            devc->sample_buffer_size = new_size;
            sr_spew("Expanded sample buffer to %zu bytes", new_size);
        }
        
        memcpy(devc->sample_buffer + devc->sample_buffer_fill, input_ptr, bytes_to_copy);
        devc->sample_buffer_fill += bytes_to_copy;
        input_ptr += bytes_to_copy;
        input_remaining -= bytes_to_copy;
        
        /* If we have a complete sample, accumulate it for batch processing */
        if (devc->sample_buffer_fill == devc->expected_sample_size) {
            /* Verify sample integrity for mixed-signal samples */
            if (devc->expected_sample_size == 32) {
                uint8_t marker = devc->sample_buffer[2];
                uint8_t eof_marker = devc->sample_buffer[31];
                
                if ((marker == MIXED_SIGNAL_MARKER || marker == ANALOG_ONLY_MARKER) && 
                    eof_marker != ANALOG_EOF_MARKER) {
                    sr_warn("Invalid EOF marker in complete sample: expected 0x%02X, got 0x%02X", 
                           ANALOG_EOF_MARKER, eof_marker);
                }
            }
            
            /* Add complete sample to accumulation buffer */
            if (devc->accumulated_samples_fill + devc->expected_sample_size > devc->accumulated_samples_size) {
                /* Try to process accumulated samples first */
                int ret = jlms_process_accumulated_samples(sdi);
                if (ret != SR_OK) {
                    total_ret = ret;
                    break;
                }
                
                /* If buffer is still too small after processing, expand it */
                if (devc->accumulated_samples_fill + devc->expected_sample_size > devc->accumulated_samples_size) {
                    size_t new_size = devc->accumulated_samples_size * 2;  /* Double the size */
                    if (new_size < devc->accumulated_samples_fill + devc->expected_sample_size + 4096) {
                        new_size = devc->accumulated_samples_fill + devc->expected_sample_size + 4096;
                    }
                    
                    uint8_t *new_buffer = g_realloc(devc->accumulated_samples, new_size);
                    if (!new_buffer) {
                        sr_err("Failed to expand accumulation buffer to %zu bytes", new_size);
                        total_ret = SR_ERR;
                        break;
                    }
                    devc->accumulated_samples = new_buffer;
                    devc->accumulated_samples_size = new_size;
                    sr_spew("Expanded accumulation buffer to %zu bytes", new_size);
                }
            }
            
            /* Copy complete sample to accumulation buffer */
            memcpy(devc->accumulated_samples + devc->accumulated_samples_fill, 
                   devc->sample_buffer, devc->expected_sample_size);
            devc->accumulated_samples_fill += devc->expected_sample_size;
            devc->accumulated_sample_count++;
            
            sr_spew("Accumulated sample %zu (%zu bytes), buffer now %zu/%zu bytes", 
                    devc->accumulated_sample_count, devc->expected_sample_size,
                    devc->accumulated_samples_fill, devc->accumulated_samples_size);
            
            /* Reset partial sample buffer for next sample */
            devc->sample_buffer_fill = 0;
            devc->expected_sample_size = 0;
        }
    }
    
    /* Check if we should process accumulated samples (threshold or completion) */
    if (devc->accumulated_sample_count >= 10 || /* Process every 10 samples (increased from 50) */
        (devc->limit_samples > 0 && devc->num_samples + devc->accumulated_sample_count >= devc->limit_samples)) {
        int ret = jlms_process_accumulated_samples(sdi);
        if (ret != SR_OK) {
            total_ret = ret;
        }
    }
    
    sr_spew("Buffered processing complete: %zu samples accumulated, buffer fill now %zu bytes", 
            devc->accumulated_sample_count, devc->sample_buffer_fill);
    
    return total_ret;
}

/* Process accumulated complete samples in batches */
SR_PRIV int jlms_process_accumulated_samples(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    int ret = SR_OK;
    
    if (devc->accumulated_sample_count == 0) {
        return SR_OK;
    }
    
    sr_spew("Processing %zu accumulated samples (%zu bytes) in batch", 
            devc->accumulated_sample_count, devc->accumulated_samples_fill);
    
    /* Process the accumulated samples using the unified processor */
    ret = jlms_process_unified_data(sdi, devc->accumulated_samples, devc->accumulated_samples_fill);
    
    /* Reset accumulation buffer */
    devc->accumulated_samples_fill = 0;
    devc->accumulated_sample_count = 0;
    
    return ret;
}

/* Initialize sample buffer */
SR_PRIV void jlms_init_sample_buffer(struct dev_context *devc)
{
    if (devc->sample_buffer) {
        g_free(devc->sample_buffer);
    }
    if (devc->accumulated_samples) {
        g_free(devc->accumulated_samples);
    }
    
    /* Use larger buffer size to handle largest possible sample (32 bytes for mixed-signal) */
    devc->sample_buffer_size = 64;  /* Sufficient for 32-byte sample + safety margin */
    devc->sample_buffer = g_malloc(devc->sample_buffer_size);
    devc->sample_buffer_fill = 0;
    devc->expected_sample_size = 0;
    
    /* Calculate accumulation buffer size based on acquisition parameters */
    /* Use minimum of 64KB or enough space for 1000 samples of largest size (32 bytes) */
    size_t min_accumulation_size = 1000 * 32;  /* 1000 samples × 32 bytes = 32KB */
    size_t max_accumulation_size = 262144;     /* 256KB maximum (increased from 64KB) */
    
    /* If we know the sample limit, optimize buffer size */
    if (devc->limit_samples > 0) {
        size_t needed_size = devc->limit_samples * 32;  /* Worst case: all 32-byte samples */
        if (needed_size < max_accumulation_size) {
            devc->accumulated_samples_size = needed_size;
        } else {
            devc->accumulated_samples_size = max_accumulation_size;
        }
    } else {
        devc->accumulated_samples_size = min_accumulation_size;
    }
    
    /* Ensure minimum size */
    if (devc->accumulated_samples_size < min_accumulation_size) {
        devc->accumulated_samples_size = min_accumulation_size;
    }
    
    devc->accumulated_samples = g_malloc(devc->accumulated_samples_size);
    devc->accumulated_samples_fill = 0;
    devc->accumulated_sample_count = 0;
    
    sr_spew("Sample buffers initialized: %zu bytes partial, %zu bytes accumulation (optimized for %"PRIu64" samples)", 
            devc->sample_buffer_size, devc->accumulated_samples_size, devc->limit_samples);
}

/* Cleanup sample buffer */
SR_PRIV void jlms_cleanup_sample_buffer(struct dev_context *devc)
{
    /* Process any remaining accumulated samples before cleanup */
    if (devc->accumulated_sample_count > 0) {
        sr_spew("Processing %zu remaining samples before cleanup", devc->accumulated_sample_count);
        /* Note: We can't call jlms_process_accumulated_samples here because we don't have sdi */
        /* This will be handled by the acquisition cleanup sequence */
    }
    
    if (devc->sample_buffer) {
        g_free(devc->sample_buffer);
        devc->sample_buffer = NULL;
    }
    
    if (devc->accumulated_samples) {
        g_free(devc->accumulated_samples);
        devc->accumulated_samples = NULL;
    }
    
    devc->sample_buffer_size = 0;
    devc->sample_buffer_fill = 0;
    devc->expected_sample_size = 0;
    
    devc->accumulated_samples_size = 0;
    devc->accumulated_samples_fill = 0;
    devc->accumulated_sample_count = 0;
    
    sr_spew("Sample buffers cleaned up");
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

/* Calculate actual sample rate from requested rate */
SR_PRIV int jlms_calculate_sample_rate(uint64_t requested_rate, uint64_t *actual_rate)
{
    if (!actual_rate) {
        return SR_ERR_ARG;
    }
    
    /* Clamp to hardware limits */
    if (requested_rate > JUMPERLESS_MAX_SAMPLE_RATE) {
        *actual_rate = JUMPERLESS_MAX_SAMPLE_RATE;
    } else if (requested_rate < JUMPERLESS_MIN_SAMPLE_RATE) {
        *actual_rate = JUMPERLESS_MIN_SAMPLE_RATE;
    } else {
        *actual_rate = requested_rate;
    }
    
    return SR_OK;
}

/* Calculate actual sample count from requested count */
SR_PRIV int jlms_calculate_sample_count(uint64_t requested_count, uint64_t *actual_count)
{
    if (!actual_count) {
        return SR_ERR_ARG;
    }
    
    /* Clamp to reasonable limits */
    if (requested_count > MAX_NUM_SAMPLES) {
        *actual_count = MAX_NUM_SAMPLES;
    } else if (requested_count < MIN_NUM_SAMPLES) {
        *actual_count = MIN_NUM_SAMPLES;
    } else {
        *actual_count = requested_count;
    }
    
    return SR_OK;
}

/* Convert ADC value to voltage based on Jumperless hardware specifications */
float jlms_convert_adc_to_voltage(uint16_t adc_value, uint8_t channel)
{
    /* Jumperless ADC voltage conversion:
     * - Most channels: ±9.14V range (18.28V total spread)
     * - 12-bit ADC: 0-4095 range
     * - Formula: voltage = (adc_value * 18.28 / 4095) - 9.14
     */
    
    if (adc_value > 4095) {
        /* Invalid ADC value - clamp to max */
        adc_value = 4095;
    }
    
    /* Standard conversion for Jumperless (±9.14V range) */
    float voltage = ((float)adc_value * 18.28f / 4095.0f) - 9.14f;
    
    /* Sanity check - clamp to reasonable voltage range */
    if (voltage < -20.0f) voltage = -20.0f;
    if (voltage > 20.0f) voltage = 20.0f;
    
    return voltage;
}

/* Calculate simple XOR checksum */
SR_PRIV uint32_t jlms_calculate_checksum(const void *data, size_t len)
{
    const uint8_t *bytes = data;
    uint32_t checksum = 0;
    size_t i;
    
    for (i = 0; i < len; i++) {
        checksum ^= bytes[i];
    }
    
    return checksum;
}

// =============================================================================
// DEVICE MANAGEMENT
// =============================================================================

/* Reset device context to initial state */
SR_PRIV void jlms_reset_device_context(struct dev_context *devc)
{
    if (!devc) {
        sr_warn("jlms_reset_device_context called with NULL context");
        return;
    }
    
    /* Stop any ongoing acquisition */
    devc->acquisition_running = FALSE;
    devc->device_armed = FALSE;
    
    /* Reset protocol state - important for reconnection */
    devc->protocol_mode = JUMPERLESS_PROTOCOL_ENHANCED;  /* Jumperless only supports Enhanced */
    devc->protocol_detected = FALSE;
    devc->device_identified = FALSE;
    devc->header_received = FALSE;
    devc->device_configured = FALSE;
    
    /* Clean up device ID string */
    if (devc->device_id) {
        g_free(devc->device_id);
        devc->device_id = NULL;
    }
    
    /* Reset device capabilities to safe defaults */
    devc->device_version = 0;
    devc->device_capture_mode = JUMPERLESS_MODE_MIXED_SIGNAL;  /* Default for Jumperless */
    devc->max_digital_channels = JUMPERLESS_MAX_DIGITAL_CHANNELS;
    devc->max_analog_channels = JUMPERLESS_MAX_ANALOG_CHANNELS; /* Default to 5 */
    devc->max_sample_rate = JUMPERLESS_MAX_SAMPLE_RATE;
    devc->max_memory_depth = MAX_NUM_SAMPLES;
    devc->supports_triggers = FALSE;
    devc->supports_compression = FALSE;
    devc->adc_resolution_bits = 12;
    
    /* Reset channel configuration */
    devc->digital_channel_mask = 0;
    devc->analog_channel_mask = 0;
    devc->num_digital_channels = 0;
    devc->num_analog_channels = 0;
    devc->bytes_per_sample = 0;
    
    /* Reset sample counting */
    devc->num_samples = 0;
    
    /* Clean up sample buffers safely */
    if (devc->raw_sample_buf) {
        g_free(devc->raw_sample_buf);
        devc->raw_sample_buf = NULL;
        devc->raw_sample_buf_size = 0;
    }
    
    /* Cleanup sample buffer structures */
    jlms_cleanup_sample_buffer(devc);
    
    sr_dbg("Device context reset to clean state for reconnection");
}

/* Cleanup acquisition resources */
SR_PRIV void jlms_cleanup_acquisition(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    struct sr_serial_dev_inst *serial = sdi->conn;
    
    if (devc->acquisition_running) {
        devc->acquisition_running = FALSE;
        
        /* Process any remaining accumulated samples before cleanup */
        if (devc->accumulated_sample_count > 0) {
            sr_spew("Processing %zu remaining accumulated samples before cleanup", 
                    devc->accumulated_sample_count);
            jlms_process_accumulated_samples(sdi);
        }
        
        /* Remove serial data source */
        serial_source_remove(sdi->session, serial);
        
        /* Cleanup sample buffer */
        jlms_cleanup_sample_buffer(devc);
        
        sr_info("Acquisition cleanup completed");
    }
}

/* Abort ongoing acquisition */
SR_PRIV int jlms_abort_acquisition(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    struct sr_serial_dev_inst *serial = sdi->conn;
    
    if (devc->acquisition_running) {
        sr_info("Aborting acquisition...");
        
        /* Send abort/reset command */
        jlms_send_command(serial, JUMPERLESS_CMD_RESET, NULL, 0);
        
        /* Cleanup */
        jlms_cleanup_acquisition(sdi);
        
        devc->device_armed = FALSE;
        
        sr_info("Acquisition aborted");
    }
    
    return SR_OK;
}

// =============================================================================
// DEBUG AND DIAGNOSTICS
// =============================================================================

/* Log device information */
SR_PRIV void jlms_log_device_info(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    
    sr_info("=== Device Information ===");
    sr_info("Device ID: %s", devc->device_id ? devc->device_id : "Unknown");
    sr_info("Protocol: %s", devc->protocol_mode == JUMPERLESS_PROTOCOL_SUMP ? "SUMP" : "Enhanced");
    sr_info("Version: %u", devc->device_version);
    sr_info("Capture Mode: %u", devc->device_capture_mode);
    sr_info("Max Channels: %u digital + %u analog", devc->max_digital_channels, devc->max_analog_channels);
    sr_info("Max Sample Rate: %"PRIu64" Hz", devc->max_sample_rate);
    sr_info("Max Memory Depth: %"PRIu64" samples", devc->max_memory_depth);
    sr_info("Features: triggers=%s, compression=%s", 
            devc->supports_triggers ? "yes" : "no",
            devc->supports_compression ? "yes" : "no");
    sr_info("ADC Resolution: %u bits", devc->adc_resolution_bits);
}

/* Log channel configuration */
SR_PRIV void jlms_log_channel_config(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    
    sr_info("=== Channel Configuration ===");
    sr_info("Digital Mask: 0x%02x (%u channels)", devc->digital_channel_mask, devc->num_digital_channels);
    sr_info("Analog Mask: 0x%02x (%u channels)", devc->analog_channel_mask, devc->num_analog_channels);
    sr_info("Bytes per Sample: %u", devc->bytes_per_sample);
}

/* Log timing configuration */
SR_PRIV void jlms_log_timing_config(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    
    sr_info("=== Timing Configuration ===");
    sr_info("Sample Rate: %"PRIu64" Hz", devc->cur_samplerate);
    sr_info("Sample Count: %"PRIu64, devc->limit_samples);
    sr_info("Duration: %.3f ms", (double)devc->limit_samples * 1000.0 / devc->cur_samplerate);
}

// =============================================================================
// USB DEVICE MANAGEMENT
// =============================================================================

/* Check if USB device is present at the given path */
SR_PRIV gboolean jlms_check_usb_device_present(const char *device_path)
{
    struct stat st;
    
    if (!device_path) {
        sr_dbg("USB check: device_path is NULL");
        return FALSE;
    }
    
    /* Check if device file exists and is accessible */
    if (stat(device_path, &st) == 0) {
        /* Device file exists, check if it's actually responsive */
        int fd = open(device_path, O_RDWR | O_NONBLOCK);
        if (fd >= 0) {
            close(fd);
            sr_dbg("USB check: Device %s is present and accessible", device_path);
            return TRUE;
        } else {
            sr_dbg("USB check: Device %s exists but not accessible: %s", 
                   device_path, strerror(errno));
            return FALSE;
        }
    } else {
        sr_dbg("USB check: Device %s not found: %s", device_path, strerror(errno));
        return FALSE;
    }
}

/* Check if an error code indicates a recoverable device error */
SR_PRIV gboolean jlms_is_device_error_recoverable(int error_code)
{
    /* Recoverable errors that might indicate temporary USB disconnection */
    switch (error_code) {
    case ENXIO:     /* No such device or address */
    case ENODEV:    /* No such device */
    case ENOTTY:    /* Not a typewriter (device disappeared) */
    case ENOENT:    /* No such file or directory */
    case EIO:       /* I/O error */
    case EPIPE:     /* Broken pipe */
    case ECONNRESET: /* Connection reset by peer */
        return TRUE;
    default:
        return FALSE;
    }
}

/* Attempt to reconnect to a disconnected device */
SR_PRIV int jlms_attempt_device_reconnection(struct sr_dev_inst *sdi)
{
    struct dev_context *devc;
    struct sr_serial_dev_inst *serial;
    const char *device_path;
    int retry_count = 0;
    const int max_retries = 5;
    const int retry_delay_ms = 1000; /* 1 second between retries */
    
    if (!sdi || !sdi->priv || !sdi->conn) {
        sr_err("Reconnection: Invalid device instance");
        return SR_ERR_ARG;
    }
    
    devc = sdi->priv;
    serial = sdi->conn;
    device_path = serial->port;
    
    sr_info("Attempting to reconnect to device %s...", device_path);
    
    /* Close any existing connection first */
    if (sdi->status == SR_ST_ACTIVE) {
        sr_dbg("Reconnection: Closing existing connection");
        serial_close(serial);
        sdi->status = SR_ST_INACTIVE;
    }
    
    /* Reset device context to clean state */
    jlms_reset_device_context(devc);
    
    /* Try to reconnect with retries */
    for (retry_count = 0; retry_count < max_retries; retry_count++) {
        sr_dbg("Reconnection attempt %d/%d...", retry_count + 1, max_retries);
        
        /* Check if USB device is physically present */
        if (!jlms_check_usb_device_present(device_path)) {
            sr_dbg("USB device not present, waiting...");
            g_usleep(retry_delay_ms * 1000);
            continue;
        }
        
        /* Try to open the serial connection */
        int ret = serial_open(serial, SERIAL_RDWR);
        if (ret != SR_OK) {
            sr_dbg("Reconnection: Failed to open serial connection (attempt %d): %d", 
                   retry_count + 1, ret);
            g_usleep(retry_delay_ms * 1000);
            continue;
        }
        
        /* Set the device as active */
        sdi->status = SR_ST_ACTIVE;
        
        /* Try to identify the device */
        ret = jlms_identify_device(sdi);
        if (ret != SR_OK) {
            sr_dbg("Reconnection: Device identification failed (attempt %d)", retry_count + 1);
            serial_close(serial);
            sdi->status = SR_ST_INACTIVE;
            g_usleep(retry_delay_ms * 1000);
            continue;
        }
        
        /* Device successfully reconnected */
        sr_info("Device %s successfully reconnected after %d attempts", 
                device_path, retry_count + 1);
        return SR_OK;
    }
    
    sr_err("Failed to reconnect to device %s after %d attempts", device_path, max_retries);
    return SR_ERR;
}

// =============================================================================
// PROTOCOL HANDLERS
// =============================================================================
