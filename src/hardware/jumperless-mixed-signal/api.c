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
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <libsigrok/libsigrok.h>
#include <libsigrok-internal.h>
#include "protocol.h"
//#include "sw_limits.h"

/* Forward declarations for static functions. */
static void jlms_init_device_context(struct dev_context *devc);


#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define SERIALCOMM "115200/8n1/dtr=1/rts=0/flow=0"

/* Forward declarations */
static int dev_close(struct sr_dev_inst *sdi);

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
    SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
    SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_DEVICE_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_cg_logic[] = {
    SR_CONF_PATTERN_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_cg_analog_group[] = {
    SR_CONF_AMPLITUDE | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_OFFSET | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg_analog_channel[] = {
    SR_CONF_MEASURED_QUANTITY | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_PATTERN_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
    SR_CONF_AMPLITUDE | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_OFFSET | SR_CONF_GET | SR_CONF_SET,
};

static const int32_t trigger_matches[] = {
    SR_TRIGGER_ZERO,
    SR_TRIGGER_ONE,
    SR_TRIGGER_RISING,
    SR_TRIGGER_FALLING,
    SR_TRIGGER_EDGE,
};

/* Pattern modes for testing */
static const char *logic_pattern_str[] = {
    "sigrok",
    "random",
    "incremental", 
    "walking-one",
    "walking-zero",
    "all-low",
    "all-high",
};

static const char *analog_pattern_str[] = {
    "square",
    "sine",
    "triangle",
    "sawtooth",
    "dc",
};

/* Capture mode options */
static const char *capture_mode_str[] = {
    "digital-only",
    "mixed-signal", 
    "analog-only",
};

/* Supported sample rates - matches Jumperless RP2350 capabilities */
static const uint64_t samplerates[] = { SR_HZ(250),
    SR_HZ(500), SR_KHZ(1), SR_KHZ(2), SR_KHZ(5), SR_KHZ(10), SR_KHZ(20), 
    SR_KHZ(50), SR_KHZ(100), SR_KHZ(200), SR_KHZ(500),
    SR_MHZ(1), SR_MHZ(2), SR_MHZ(5), SR_MHZ(10), SR_MHZ(20), SR_MHZ(50)
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
    "ADC 0", "ADC 1", "ADC 2", "ADC 3", "ADC 4",
            "Pad Sense", "Supply", "Probe Measure", 
        "DAC 0 Output", "DAC 1 Output", "INA 0 Voltage", "INA 0 Current", "INA 1 Voltage", "INA 1 Current",
};

static void clear_helper(struct dev_context *devc)
{
    /* Clean up any allocated buffers */
    if (devc->raw_sample_buf) {
        g_free(devc->raw_sample_buf);
        devc->raw_sample_buf = NULL;
    }
    
    //sr_sw_limits_free(devc->limits);

    /* Clean up device ID string */
    if (devc->device_id) {
        g_free(devc->device_id);
        devc->device_id = NULL;
    }
}

static int dev_clear(const struct sr_dev_driver *di)
{
    return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
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
        sr_free_probe_names(channel_names);
        return NULL;
    }

    /* Detect Jumperless mixed-signal logic analyzer */
    gboolean device_detected = FALSE;
    if (force_detect && g_ascii_strcasecmp(force_detect, "jumperless") == 0) {
        device_detected = TRUE;
    } else {
        device_detected = jlms_detect_device(serial);
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
    sdi->version = g_strdup("2.0");
    sdi->inst_type = SR_INST_SERIAL;
    sdi->conn = serial;
    sdi->connection_id = g_strdup(serial->port);
    
    devc = g_malloc0(sizeof(struct dev_context));
    sdi->priv = devc;
    
    /* Initialize device context with defaults */
    devc->cur_samplerate = SR_KHZ(100);  // 100KHz default  
    devc->limit_samples = 5000;         // Default 5K samples
    devc->capture_ratio = 50;            // 50% pre-trigger
    
    /* Protocol detection - Jumperless devices ONLY support Enhanced protocol */
    devc->protocol_mode = JUMPERLESS_PROTOCOL_ENHANCED;
    devc->protocol_detected = TRUE;  /* No need to detect - we know it's Enhanced */
    devc->device_identified = FALSE;
    devc->header_received = FALSE;
    devc->device_configured = FALSE;
    devc->device_armed = FALSE;
    devc->acquisition_running = FALSE;
    
    /* Device capabilities (defaults, will be updated from device header) */
    devc->device_id = NULL;
    devc->device_version = 0;
    devc->device_capture_mode = JUMPERLESS_MODE_MIXED_SIGNAL;
    devc->max_digital_channels = JUMPERLESS_MAX_DIGITAL_CHANNELS;
    devc->max_analog_channels = JUMPERLESS_MAX_ANALOG_CHANNELS;
    
    sr_err("INIT DEBUG: Set initial max_analog_channels to %d (constant=%d)", 
           devc->max_analog_channels, JUMPERLESS_MAX_ANALOG_CHANNELS);
    devc->max_sample_rate = JUMPERLESS_MAX_SAMPLE_RATE;
    devc->max_memory_depth = MAX_NUM_SAMPLES;
    devc->supports_triggers = FALSE;
    devc->supports_compression = FALSE;
    devc->adc_resolution_bits = 12;
    
    /* Channel configuration - ALWAYS use mixed-signal format */
    devc->digital_channel_mask = 0xFF;  // Default to all 8 digital channels
    devc->analog_channel_mask = 0x1F;   // Default to 5 analog channels
    devc->num_digital_channels = 8;     // Always 8 digital channels
    devc->num_analog_channels = 5;      // Always 5 analog channels
    devc->bytes_per_sample = 32;        // ALWAYS 32 bytes per sample (mixed-signal)
    devc->digital_bytes_per_sample = 3; // 3 bytes for digital data
    devc->analog_bytes_per_sample = 29; // 29 bytes for analog data
    devc->analog_resolution = 12;
    devc->analog_voltage_range = 18.28f;  /* Jumperless ADC spread for ±8V channels */
    devc->max_samples = MAX_NUM_SAMPLES;
    devc->supported_modes = JUMPERLESS_CAP_MIXED_SIGNAL;
    
    /* Logic analyzer specific fields */
    devc->num_logic_channels = JUMPERLESS_DEFAULT_DIGITAL_CHANNELS;  // Use default, not max
    devc->logic_unitsize = 1;
    devc->all_logic_channels_mask = 0xFF;  // 8 channels = 0xFF (default)
    devc->raw_sample_buf = NULL;
    devc->raw_sample_buf_size = 0;
    
    /* Reset acquisition state */
    devc->num_samples = 0;
    
    /* Add digital logic channels with channel group */
    if (devc->max_digital_channels > 0) {
        struct sr_channel_group *cg_logic = sr_channel_group_new(sdi, "Logic", NULL);
        sr_dbg("Creating digital channels: ch_max=%zu, default_digital_channels=%d", ch_max, JUMPERLESS_DEFAULT_DIGITAL_CHANNELS);
        for (i = 0; i < MIN(ch_max, JUMPERLESS_DEFAULT_DIGITAL_CHANNELS); i++) {
            struct sr_channel *ch = sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, channel_names[i]);
            cg_logic->channels = g_slist_append(cg_logic->channels, ch);
            sr_dbg("Created digital channel '%s' with index %zu", channel_names[i], i);
        }
    }

    /* Add analog channels with channel groups */
    if (devc->max_analog_channels > 0) {
        /* Create an "Analog" group with all analog channels */
        struct sr_channel_group *acg = sr_channel_group_new(sdi, "Analog", NULL);
        sr_dbg("Creating analog channels: default_analog_channels=%d, starting_index=%d", 
               JUMPERLESS_DEFAULT_ANALOG_CHANNELS, JUMPERLESS_DEFAULT_DIGITAL_CHANNELS);
        
        for (i = 0; i < MIN(ARRAY_SIZE(jumperless_analog_names), JUMPERLESS_DEFAULT_ANALOG_CHANNELS); i++) {
            size_t channel_index = i + JUMPERLESS_DEFAULT_DIGITAL_CHANNELS;
            struct sr_channel *ch = sr_channel_new(sdi, channel_index, 
                                                  SR_CHANNEL_ANALOG, TRUE,
                                                  jumperless_analog_names[i]);
            /* Add to the group analog channel group */
            acg->channels = g_slist_append(acg->channels, ch);
            sr_dbg("Created analog channel '%s' with index %zu", jumperless_analog_names[i], channel_index);
            
            /* Each analog channel also gets its own individual channel group */
            struct sr_channel_group *cg = sr_channel_group_new(sdi, jumperless_analog_names[i], NULL);
            cg->channels = g_slist_append(NULL, ch);
        }
    }

    sr_free_probe_names(channel_names);
    
    return std_scan_complete(di, g_slist_append(NULL, sdi));

error_out:
    sr_free_probe_names(channel_names);
    return NULL;
}

static int config_get(uint32_t key, GVariant **data,
                     const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
    struct dev_context *devc = (sdi) ? sdi->priv : NULL;
    struct sr_channel *ch;

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
    case SR_CONF_LIMIT_MSEC:
        if (!devc)
            return SR_ERR_ARG;
        *data = g_variant_new_uint64(devc->limit_msec);
        break;
    case SR_CONF_LIMIT_FRAMES:
        if (!devc)
            return SR_ERR_ARG;
        *data = g_variant_new_uint64(devc->limit_frames);
        break;
    case SR_CONF_CAPTURE_RATIO:
        if (!devc)
            return SR_ERR_ARG;
        *data = g_variant_new_uint64(devc->capture_ratio);
        break;
    case SR_CONF_CONN:
        if (!sdi || !sdi->conn)
            return SR_ERR_ARG;
        *data = g_variant_new_string(((struct sr_serial_dev_inst *)(sdi->conn))->port);
        break;
    // case SR_CONF_PATTERN_MODE:
    //     if (!cg)
    //         return SR_ERR_CHANNEL_GROUP;
    //     ch = cg->channels->data;
    //     if (ch->type == SR_CHANNEL_LOGIC) {
    //         /* Return logic pattern for logic channels */
    //         *data = g_variant_new_string("sigrok");  /* Default pattern */
    //     } else if (ch->type == SR_CHANNEL_ANALOG) {
    //         /* Return analog pattern for analog channels */
    //         *data = g_variant_new_string("dc");  /* Default pattern */
    //     } else {
    //         return SR_ERR_BUG;
    //     }
    //     break;
    case SR_CONF_AMPLITUDE:
        if (!cg)
            return SR_ERR_CHANNEL_GROUP;
        ch = cg->channels->data;
        if (ch->type != SR_CHANNEL_ANALOG)
            return SR_ERR_ARG;
        *data = g_variant_new_double(devc->analog_voltage_range);
        break;
    case SR_CONF_OFFSET:
        if (!cg)
            return SR_ERR_CHANNEL_GROUP;
        ch = cg->channels->data;
        if (ch->type != SR_CHANNEL_ANALOG)
            return SR_ERR_ARG;
        *data = g_variant_new_double(0.0);  /* Default offset */
        break;
    case SR_CONF_MEASURED_QUANTITY:
        if (!cg)
            return SR_ERR_CHANNEL_GROUP;
        ch = cg->channels->data;
        if (ch->type != SR_CHANNEL_ANALOG)
            return SR_ERR_ARG;
        /* Return a tuple (mq, mqflags) for measured quantity */
        *data = g_variant_new("(uu)", SR_MQ_VOLTAGE, SR_MQFLAG_DC);
        break;
    case SR_CONF_DEVICE_MODE:
        if (!devc)
            return SR_ERR_ARG;
        *data = g_variant_new_string(capture_mode_str[devc->device_capture_mode]);
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
    struct sr_channel *ch;
    uint64_t tmp_u64;
    int logic_pattern, analog_pattern;

    if (!devc)
        return SR_ERR_ARG;

    switch (key) {
    case SR_CONF_SAMPLERATE:
        if (g_variant_is_of_type(data, G_VARIANT_TYPE_UINT64)) {
            devc->cur_samplerate = g_variant_get_uint64(data);
            sr_info("DRIVER API: Set cur_samplerate to %"PRIu64, devc->cur_samplerate);
            if (sdi->status == SR_ST_ACTIVE) {
                jlms_configure_timing(sdi);
            }
        }
        break;

    case SR_CONF_LIMIT_SAMPLES:
        if (g_variant_is_of_type(data, G_VARIANT_TYPE_UINT64)) {
            devc->limit_samples = g_variant_get_uint64(data);
            sr_info("DRIVER API: Set limit_samples to %"PRIu64, devc->limit_samples);
            if (sdi->status == SR_ST_ACTIVE) {
                jlms_configure_timing(sdi);
            }
        }
        break;
    case SR_CONF_LIMIT_MSEC:
        tmp_u64 = g_variant_get_uint64(data);
        devc->limit_msec = tmp_u64;
        devc->limit_samples = 0;  /* Clear conflicting limit */
        break;
    case SR_CONF_LIMIT_FRAMES:
        tmp_u64 = g_variant_get_uint64(data);
        devc->limit_frames = tmp_u64;
        break;
    case SR_CONF_CAPTURE_RATIO:
        tmp_u64 = g_variant_get_uint64(data);
        if (tmp_u64 > 100)
            return SR_ERR_ARG;
        devc->capture_ratio = tmp_u64;
        break;
    // case SR_CONF_PATTERN_MODE:
    //     if (!cg)
    //         return SR_ERR_CHANNEL_GROUP;
    //     logic_pattern = std_str_idx(data, ARRAY_AND_SIZE(logic_pattern_str));
    //     analog_pattern = std_str_idx(data, ARRAY_AND_SIZE(analog_pattern_str));
    //     if (logic_pattern < 0 && analog_pattern < 0)
    //         return SR_ERR_ARG;
        
    //     for (GSList *l = cg->channels; l; l = l->next) {
    //         ch = l->data;
    //         if (ch->type == SR_CHANNEL_LOGIC) {
    //             if (logic_pattern == -1)
    //                 return SR_ERR_ARG;
    //             sr_dbg("Setting logic pattern to %s", logic_pattern_str[logic_pattern]);
    //             /* Store pattern mode in device context if needed */
    //         } else if (ch->type == SR_CHANNEL_ANALOG) {
    //             if (analog_pattern == -1)
    //                 return SR_ERR_ARG;
    //             sr_dbg("Setting analog pattern for channel %s to %s",
    //                    ch->name, analog_pattern_str[analog_pattern]);
    //             /* Store pattern mode in device context if needed */
    //         } else {
    //             return SR_ERR_BUG;
    //         }
    //     }
    //     break;
    case SR_CONF_AMPLITUDE:
        if (!cg)
            return SR_ERR_CHANNEL_GROUP;
        for (GSList *l = cg->channels; l; l = l->next) {
            ch = l->data;
            if (ch->type != SR_CHANNEL_ANALOG)
                return SR_ERR_ARG;
            /* Store amplitude setting - could extend dev_context for this */
            sr_dbg("Setting amplitude for channel %s", ch->name);
        }
        break;
    case SR_CONF_OFFSET:
        if (!cg)
            return SR_ERR_CHANNEL_GROUP;
        for (GSList *l = cg->channels; l; l = l->next) {
            ch = l->data;
            if (ch->type != SR_CHANNEL_ANALOG)
                return SR_ERR_ARG;
            /* Store offset setting - could extend dev_context for this */
            sr_dbg("Setting offset for channel %s", ch->name);
        }
        break;
    case SR_CONF_MEASURED_QUANTITY:
        if (!cg)
            return SR_ERR_CHANNEL_GROUP;
        for (GSList *l = cg->channels; l; l = l->next) {
            ch = l->data;
            if (ch->type != SR_CHANNEL_ANALOG)
                return SR_ERR_ARG;
            /* Store measured quantity setting */
            sr_dbg("Setting measured quantity for channel %s", ch->name);
        }
        break;
    case SR_CONF_DEVICE_MODE:
        if (!devc)
            return SR_ERR_ARG;
        for (size_t i = 0; i < ARRAY_SIZE(capture_mode_str); i++) {
            if (g_ascii_strcasecmp(g_variant_get_string(data, NULL), capture_mode_str[i]) == 0) {
                sr_info("Setting Jumperless device mode to %s (%zu)", capture_mode_str[i], i);
                
                /* Always use Enhanced protocol for Jumperless devices */
                devc->protocol_mode = JUMPERLESS_PROTOCOL_ENHANCED;
                
                /* If device is active, send mode command to firmware */
                if (sdi->status == SR_ST_ACTIVE) {
                    int ret = jlms_set_device_mode(sdi, i);
                    if (ret != SR_OK) {
                        sr_err("Failed to set device mode to %s", capture_mode_str[i]);
                        return ret;
                    }
                } else {
                    /* Device not active, just update the mode in context */
                    devc->device_capture_mode = i;
                    sr_dbg("Device not active, stored mode %zu for later", i);
                }
                
                /* DON'T force channel states based on mode - preserve user selections */
                /* The user's channel enabled/disabled state should drive the mode, */
                /* not the other way around. This prevents the analog channel disable loop. */
                sr_dbg("Device mode set to %s - preserving user channel selections", capture_mode_str[i]);
                
                return SR_OK;
            }
        }
        sr_err("Invalid device mode string: %s", g_variant_get_string(data, NULL));
        return SR_ERR_ARG;
    default:
        return SR_ERR_NA;
    }

    return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
                      const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
    struct sr_channel *ch;
    (void)sdi;  /* Suppress unused parameter warning */

    if (!cg) {
        switch (key) {
        case SR_CONF_SCAN_OPTIONS:
        case SR_CONF_DEVICE_OPTIONS:
            return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
        case SR_CONF_SAMPLERATE:
            *data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
            break;
        case SR_CONF_LIMIT_SAMPLES:
            /* Use dynamic maximum from device capabilities when available */
            if (sdi && sdi->priv) {
                struct dev_context *devc = sdi->priv;
                uint64_t max_samples = (devc->max_memory_depth > 0) ? devc->max_memory_depth : MAX_NUM_SAMPLES;
                *data = std_gvar_tuple_u64(MIN_NUM_SAMPLES, max_samples);
            } else {
                /* Fallback to static maximum when device context not available */
                *data = std_gvar_tuple_u64(MIN_NUM_SAMPLES, MAX_NUM_SAMPLES);
            }
            break;
        case SR_CONF_TRIGGER_MATCH:
            *data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
            break;
        case SR_CONF_DEVICE_MODE:
            *data = std_gvar_array_str(ARRAY_AND_SIZE(capture_mode_str));
            break;
        default:
            return SR_ERR_NA;
        }
    } else {
        ch = cg->channels->data;
        switch (key) {
        case SR_CONF_DEVICE_OPTIONS:
            if (ch->type == SR_CHANNEL_LOGIC)
                *data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_logic));
            else if (ch->type == SR_CHANNEL_ANALOG) {
                if (strcmp(cg->name, "Analog") == 0)
                    *data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_analog_group));
                
                else
                    *data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_analog_channel));
            }
            else
                return SR_ERR_BUG;
            break;
        // case SR_CONF_PATTERN_MODE:
        //     /* The analog group (with all channels) shall not have a pattern property. */
        //     if (strcmp(cg->name, "Analog") == 0)
        //         return SR_ERR_NA;

        //     if (ch->type == SR_CHANNEL_LOGIC)
        //         *data = g_variant_new_strv(ARRAY_AND_SIZE(logic_pattern_str));
        //     else if (ch->type == SR_CHANNEL_ANALOG)
        //         *data = g_variant_new_strv(ARRAY_AND_SIZE(analog_pattern_str));
        //     else
        //         return SR_ERR_BUG;
        //     break;
        default:
            return SR_ERR_NA;
        }
    }

    return SR_OK;
}

