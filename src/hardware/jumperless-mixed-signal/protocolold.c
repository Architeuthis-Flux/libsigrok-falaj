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

#include "config.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"


//This is the old protocol for that works!

/* Send command to Jumperless device */
SR_PRIV int jumperless_send_command(struct sr_serial_dev_inst *serial, 
                                   uint8_t command, const void *data, size_t len)
{
    uint8_t cmd_buf[256];
    size_t total_len = 1 + len;
    
    if (total_len > sizeof(cmd_buf)) {
        sr_err("Command payload too large: %zu bytes", len);
        return SR_ERR_ARG;
    }
    
    cmd_buf[0] = command;
    if (data && len > 0) {
        memcpy(cmd_buf + 1, data, len);
    }
    
    int ret = serial_write_blocking(serial, cmd_buf, total_len, 1000);
    if (ret < 0) {
        sr_err("Failed to send command 0x%02x: %d", command, ret);
        return SR_ERR;
    }
    
    sr_dbg("Sent command 0x%02x with %zu bytes payload", command, len);
    return SR_OK;
}

/* Wait for specific response from Jumperless device */
SR_PRIV int jumperless_wait_for_response(struct sr_serial_dev_inst *serial, 
                                        uint8_t expected_response, 
                                        uint8_t *buf, size_t buf_size, 
                                        size_t *received_len, int timeout_ms)
{
    uint8_t response_header;
    int ret;
    
    /* Read response header */
    ret = serial_read_blocking(serial, &response_header, 1, timeout_ms);
    if (ret != 1) {
        sr_err("Failed to read response header: %d", ret);
        return SR_ERR;
    }
    
    if (response_header != expected_response) {
        sr_err("Unexpected response: got 0x%02x, expected 0x%02x", 
               response_header, expected_response);
        if (response_header == JUMPERLESS_RESP_ERROR) {
            uint8_t error_code;
            if (serial_read_blocking(serial, &error_code, 1, 100) == 1) {
                sr_err("Device error code: 0x%02x", error_code);
            }
        }
        return SR_ERR;
    }
    
    /* Read payload length */
    uint16_t payload_len;
    ret = serial_read_blocking(serial, &payload_len, sizeof(payload_len), timeout_ms);
    if (ret != sizeof(payload_len)) {
        sr_err("Failed to read payload length: %d", ret);
        return SR_ERR;
    }
    
    if (payload_len > buf_size) {
        sr_err("Payload too large: %u bytes, buffer size %zu", payload_len, buf_size);
        return SR_ERR;
    }
    
    /* Read payload */
    if (payload_len > 0) {
        ret = serial_read_blocking(serial, buf, payload_len, timeout_ms);
        if (ret != (int)payload_len) {
            sr_err("Failed to read complete payload: got %d, expected %u", ret, payload_len);
            return SR_ERR;
        }
    }
    
    if (received_len) {
        *received_len = payload_len;
    }
    
    sr_dbg("Received response 0x%02x with %u bytes payload", expected_response, payload_len);
    return SR_OK;
}

/* Receive and parse device header */
SR_PRIV int jumperless_receive_header(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    struct sr_serial_dev_inst *serial = sdi->conn;
    uint8_t header_buf[sizeof(jumperless_header)];
    size_t header_len;
    jumperless_header header;
    
    /* Request header from device */
    int ret = jumperless_send_command(serial, JUMPERLESS_CMD_GET_HEADER, NULL, 0);
    if (ret != SR_OK) {
        return ret;
    }
    
    /* Wait for header response */
    ret = jumperless_wait_for_response(serial, JUMPERLESS_RESP_HEADER, 
                                      header_buf, sizeof(header_buf), 
                                      &header_len, 2000);
    if (ret != SR_OK) {
        sr_err("Failed to receive device header");
        return ret;
    }
    
    if (header_len < sizeof(jumperless_header)) {
        sr_err("Header too short: %zu bytes, expected %zu", header_len, sizeof(jumperless_header));
        return SR_ERR;
    }
    
    /* Parse header */
    memcpy(&header, header_buf, sizeof(header));
    
    /* Verify magic string */
    if (strncmp(header.magic, JUMPERLESS_HEADER_MAGIC, strlen(JUMPERLESS_HEADER_MAGIC)) != 0) {
        sr_err("Invalid header magic");
        return SR_ERR;
    }
    
    /* Update device context with header information */
    devc->bytes_per_sample = header.bytes_per_sample;
    devc->digital_bytes_per_sample = header.digital_bytes_per_sample;
    devc->analog_bytes_per_sample = header.analog_bytes_per_sample;
    devc->analog_resolution = header.analog_resolution;
    devc->analog_voltage_range = header.analog_voltage_range;
    devc->max_sample_rate = header.max_sample_rate;
    devc->max_samples = header.max_samples;
    devc->supported_modes = header.supported_modes;
    devc->num_analog_channels = header.num_analog_channels;
    
    strncpy(devc->firmware_version, header.firmware_version, sizeof(devc->firmware_version) - 1);
    strncpy(devc->device_id, header.device_id, sizeof(devc->device_id) - 1);
    
    devc->header_received = TRUE;
    
    sr_info("Jumperless %s connected, firmware %s", devc->device_id, devc->firmware_version);
    sr_info("Device capabilities: %u Hz max rate, %u max samples", 
            devc->max_sample_rate, devc->max_samples);
    
    return SR_OK;
}

