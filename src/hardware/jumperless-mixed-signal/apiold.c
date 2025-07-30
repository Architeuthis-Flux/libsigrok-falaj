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
#include <fcntl.h>
#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "protocol.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define SERIALCOMM "115200/8n1/dtr=1/rts=0/flow=0"

static const uint32_t scanopts[] = {
        SR_CONF_CONN,
        SR_CONF_SERIALCOMM,
        SR_CONF_PROBE_NAMES,
        SR_CONF_FORCE_DETECT,
};

static const uint32_t drvopts[] = {
        SR_CONF_LOGIC_ANALYZER,
        SR_CONF_OSCILLOSCOPE,  /* For mixed-signal support */
};

static const uint32_t devopts[] = {
        SR_CONF_CONTINUOUS,
        SR_CONF_CONN | SR_CONF_GET,
        SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
        SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static struct sr_dev_driver jumperless_mixed_signal_driver_info;

/* Supported sample rates - matches Jumperless RP2350 capabilities */
static const uint64_t samplerates[] = {
        SR_KHZ(1), SR_KHZ(2), SR_KHZ(5), SR_KHZ(10), SR_KHZ(20), 
        SR_KHZ(50), SR_KHZ(100), SR_KHZ(200), SR_KHZ(500),
        SR_MHZ(1), SR_MHZ(2), SR_MHZ(5), SR_MHZ(10), SR_MHZ(20), SR_MHZ(50), SR_MHZ(100)
};

/* Jumperless logic analyzer channels (8 GPIO channels) */
SR_PRIV const char *jumperless_channel_names[] = {
        "GPIO 1", "GPIO 2", "GPIO 3", "GPIO 4", "GPIO 5", "GPIO 6", "GPIO 7", "GPIO 8", //"UART Tx", "UART Rx", "RP 6", "RP 7", "Nano Reset 0", "Nano Reset 1", 
};

SR_PRIV const char *jumperless_debug_all_channel_names[] = {
        "UART Tx", "UART Rx", "LED Out Probe", "LED Out Top", "I2C 0 SDA", "I2C 0 SCL", "RP 6", "RP 7", "LDAC", "Probe Button", "Probe Probe", 
        "Encoder Push", "Encoder A", "Encoder B", "CH446Q Data", "CH446Q Clock", "CH446Q Reset", "LED Out Breadboard",
        "Nano Reset 0", "Nano Reset 1", "GPIO 1", "GPIO 2", "GPIO 3", "GPIO 4", "GPIO 5", "GPIO 6", "GPIO 7", "GPIO 8", 
        "CH446Q Chip Select A", "CH446Q Chip Select B", "CH446Q Chip Select C", "CH446Q Chip Select D", "CH446Q Chip Select E", "CH446Q Chip Select F", "CH446Q Chip Select G", "CH446Q Chip Select H", "CH446Q Chip Select I", "CH446Q Chip Select J", "CH446Q Chip Select K", "CH446Q Chip Select L", 
        "ADC 0", "ADC 1", "ADC 2", "ADC 3", "ADC 4 (0 - 5V)", "ADC 5 (Probe Pad Sense)", "ADC 6 (Power Supply Monitor)", "ADC 7 (Probe Measure)",
};

/* Jumperless analog channels (ADC capable pins) */
SR_PRIV const char *jumperless_analog_names[] = {
        "ADC 0", "ADC 1", "ADC 2", "ADC 3", "ADC 4 (0 - 5V)", 
        //"ADC 5 (Probe Pad Sense)", "ADC 6 (Power Supply Monitor)", "ADC 7 (Probe Measure)", 
      //  "DAC 0", "DAC 1", "INA 0", "INA 1",
};

SR_PRIV gboolean jumperless_detect_device(struct sr_serial_dev_inst *serial)
{
        /* Send Jumperless ID command */
        if (serial_write_blocking(serial, "\x02", 1, 100) != 1)
                return FALSE;
        
        /* Read response header (JUMPERLESS_RESP_STATUS) */
        uint8_t response_header;
        if (serial_read_blocking(serial, &response_header, 1, 500) != 1)
                return FALSE;
                
        if (response_header != JUMPERLESS_RESP_STATUS)
                return FALSE;
        
        /* Read payload length */
        uint16_t payload_len;
        if (serial_read_blocking(serial, &payload_len, 2, 500) != 2)
                return FALSE;
        
        /* Read device ID string */
        if (payload_len > 0 && payload_len < 256) {
                char id_buf[256];
                if (serial_read_blocking(serial, id_buf, payload_len, 500) == payload_len) {
                        id_buf[payload_len] = '\0';
                        /* Check for Jumperless device ID */
                        if (strstr(id_buf, "Jumperless") != NULL) {
                                sr_info("Detected Jumperless device: %s", id_buf);
                                return TRUE;
                        }
                }
        }
                
        return FALSE;
}

static int dev_clear(const struct sr_dev_driver *di)
{
        /* Ensure all serial ports are properly closed */
        return std_dev_clear(di);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
        struct drv_context *drvc;
        struct sr_config *src;
        struct sr_dev_inst *sdi;
        struct sr_serial_dev_inst *serial;
        struct dev_context *devc;
        size_t i;
        GSList *l;
        const char *conn, *serialcomm, *force_detect;
        size_t ch_max;
        char **channel_names = sr_parse_probe_names(NULL, jumperless_channel_names, 
                                                   ARRAY_SIZE(jumperless_channel_names),
                                                   ARRAY_SIZE(jumperless_channel_names), &ch_max);

        drvc = di->context;
        if (drvc->instances) {
                /* Clean up any existing instances to prevent port conflicts */
                g_slist_free_full(drvc->instances, (GDestroyNotify)sr_dev_inst_free);
                drvc->instances = NULL;
        }

        conn = serialcomm = force_detect = NULL;
        for (l = options; l; l = l->next) {
                src = l->data;
                switch (src->key) {
                case SR_CONF_CONN:
                        conn = g_variant_get_string(src->data, NULL);
                        break;
                case SR_CONF_SERIALCOMM:
                        serialcomm = g_variant_get_string(src->data, NULL);
                        break;
                case SR_CONF_FORCE_DETECT:
                        force_detect = g_variant_get_string(src->data, NULL);
                        break;
                }
        }
        if (!conn)
                return NULL;

        if (!serialcomm)
                serialcomm = SERIALCOMM;

        serial = sr_serial_dev_inst_new(conn, serialcomm);
        if (serial_open(serial, SERIAL_RDWR) != SR_OK) {
                sr_err("Failed to open serial port %s", conn);
                sr_serial_dev_inst_free(serial);
                return NULL;
        }

        /* Detect Jumperless mixed-signal logic analyzer */
        gboolean device_detected = FALSE;
        if (force_detect && g_ascii_strcasecmp(force_detect, "jumperless") == 0) {
                device_detected = TRUE;
        } else {
                device_detected = jumperless_detect_device(serial);
        }

        if (!device_detected) {
                sr_info("Jumperless mixed-signal logic analyzer not detected on %s", conn);
                serial_close(serial);
                sr_serial_dev_inst_free(serial);
                sr_free_probe_names(channel_names);
                return NULL;
        }

        /* Always close the port after detection to prevent resource conflicts */
        serial_close(serial);

        sdi = g_malloc0(sizeof(struct sr_dev_inst));
        sdi->status = SR_ST_INACTIVE;
        sdi->vendor = g_strdup("Jumperless");
        sdi->model = g_strdup("Mixed-Signal Logic Analyzer");
        sdi->version = g_strdup("1.0");
        sdi->inst_type = SR_INST_SERIAL;
        sdi->conn = serial;
        sdi->connection_id = g_strdup(serial->port);
        
        devc = g_malloc0(sizeof(struct dev_context));
        sdi->priv = devc;
        
        /* Default configuration for Jumperless */
        devc->cur_samplerate = SR_KHZ(100);  // 100KHz default  
        devc->num_logic_channels = 8;        // 8 digital channels
        devc->num_analog_channels = 5;       // 5 analog channels
        devc->capture_mode = JUMPERLESS_MODE_MIXED_SIGNAL;  // Default mixed-signal mode
        devc->logic_unitsize = 1;            // 1 byte per sample
        devc->all_logic_channels_mask = 0xFF;  // 8 channels = 0xFF
        devc->limit_samples = 32768;         // Default 32K samples
        devc->bytes_per_sample = 1 + (5 * 2); // 1 digital + 5*2 analog bytes
        devc->digital_bytes_per_sample = 1;
        devc->analog_bytes_per_sample = 10;
        devc->analog_resolution = 12;
        devc->analog_voltage_range = 3.3f;
        devc->header_received = FALSE;
        devc->device_configured = FALSE;
        devc->acquisition_active = FALSE;

        /* Add digital logic channels */
        for (i = 0; i < MIN(ch_max, devc->num_logic_channels); i++) {
                sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, channel_names[i]);
        }

        /* Add analog channels */
        for (i = 0; i < MIN(ARRAY_SIZE(jumperless_analog_names), devc->num_analog_channels); i++) {
                sr_channel_new(sdi, i + devc->num_logic_channels, SR_CHANNEL_ANALOG, TRUE,
                              jumperless_analog_names[i]);
        }

        sr_free_probe_names(channel_names);
        
        return std_scan_complete(di, g_slist_append(NULL, sdi));
}