static int dev_open(struct sr_dev_inst *sdi)
{
    struct sr_serial_dev_inst *serial = sdi->conn;
    struct dev_context *devc = sdi->priv;
    int ret;
    
    if (!sdi || !serial || !devc) {
        sr_err("dev_open called with invalid parameters");
        return SR_ERR_ARG;
    }
    
    /* If device is already open, close it first to ensure clean state */
    if (sdi->status == SR_ST_ACTIVE) {
        sr_dbg("Device already active, closing first for clean reconnection");
        dev_close(sdi);
    }
    
    /* Reset device context to ensure clean state on reconnection */
    jlms_reset_device_context(devc);
    
    ret = serial_open(serial, SERIAL_RDWR);
    if (ret != SR_OK) {
        sr_err("Failed to open serial port %s", serial->port);
        return ret;
    }
    
    /* For reconnected devices, always force protocol re-identification */
    devc->protocol_mode = JUMPERLESS_PROTOCOL_ENHANCED;
    devc->protocol_detected = FALSE;  /* Force re-detection */
    devc->device_identified = FALSE;  /* Force re-identification */
    devc->header_received = FALSE;    /* Force header re-request */
    
    sr_info("Device opened, forcing Enhanced protocol re-identification for reconnection");
    
    /* Re-identify the device and get capabilities */
    ret = jlms_identify_device(sdi);
    if (ret != SR_OK) {
        sr_warn("Failed to identify device after reconnection");
        /* Continue anyway - device might still work with defaults */
    }
    
    /* Request the initial capabilities header from the device. */
    ret = jlms_receive_header(sdi, TRUE);
    if (ret != SR_OK) {
        sr_warn("Failed to receive initial Jumperless header, using defaults");
    } else {
        sr_info("Successfully received initial Jumperless header after reconnection");
    }
    
    sdi->status = SR_ST_ACTIVE;
    sr_info("Device successfully reopened and configured");
    return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
    struct dev_context *devc;
    struct sr_serial_dev_inst *serial;
    int ret = SR_OK;
    
    /* Validate input parameters first */
    if (!sdi) {
        sr_dbg("dev_close called with NULL sdi");
        return SR_ERR_ARG;
    }
    
    if (!sdi->priv) {
        sr_dbg("dev_close called with NULL device context");
        /* Device context is already gone, mark as inactive and return */
        if (sdi->status == SR_ST_ACTIVE) {
            sdi->status = SR_ST_INACTIVE;
        }
        return SR_OK;
    }
    
    if (!sdi->conn) {
        sr_dbg("dev_close called with NULL connection");
        /* Connection is already gone, cleanup context and return */
        devc = sdi->priv;
        if (devc->acquisition_running) {
            devc->acquisition_running = FALSE;
            devc->device_armed = FALSE;
        }
        if (sdi->status == SR_ST_ACTIVE) {
            sdi->status = SR_ST_INACTIVE;
        }
        return SR_OK;
    }
    
    devc = sdi->priv;
    serial = sdi->conn;
    
    sr_dbg("Closing device %s", serial->port ? serial->port : "unknown");
    
    /* Stop any ongoing acquisition first */
    if (devc->acquisition_running) {
        sr_dbg("Stopping acquisition before closing device");
        
        /* Check if device is still present before trying to send commands */
        if (jlms_check_usb_device_present(serial->port)) {
            /* Try to send reset command, but don't fail if it doesn't work */
            int cmd_ret = jlms_send_command(serial, JUMPERLESS_CMD_RESET, NULL, 0);
            if (cmd_ret != SR_OK) {
                sr_warn("Failed to send reset command during close (device may be disconnected)");
                /* Continue with cleanup anyway */
            }
        } else {
            sr_dbg("Device no longer present, skipping reset command");
        }
        
        /* Clean up acquisition state */
        devc->acquisition_running = FALSE;
        devc->device_armed = FALSE;
        devc->num_samples = 0;
        
        /* Remove serial data source if active */
        if (sdi->session) {
            /* Try to remove the serial source, but don't fail if it's already gone */
            serial_source_remove(sdi->session, serial);
            sr_dbg("Serial data source removed (or was already removed)");
        }
        
        /* Free sample buffer */
        if (devc->raw_sample_buf) {
            g_free(devc->raw_sample_buf);
            devc->raw_sample_buf = NULL;
        }
    }
    
    /* Close serial connection - handle gracefully if already closed */
    if (serial && sdi->status == SR_ST_ACTIVE) {
        int close_ret = serial_close(serial);
        if (close_ret != SR_OK) {
            sr_warn("Serial close returned error (device may have been disconnected): %d", close_ret);
            /* Don't treat this as a fatal error */
        }
    }
    
    /* Mark device as inactive */
    sdi->status = SR_ST_INACTIVE;
    
    sr_dbg("Device closed successfully");
    return SR_OK;
}