/* Configure device channels */
SR_PRIV int jumperless_configure_channels(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    struct sr_serial_dev_inst *serial = sdi->conn;
    GSList *l;
    struct sr_channel *ch;
    
    /* Reset channel masks */
    devc->digital_channel_mask = 0;
    devc->analog_channel_mask = 0;
    devc->capture_mode = JUMPERLESS_MODE_DIGITAL_ONLY;
    
    /* Build channel masks from enabled channels */
    for (l = sdi->channels; l; l = l->next) {
        ch = l->data;
        
        /* Debug: Log all channels and their enable status */
        sr_dbg("Channel: %s (index=%d, type=%s, enabled=%s)", 
               ch->name, ch->index,
               (ch->type == SR_CHANNEL_LOGIC) ? "LOGIC" : "ANALOG",
               ch->enabled ? "YES" : "NO");
        
        if (!ch->enabled) {
            continue;
        }
        
        if (ch->type == SR_CHANNEL_LOGIC) {
            devc->digital_channel_mask |= (1 << ch->index);
            sr_dbg("Digital channel %d enabled, mask now: 0x%08X", ch->index, devc->digital_channel_mask);
        } else if (ch->type == SR_CHANNEL_ANALOG) {
            /* Analog channels start after digital channels - convert to ADC index */
            int adc_index = ch->index - JUMPERLESS_MAX_DIGITAL_CHANNELS;
            sr_dbg("Analog channel %d (original index %d) - adc_index calculated as %d", 
                   ch->index - JUMPERLESS_MAX_DIGITAL_CHANNELS, ch->index, adc_index);
            
            if (adc_index >= 0 && adc_index < JUMPERLESS_MAX_ANALOG_CHANNELS) {
                devc->analog_channel_mask |= (1 << adc_index);
                sr_dbg("Analog channel enabled, adc_index=%d, mask now: 0x%08X", 
                       adc_index, devc->analog_channel_mask);
            } else {
                sr_warn("Analog channel index out of range: adc_index=%d", adc_index);
            }
        }
    }
    
    /* Final debug output */
    sr_info("Channel configuration complete: digital_mask=0x%08X, analog_mask=0x%08X, mode=%d",
            devc->digital_channel_mask, devc->analog_channel_mask, devc->capture_mode);
    
    /* TEMPORARY TEST: Force analog channels on to test data path */
    if (devc->analog_channel_mask == 0) {
        sr_warn("TEMPORARY TEST: Forcing analog channels 0,1 ON for testing");
        devc->analog_channel_mask = 0x03;  // Force ADC 0,1 on
    }
    
    /* Determine capture mode */
    if (devc->digital_channel_mask && devc->analog_channel_mask) {
        devc->capture_mode = JUMPERLESS_MODE_MIXED_SIGNAL;
    } else if (devc->analog_channel_mask) {
        devc->capture_mode = JUMPERLESS_MODE_ANALOG_ONLY;
    } else {
        devc->capture_mode = JUMPERLESS_MODE_DIGITAL_ONLY;
    }
    
    /* Send channel configuration */
    uint8_t config_data[8];
    config_data[0] = (devc->digital_channel_mask >> 0) & 0xFF;
    config_data[1] = (devc->digital_channel_mask >> 8) & 0xFF;
    config_data[2] = (devc->digital_channel_mask >> 16) & 0xFF;
    config_data[3] = (devc->digital_channel_mask >> 24) & 0xFF;
    config_data[4] = (devc->analog_channel_mask >> 0) & 0xFF;
    config_data[5] = (devc->analog_channel_mask >> 8) & 0xFF;
    config_data[6] = (devc->analog_channel_mask >> 16) & 0xFF;
    config_data[7] = (devc->analog_channel_mask >> 24) & 0xFF;
    
    int ret = jumperless_send_command(serial, JUMPERLESS_CMD_SET_CHANNELS, 
                                     config_data, sizeof(config_data));
    if (ret != SR_OK) {
        return ret;
    }
    
    /* Wait for acknowledgment */
    uint8_t response_buf[16];
    size_t response_len;
    ret = jumperless_wait_for_response(serial, JUMPERLESS_RESP_STATUS, 
                                      response_buf, sizeof(response_buf), 
                                      &response_len, 1000);
    if (ret != SR_OK) {
        sr_err("Failed to receive channel configuration acknowledgment");
        return ret;
    }
    
    if (response_len >= 1 && response_buf[0] == 0x00) {
        sr_dbg("Channel configuration accepted by device");
        devc->device_configured = TRUE;
        return SR_OK;
    } else {
        sr_err("Device rejected channel configuration (status: 0x%02x)", 
               response_len >= 1 ? response_buf[0] : 0xFF);
        return SR_ERR;
    }
}