static int config_get(uint32_t key, GVariant **data,
                     const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
        struct dev_context *devc = (sdi) ? sdi->priv : NULL;
        (void)cg;

        switch (key) {
        case SR_CONF_SAMPLERATE:
                if (!devc)
                        return SR_ERR_ARG;
                *data = g_variant_new_uint64(devc->cur_samplerate);
                break;
        case SR_CONF_LIMIT_SAMPLES:
                if (!devc)
                        return SR_ERR_ARG;
                *data = g_variant_new_uint64(devc->limit_samples);
                break;
        case SR_CONF_CONN:
                if (!sdi || !sdi->conn)
                        return SR_ERR_ARG;
                *data = g_variant_new_string(((struct sr_serial_dev_inst *)(sdi->conn))->port);
                break;
        default:
                return SR_ERR_NA;
        }

        return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
                     const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
        struct dev_context *devc = (sdi) ? sdi->priv : NULL;
        (void)cg;
        uint64_t tmp_u64;

        if (!devc)
                return SR_ERR_ARG;

        switch (key) {
        case SR_CONF_SAMPLERATE:
                tmp_u64 = g_variant_get_uint64(data);
                /* Validate sample rate is supported */
                for (size_t i = 0; i < ARRAY_SIZE(samplerates); i++) {
                        if (tmp_u64 == samplerates[i]) {
                                devc->cur_samplerate = tmp_u64;
                                return SR_OK;
                        }
                }
                return SR_ERR_ARG;
        case SR_CONF_LIMIT_SAMPLES:
                tmp_u64 = g_variant_get_uint64(data);
                if (tmp_u64 < 1024 || tmp_u64 > 1000000)  // 1K to 1M samples
                        return SR_ERR_ARG;
                devc->limit_samples = tmp_u64;
                break;
        default:
                return SR_ERR_NA;
        }

        return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
                      const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
        struct dev_context *devc;
        (void)cg;

        devc = (sdi) ? sdi->priv : NULL;

        switch (key) {
        case SR_CONF_SCAN_OPTIONS:
        case SR_CONF_DEVICE_OPTIONS:
                return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
        case SR_CONF_SAMPLERATE:
                *data = std_gvar_array_u64(samplerates, ARRAY_SIZE(samplerates));
                break;
        case SR_CONF_LIMIT_SAMPLES:
                if (!sdi)
                        return SR_ERR_ARG;
                devc = sdi->priv;
                if (devc->limit_samples == 0)
                        return SR_ERR_NA;
                else
                        *data = std_gvar_tuple_u64(1024, 1000000);  // 1K to 1M range
                break;
        default:
                return SR_ERR_NA;
        }

        return SR_OK;
}