// static int jlms_dev_caps(const struct sr_dev_inst *sdi, uint32_t *caps)
// {
//     struct dev_context *devc = sdi->priv;
//     (void)devc;

//     *caps = SR_CAP_LOGIC_ANALYZER;

//     if (devc->max_analog_channels > 0)
//         *caps |= SR_CAP_OSCILLOSCOPE;

//     return SR_OK;
// }

/* Check if channel configuration has changed and reconfigure if needed */
static int check_and_update_channels(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    GSList *l;
    uint32_t current_digital_mask = 0;
    uint32_t current_analog_mask = 0;
    int digital_count = 0, analog_count = 0;
    
    /* Calculate current channel masks based on enabled channels */
    for (l = sdi->channels; l; l = l->next) {
        struct sr_channel *ch = l->data;
        if (!ch->enabled)
            continue;
            
        if (ch->type == SR_CHANNEL_LOGIC && ch->index < 8) {
            current_digital_mask |= (1 << ch->index);
            digital_count++;
        } else if (ch->type == SR_CHANNEL_ANALOG) {
            /* Find ADC index by matching channel name with jumperless_analog_names */
            int adc_index = -1;
            for (int i = 0; i < JUMPERLESS_MAX_ANALOG_CHANNELS; i++) {
                if (strcmp(ch->name, jumperless_analog_names[i]) == 0) {
                    adc_index = i;
                    break;
                }
            }
            if (adc_index >= 0 && adc_index < devc->max_analog_channels) {
                current_analog_mask |= (1UL << adc_index);
                analog_count++;
            }
        }
    }
    
    /* Check if configuration has changed */
    if (current_digital_mask != devc->digital_channel_mask || 
        current_analog_mask != devc->analog_channel_mask) {
        
        sr_info("Channel configuration changed: digital 0x%08X→0x%08X, analog 0x%08X→0x%08X", 
                devc->digital_channel_mask, current_digital_mask,
                devc->analog_channel_mask, current_analog_mask);
        
        /* Send updated configuration to firmware */
        if (sdi->status == SR_ST_ACTIVE) {
            int ret = jlms_configure_channels(sdi);
            if (ret != SR_OK) {
                sr_err("Failed to reconfigure channels after change");
                return ret;
            }
            sr_info("Channels reconfigured successfully");
        } else {
            sr_dbg("Device not active, channel change will be applied on next acquisition");
        }
        
        return SR_OK;
    }
    
    sr_spew("No channel configuration changes detected");
    return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc;
    struct sr_serial_dev_inst *serial;
    int ret;

    devc = sdi->priv;
    serial = sdi->conn;

    /* Ensure device is open */
    if (sdi->status != SR_ST_ACTIVE) {
        if (dev_open((struct sr_dev_inst *)sdi) != SR_OK)
            return SR_ERR;
    }

    /* Reset acquisition state - critical for preventing crashes */
    devc->num_samples = 0;
    devc->acquisition_running = FALSE;
    devc->device_armed = FALSE;
    
    std_session_send_df_header(sdi);

    /* Jumperless devices ONLY support Enhanced protocol - never fall back to SUMP */
    devc->protocol_mode = JUMPERLESS_PROTOCOL_ENHANCED;
    sr_info("Starting Enhanced protocol acquisition (Jumperless devices only support Enhanced protocol)");
    
    /* Check if channels have changed since last acquisition and reconfigure if needed */
    ret = check_and_update_channels(sdi);
    if (ret != SR_OK) {
        sr_err("Failed to check/update channel configuration");
        goto cleanup_and_fail;
    }
    
    /* Configure timing parameters FIRST */
    ret = jlms_configure_timing(sdi);
    if (ret != SR_OK) {
        sr_err("Enhanced timing configuration failed - this is a fatal error for Jumperless devices");
        goto cleanup_and_fail;
    }
    
    /* Configure channels - this will be skipped if no changes detected */
    ret = jlms_configure_channels(sdi);
    if (ret != SR_OK) {
        sr_err("Enhanced channel configuration failed - this is a fatal error for Jumperless devices");
        goto cleanup_and_fail;
    }
    
    /* Arm device */
    ret = jlms_arm_device(sdi);
    if (ret != SR_OK) {
        sr_err("Enhanced device arming failed - this is a fatal error for Jumperless devices");
        goto cleanup_and_fail;
    }
    
    // The jlms_arm_device() function now handles auto-starting the acquisition
    // if no triggers are set. The explicit call here is redundant and causes
    // a race condition where the serial data source is added twice.
    // By removing this, we ensure the acquisition is only started once.
    /*
    ret = jlms_start_acquisition(sdi);
    if (ret != SR_OK) {
        sr_err("Enhanced acquisition start failed - this is a fatal error for Jumperless devices");
        devc->device_armed = FALSE;
        devc->acquisition_running = FALSE;
        goto cleanup_and_fail;
    }
    */
    
    sr_info("Enhanced protocol acquisition armed and/or started successfully");
    return SR_OK;

cleanup_and_fail:
    /* Ensure clean state on failure */
    devc->acquisition_running = FALSE;
    devc->device_armed = FALSE;
    devc->num_samples = 0;
    
    /* Send end packet to clean up session */
    struct sr_datafeed_packet packet;
    packet.type = SR_DF_END;
    packet.payload = NULL;
    sr_session_send(sdi, &packet);
    
    sr_err("Failed to start Enhanced protocol acquisition - Jumperless devices do not support SUMP fallback");
    return SR_ERR;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    struct sr_serial_dev_inst *serial = sdi->conn;

    if (!devc->acquisition_running) {
        return SR_OK;
    }

    /* Cleanup acquisition */
    jlms_cleanup_acquisition(sdi);

    /* Send stop command to device */
    jlms_send_command(serial, JUMPERLESS_CMD_RESET, NULL, 0);

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

static struct sr_dev_driver jumperless_mixed_signal_driver_info = {
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
SR_REGISTER_DEV_DRIVER(jumperless_mixed_signal_driver_info);