/* Process received sample data */
static int process_sample_data(const struct sr_dev_inst *sdi, const uint8_t *buf, size_t len)
{
    struct dev_context *devc = sdi->priv;
    struct sr_datafeed_packet packet;
    GSList *analog_channels = NULL;
    GSList *l;
    
    /* Calculate samples based on mode */
    size_t samples_in_buffer;
    if (devc->capture_mode == JUMPERLESS_MODE_MIXED_SIGNAL) {
        /* Mixed signal: 1 digital byte + N*2 analog bytes per sample */
        samples_in_buffer = len / devc->bytes_per_sample;
    } else {
        /* Digital only: 1 byte per sample */
        samples_in_buffer = len;
        devc->logic_unitsize = 1;
    }
    
    if (samples_in_buffer == 0) {
        return SR_OK;
    }
    
    /* Process digital data */
    if (devc->digital_channel_mask != 0) {
        struct sr_datafeed_logic logic;
        uint8_t *logic_buf;
        
        if (devc->capture_mode == JUMPERLESS_MODE_MIXED_SIGNAL) {
            /* Extract digital bytes from mixed-signal data */
            logic_buf = g_malloc(samples_in_buffer);
            for (size_t i = 0; i < samples_in_buffer; i++) {
                logic_buf[i] = buf[i * devc->bytes_per_sample];
            }
        } else {
            /* Digital-only data */
            logic_buf = g_malloc(len);
            memcpy(logic_buf, buf, len);
        }
        
        /* Send digital logic data */
        packet.type = SR_DF_LOGIC;
        packet.payload = &logic;
        logic.length = samples_in_buffer * devc->logic_unitsize;
        logic.unitsize = devc->logic_unitsize;
        logic.data = logic_buf;
        sr_session_send(sdi, &packet);
        
        g_free(logic_buf);
    }
    
    /* Process analog data */
    if (devc->analog_channel_mask != 0 && devc->capture_mode == JUMPERLESS_MODE_MIXED_SIGNAL) {
        /* Process each enabled analog channel separately (like demo driver) */
        for (l = sdi->channels; l; l = l->next) {
            struct sr_channel *ch = l->data;
            if (ch->type != SR_CHANNEL_ANALOG || !ch->enabled) {
                continue;
            }
            
            struct sr_datafeed_analog analog;
            struct sr_analog_encoding encoding;
            struct sr_analog_meaning meaning;
            struct sr_analog_spec spec;
            
            /* Allocate data for this channel */
            float *analog_data = g_malloc(samples_in_buffer * sizeof(float));
            
            /* Calculate which analog channel this is (0-4 for ADC 0-4) */
            int adc_channel = ch->index - JUMPERLESS_MAX_DIGITAL_CHANNELS;
            
            /* Skip if this channel is not enabled in the device mask */
            if (adc_channel < 0 || adc_channel >= JUMPERLESS_MAX_ANALOG_CHANNELS ||
                !((devc->analog_channel_mask >> adc_channel) & 1)) {
                continue;
            }
            
            /* Extract data for this specific channel from mixed stream */
            for (size_t i = 0; i < samples_in_buffer; i++) {
                /* Firmware sends: [digital_byte][adc0_low][adc0_high][adc1_low][adc1_high]...
                 * for each enabled analog channel in order of channel mask bits */
                
                /* Find the position of this ADC channel in the data stream */
                int stream_position = 0;
                for (int bit = 0; bit < adc_channel; bit++) {
                    if ((devc->analog_channel_mask >> bit) & 1) {
                        stream_position++;
                    }
                }
                
                /* Calculate offset in the buffer */
                size_t adc_offset = i * devc->bytes_per_sample + 1 + (stream_position * 2);
                
                if (adc_offset + 1 < len) {
                    uint16_t adc_value = buf[adc_offset] | (buf[adc_offset + 1] << 8);
                    
                    /* Convert to voltage using proper Jumperless ADC formula */
                    float voltage;
                    if (adc_channel == 4) {
                        /* ADC 4 is 0-5V range: voltage = adc * 5.0 / 4095 */
                        voltage = (float)adc_value * 5.0f / 4095.0f;
                    } else {
                        /* ADCs 0-3,7 are ±8V range: voltage = (adc * 18.28 / 4095) - 8.0 */
                        voltage = ((float)adc_value * 18.28f / 4095.0f) - 8.0f;
                    }
                    
                    analog_data[i] = voltage;
                } else {
                    /* If we can't read the data, set to 0V */
                    analog_data[i] = 0.0f;
                }
            }
            
            /* Set up analog packet (following demo driver pattern) */
            sr_analog_init(&analog, &encoding, &meaning, &spec, 12);
            
            /* Create channel list with just this channel */
            analog_channels = g_slist_append(NULL, ch);
            analog.meaning->channels = analog_channels;
            analog.meaning->mq = SR_MQ_VOLTAGE;
            analog.meaning->unit = SR_UNIT_VOLT;
            analog.meaning->mqflags = 0;
            analog.num_samples = samples_in_buffer;
            analog.data = analog_data;
            
            /* Send the packet */
            packet.type = SR_DF_ANALOG;
            packet.payload = &analog;
            sr_session_send(sdi, &packet);
            
            /* Clean up */
            g_free(analog_data);
            g_slist_free(analog_channels);
            analog_channels = NULL;
        }
    }
    
    devc->num_samples += samples_in_buffer;
    return SR_OK;
}