static int dev_open(struct sr_dev_inst *sdi)
{
        struct sr_serial_dev_inst *serial = sdi->conn;
        int ret;
        
        ret = serial_open(serial, SERIAL_RDWR);
        if (ret != SR_OK) {
                sr_err("Failed to open serial port");
                return ret;
        }
        
        /* Get device header to learn about capabilities */
        ret = jumperless_receive_header(sdi);
        if (ret != SR_OK) {
                sr_err("Failed to receive device header");
                serial_close(serial);
                return ret;
        }
        
        sdi->status = SR_ST_ACTIVE;
        return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
        struct sr_serial_dev_inst *serial = sdi->conn;
        
        if (serial && sdi->status == SR_ST_ACTIVE) {
                serial_close(serial);
                sdi->status = SR_ST_INACTIVE;
        }
        
        return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
        struct dev_context *devc;
        struct sr_serial_dev_inst *serial;
        uint8_t cmd[5];
        uint32_t sample_count, divider;
        int ret;

        devc = sdi->priv;
        serial = sdi->conn;

        /* Ensure device is open */
        if (sdi->status != SR_ST_ACTIVE) {
                if (dev_open((struct sr_dev_inst *)sdi) != SR_OK)
                        return SR_ERR;
        }

        /* Reset operational states */
        devc->num_samples = 0;
        devc->num_transfers = 0;
        devc->logic_unitsize = 1;
        
        std_session_send_df_header(sdi);

        /* Send SUMP reset command */
        cmd[0] = 0x00;
        if (serial_write_blocking(serial, cmd, 1, 100) != 1) {
                sr_err("Failed to send SUMP reset command");
                return SR_ERR;
        }
        g_usleep(100000); /* Wait 100ms for reset */

        /* Configure sample count (SUMP command 0x81) */
        sample_count = (devc->limit_samples / 4) - 1; /* Convert to read_count */
        if (sample_count > 0xFFFF) sample_count = 0xFFFF;
        
        cmd[0] = 0x81; /* SUMP_SET_READ_DELAY */
        cmd[1] = (sample_count >> 0) & 0xFF;  /* read_count low */
        cmd[2] = (sample_count >> 8) & 0xFF;  /* read_count high */
        cmd[3] = 0x00; /* delay_count low (no delay) */
        cmd[4] = 0x00; /* delay_count high (no delay) */
        if (serial_write_blocking(serial, cmd, 5, 100) != 5) {
                sr_err("Failed to send SUMP sample count");
                return SR_ERR;
        }

        /* Configure sample rate (SUMP command 0x80) */
        divider = (100000000 / devc->cur_samplerate) - 1;
        if (divider > 0xFFFFFF) divider = 0xFFFFFF; /* Cap at max divider */
        
        cmd[0] = 0x80; /* SUMP_SET_DIVIDER */
        cmd[1] = (divider >> 0) & 0xFF;   /* divider bits 0-7 */
        cmd[2] = (divider >> 8) & 0xFF;   /* divider bits 8-15 */
        cmd[3] = (divider >> 16) & 0xFF;  /* divider bits 16-23 */
        if (serial_write_blocking(serial, cmd, 4, 100) != 4) {
                sr_err("Failed to send SUMP sample rate");
                return SR_ERR;
        }
        
        /* Configure channels based on PulseView settings */
        ret = jumperless_configure_channels(sdi);
        if (ret != SR_OK) {
                return ret;
        }

        /* Allocate sample buffer */
        size_t buffer_size = devc->limit_samples * devc->bytes_per_sample;
        if (devc->raw_sample_buf) {
                g_free(devc->raw_sample_buf);
        }
        devc->raw_sample_buf = g_malloc(buffer_size);
        devc->raw_sample_buf_size = buffer_size;

        /* Reset acquisition state */
        devc->num_samples = 0;
        devc->acquisition_active = TRUE;
        devc->start_us = g_get_monotonic_time();

        /* Send acquisition start command */
        ret = jumperless_send_command(serial, JUMPERLESS_CMD_ARM, NULL, 0);
        if (ret != SR_OK) {
                devc->acquisition_active = FALSE;
                return ret;
        }

        return serial_source_add(sdi->session, serial, G_IO_IN, 50,
                                jumperless_fala_receive_data, (struct sr_dev_inst *)sdi);
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
        struct dev_context *devc = sdi->priv;
        struct sr_serial_dev_inst *serial = sdi->conn;

        if (!devc->acquisition_active) {
                return SR_OK;
        }

        devc->acquisition_active = FALSE;

        /* Remove serial source */
        serial_source_remove(sdi->session, serial);

        /* Send stop command to device */
        jumperless_send_command(serial, JUMPERLESS_CMD_RESET, NULL, 0);

        /* Free sample buffer */
        if (devc->raw_sample_buf) {
                g_free(devc->raw_sample_buf);
                devc->raw_sample_buf = NULL;
        }

        /* Send session end packet */
        std_session_send_df_end(sdi);

        sr_info("Jumperless acquisition stopped, %"PRIu64" samples captured", devc->num_samples);

        return SR_OK;
}

static struct sr_dev_driver jumperless_fala_driver_info = {
        .name = "jumperless-mixed-signal",
        .longname = "Jumperless Mixed-Signal Logic Analyzer",
        .api_version = 1,
        .init = std_init,
        .cleanup = std_cleanup,
        .scan = scan,
        .dev_list = std_dev_list,
        .dev_clear = dev_clear,
        .config_get = config_get,
        .config_set = config_set,
        .config_list = config_list,
        .dev_open = dev_open,
        .dev_close = dev_close,
        .dev_acquisition_start = dev_acquisition_start,
        .dev_acquisition_stop = dev_acquisition_stop,
        .context = NULL,
};
SR_REGISTER_DEV_DRIVER(jumperless_fala_driver_info);