/* Main data reception function */
SR_PRIV int jumperless_fala_receive_data(int fd, int revents, void *cb_data)
{
    struct sr_dev_inst *sdi = cb_data;
    struct dev_context *devc = sdi->priv;
    struct sr_serial_dev_inst *serial = sdi->conn;
    uint8_t buf[LOGIC_BUFSIZE];
    int len;
    
    (void)fd;
    (void)revents;
    
    if (!devc->acquisition_active) {
        return TRUE;
    }
    
    /* Try to read data */
    len = serial_read_blocking(serial, buf, sizeof(buf), 1000);
    if (len < 0) {
        sr_err("Serial read error: %d", len);
        sr_dev_acquisition_stop(sdi);
        return FALSE;
    }
    
    if (len == 0) {
        return TRUE;  /* No data available */
    }
    
    /* Process received data */
    int ret = process_sample_data(sdi, buf, len);
    if (ret != SR_OK) {
        sr_err("Failed to process sample data");
        sr_dev_acquisition_stop(sdi);
        return FALSE;
    }
    
    /* Check if we've received enough samples */
    if (devc->limit_samples > 0 && devc->num_samples >= devc->limit_samples) {
        sr_info("Requested number of samples received: %"PRIu64, devc->num_samples);
        sr_dev_acquisition_stop(sdi);
        return FALSE;
    }
    
    return TRUE;
}
