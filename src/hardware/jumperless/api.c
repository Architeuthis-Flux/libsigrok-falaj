/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 Shawn Walker <ac0bi00@gmail.com>
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
 #include <math.h>
 #include <stdlib.h>
 #include <string.h>
 #include <strings.h>
 #include <unistd.h>

 #include <libsigrok/libsigrok.h>
 #include "libsigrok-internal.h"
 #include "protocol.h"
 
 #define LOG_PREFIX "api"
 
 #define SERIALCOMM "115200/8n1/dtr=1/rts=0/flow=0"
 
 static const uint32_t scanopts[] = {
     SR_CONF_CONN,
     SR_CONF_SERIALCOMM,
     SR_CONF_FORCE_DETECT
 };
 
 static const uint64_t samplerates[] = {
     SR_HZ(1), SR_HZ(10), SR_HZ(100), SR_HZ(500), SR_KHZ(1), SR_KHZ(5), SR_KHZ(10), SR_KHZ(20),  
     SR_KHZ(50),  SR_KHZ(100), SR_KHZ(200),
      SR_KHZ(400), SR_KHZ(800), 
     SR_MHZ(1),  SR_MHZ(2), 
      SR_MHZ(4), SR_MHZ(8)
    
 };
 
 /* Custom trigger type for internal variables */
#define SR_TRIGGER_INTERNAL_VAR 8

static const uint32_t drvopts[] = {
    SR_CONF_OSCILLOSCOPE,
    SR_CONF_LOGIC_ANALYZER,
};

static const int32_t trigger_matches[] = {
    SR_TRIGGER_ZERO,
    SR_TRIGGER_ONE,
    SR_TRIGGER_RISING,
    SR_TRIGGER_FALLING,
    SR_TRIGGER_EDGE,
    SR_TRIGGER_OVER,
    SR_TRIGGER_UNDER,
};
 
 /* Custom configuration key for control channels */
#define SR_CONF_CONTROL_CHANNELS 0x753F
 
 static const uint32_t devopts[] = {
     SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
     SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
     // SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,  // Pre-trigger capture disabled
     SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
     SR_CONF_CONTROL_CHANNELS | SR_CONF_GET | SR_CONF_SET,
 };

 /* Global channel name mapping: descriptive name -> firmware single-letter type + number */
static const char *channel_names[][3] = {
    /* Analog channels */
    {"ADC_0", "A", "0"}, {"ADC_1", "A", "1"}, {"ADC_2", "A", "2"}, {"ADC_3", "A", "3"},
    {"ADC_4 (0-5V)", "A", "4"}, {"Pad_Sense", "A", "5"}, {"Supply_Monitor", "A", "6"}, {"Probe_Measure", "A", "7"},
    /* Digital channels */
    {"GPIO_1", "D", "1"}, {"GPIO_2", "D", "2"}, {"GPIO_3", "D", "3"}, {"GPIO_4", "D", "4"},
    {"GPIO_5", "D", "5"}, {"GPIO_6", "D", "6"}, {"GPIO_7", "D", "7"}, {"GPIO_8", "D", "8"},
    /* Control channels */
    {"UART_TX", "C", "0"}, {"UART_RX", "C", "1"}, {"Python_D1", "C", "2"}, {"Python_D2", "C", "3"},
    {"Python_D3", "C", "4"}, {"Python_D4", "C", "5"}, {"RP_6", "C", "6"}, {"RP_7", "C", "7"},
    {"DAC_0", "C", "8"}, {"DAC_1", "C", "9"}, {"Python_A1", "C", "10"}, {"Python_A2", "C", "11"},
    {"Python_A3", "C", "12"}, {"Python_A4", "C", "13"}, {"Top_Rail", "C", "14"}, {"Bottom_Rail", "C", "15"},
    {NULL, NULL, NULL}  /* End marker */
};

/* Forward declaration for enable_control_channels function */
static char get_channel_type(const char *channel_name);
static int get_channel_number(const char *channel_name);
static const char *get_channel_name(int channel_index, char channel_type);
static int get_total_channels(void);
static int enable_control_channels(struct sr_dev_inst *sdi, int num_channels);
 
 /* Static variables to store user configuration during session */
 static uint32_t saved_analog_mask = 0;
 static uint32_t saved_digital_mask = 0;
 static uint32_t saved_control_mask = 0;
 static uint64_t saved_sample_rate = 0;
 static uint64_t saved_limit_samples = 0;
 static gboolean config_initialized = FALSE;
 
 /* Load saved analog channel mask */
 static uint32_t load_saved_analog_mask(void)
 {
     if (!config_initialized) {
         return 0xffffffff;  /* Use defaults on first load */
     }
     return saved_analog_mask;
 }
 
 /* Load saved digital channel mask */
 static uint32_t load_saved_digital_mask(void)
 {
     if (!config_initialized) {
         return 0xffffffff;  /* Use defaults on first load */
     }
     return saved_digital_mask;
 }
 
 /* Load saved control channel mask */
 static uint32_t load_saved_control_mask(void)
 {
     if (!config_initialized) {
         return 0xffffffff;  /* Use defaults on first load */
     }
     return saved_control_mask;
 }
 
 /* Load saved sample rate */
 static uint64_t load_saved_sample_rate(void)
 {
     if (!config_initialized) {
         return 0xffffffff;  /* Use defaults on first load */
     }
     return saved_sample_rate;
 }
 
 /* Load saved sample count */
 static uint64_t load_saved_limit_samples(void)
 {
     if (!config_initialized) {
         return 0xffffffff;  /* Use defaults on first load */
     }
     return saved_limit_samples;
 }
 
 /* Save channel masks and timing parameters */
 static void save_channel_masks(uint32_t analog_mask, uint32_t digital_mask, uint32_t control_mask)
 {
     saved_analog_mask = analog_mask;
     saved_digital_mask = digital_mask;
     saved_control_mask = control_mask;
     config_initialized = TRUE;
 }
 
 /* Save timing parameters */
 static void save_timing_params(uint64_t sample_rate, uint64_t limit_samples)
 {
     saved_sample_rate = sample_rate;
     saved_limit_samples = limit_samples;
     config_initialized = TRUE;
 }
 
 static struct sr_dev_driver jumperless_driver_info;
 
 static GSList *scan(struct sr_dev_driver *di, GSList * options)
 {
     struct sr_config *src;
     struct sr_dev_inst *sdi;
     struct sr_serial_dev_inst *serial;
     struct dev_context *devc;
     struct sr_channel *ch;
     GSList *l;
     int num_read;
     int i;
     const char *conn, *serialcomm, *force_detect;
     char buf[32];
     char ustr[64];
     int len;
     uint8_t num_a, num_d, a_size;
     gchar *channel_name;
 
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
             sr_info("Force detect string %s", force_detect);
             break;
        }
    }
    if (!conn)
        return NULL;

    if (!serialcomm)
        serialcomm = SERIALCOMM;

    serial = sr_serial_dev_inst_new(conn, serialcomm);


    /* Retry opening port with backoff to handle "Resource busy" errors */
    int retry_count = 0;
    int max_retries = 0;
    while (retry_count < max_retries) {
        if (serial_open(serial, SERIAL_RDWR) == SR_OK) {
            sr_dbg("Successfully opened port %s on attempt %d", conn, retry_count + 1);
            break;  /* Success! */
        }
        
        retry_count++;
        if (retry_count < max_retries) {
            sr_dbg("Port open attempt %d failed, retrying in %dms...", retry_count, retry_count * 100);
            g_usleep((retry_count * 50) * 1000);  /* Progressive delay: 100ms, 200ms, 300ms, etc. */
        } else {
            sr_dbg("Failed to open port %s after %d attempts - device may be busy or disconnected", conn, max_retries);
            return NULL;
        }
    }
 
     send_serial_char(serial, '*');
     g_usleep(10000);
     do {
         sr_dbg("Draining any existing data from port");
         len = serial_read_blocking(serial, buf, 32, 100);
     } while (len > 0);
     
     if (force_detect && (strlen(force_detect) <= 60)) {
         sprintf(ustr,"i%s\n", force_detect);
         sr_info("User string %s", ustr);
         num_read = send_serial_w_resp(serial, ustr, buf, 17);
     } else {
         num_read = send_serial_w_resp(serial, "i\n", buf, 17);
     }
         if (num_read < 10) {
        sr_dbg("Initial communication failed, attempting recovery...");
        serial_close(serial);
        g_usleep(200000); /* Longer delay for port release */
        
        /* Force close again and wait to ensure port is really released */
        serial_close(serial);
        g_usleep(200000);
        
        if (serial_open(serial, SERIAL_RDWR) != SR_OK) {
            sr_dbg("Failed to reopen serial port %s during recovery - device may be busy", conn);
            serial_close(serial);  /* Ensure port is freed on error */
            return NULL;
        }
         g_usleep(100000);
         send_serial_char(serial, '*');
         g_usleep(100000);
         num_read = send_serial_w_resp(serial, "i\n", buf, 17);
                 if (num_read < 10) {
            sr_dbg("Communication failed after recovery attempt - device may not be ready");
            serial_close(serial);  /* Ensure port is freed on error */
            return NULL;
        }
     }
 
         /* Expected ID response is SRPICO,AxxyDzz,VV
     * where xx are number of analog channels, y is bytes per analog sample
     * (7 bits per byte), zz is number of digital channels, and VV is version "02" */
    /* Accept either SRPICO,A or SRJLV5,A format - be permissive */
    int is_srpico = (strncmp(buf, "SRPICO,A", 8) == 0);
    int is_srjlv5 = (strncmp(buf, "SRJLV5,A", 8) == 0);
    
    if ((num_read < 10) || (!is_srpico && !is_srjlv5)) {
        sr_dbg("Bad response string '%s' (length %d) - expected SRPICO,A or SRJLV5,A format - device may not be ready", buf, num_read);
        serial_close(serial);  /* Ensure port is freed on error */
        return NULL;
    }
    
    /* More permissive validation - just check we have reasonable structure */
    if (num_read < 16) {
        sr_warn("Short response '%s' - proceeding anyway", buf);
    }
 
         /* Parse channel counts more flexibly */
    char *a_start, *d_start;
    if (is_srpico) {
        a_start = &buf[8];   /* SRPICO,A -> start at position 8 */
        d_start = &buf[12];  /* Should be at position 12 */
    } else {
        a_start = &buf[8];   /* SRJLV5,A -> start at position 8 */ 
        d_start = &buf[12];  /* Should be at position 12 */
    }
    
        /* Find the 'D' separator more robustly */
    char *d_pos = strchr(buf, 'D');
    int original_num_a = 0;
    int original_a_size = 0;
    
    if (d_pos && d_pos > a_start) {
        sr_dbg("Full response: '%s'", buf);
        sr_dbg("Analog part starts at position %ld: '%.8s'", a_start - buf, a_start);
        
        /* Parse analog channels (first 2 digits) and bytes per sample (3rd digit)
         * a_start points to the FIRST digit after "...A" (not to 'A').
         */
        if (d_pos - a_start >= 3) {
            char temp_buf[3];
            temp_buf[0] = a_start[0];  /* First analog channel digit */
            temp_buf[1] = a_start[1];  /* Second analog channel digit */
            temp_buf[2] = '\0';
            num_a = atoi(temp_buf);
            original_num_a = num_a;
            a_size = a_start[2] - '0';  /* Third character is bytes per sample (e.g., '2') */
            original_a_size = a_size;
            
            sr_dbg("Parsed analog: channels=%d, bytes_per_sample=%d", num_a, a_size);
            sr_dbg("Raw characters: a_start[1]='%c', a_start[2]='%c', a_start[3]='%c'", 
                   a_start[1], a_start[2], a_start[3]);
            
            /* Validate the parsed values and provide better error messages */
            if (num_a > 20) {  /* More reasonable upper limit before clamping */
                sr_warn("Firmware reported suspiciously high analog channel count (%d) - this may indicate a firmware bug", num_a);
            }
            if (a_size > 10) {  /* More reasonable upper limit before clamping */
                sr_warn("Firmware reported suspiciously high analog sample size (%d bytes) - this may indicate a firmware bug", a_size);
            }
        } else {
            num_a = atoi(a_start);  /* a_start already points at first digit */
            original_num_a = num_a;
            a_size = 2;
            original_a_size = a_size;
            sr_dbg("Fallback parsing: channels=%d, bytes_per_sample=%d", num_a, a_size);
        }
        
        /* Parse digital channels more robustly - find the comma or end */
        char *d_end = strchr(d_pos + 1, ',');
        if (d_end) {
            char temp_d_buf[4];
            int d_len = d_end - (d_pos + 1);
            if (d_len > 0 && d_len < 4) {
                strncpy(temp_d_buf, d_pos + 1, d_len);
                temp_d_buf[d_len] = '\0';
                num_d = atoi(temp_d_buf);
            } else {
                num_d = atoi(d_pos + 1);  /* Fallback */
            }
        } else {
            num_d = atoi(d_pos + 1);  /* No comma found, parse to end */
        }
        sr_dbg("Digital part: '%s', parsed num_d=%d", d_pos + 1, num_d);
        sr_dbg("Parsed: num_a=%d, num_d=%d, a_size=%d", num_a, num_d, a_size);
    sr_info("DEVICE REPORTED: %d analog channels (%d bytes each), %d digital channels", num_a, a_size, num_d);
    sr_dbg("Raw firmware response: '%s'", buf);
    sr_dbg("Parsed values: num_a=%d, a_size=%d, num_d=%d", num_a, a_size, num_d);
    } else {
        sr_warn("Could not parse channel counts from '%s', using defaults", buf);
        num_a = 5;  /* Default Jumperless values */
        num_d = 8;
        a_size = 2;
    }
    
    sr_info("Parsed: %d analog channels (%d bytes each), %d digital channels", num_a, a_size, num_d);
 
     sdi = g_malloc0(sizeof(struct sr_dev_inst));
     sdi->status = SR_ST_INACTIVE;
     sdi->vendor = g_strdup("Architeuthis Flux");
     sdi->model = g_strdup("Jumperless V5");
     sdi->version = g_strdup("01");
     sdi->conn = serial;
          sdi->driver = &jumperless_driver_info;
     sdi->inst_type = SR_INST_SERIAL;
     sdi->serial_num = g_strdup("N/A");

         /* Keep port open for device configuration - will close later */

         /* More permissive channel validation - clamp values instead of rejecting */
    if ((num_a == 0) && (num_d == 0)) {
        sr_warn("No channels reported, using default Jumperless config: 8D 5A");
        num_a = 5;
        num_d = 8;
    }
    
    if (num_a > MAX_ANALOG_CHANNELS) {
        sr_warn("Too many analog channels (%d), clamping to %d (firmware may have a bug)", num_a, MAX_ANALOG_CHANNELS);
        num_a = MAX_ANALOG_CHANNELS;
    }
    
    if (num_d > MAX_DIGITAL_CHANNELS) {
        sr_warn("Too many digital channels (%d), clamping to %d (firmware may have a bug)", num_d, MAX_DIGITAL_CHANNELS);
        num_d = MAX_DIGITAL_CHANNELS;
    }
    
    /* Initialize control channels - these are for firmware control signals */
    int num_c = MAX_CONTROL_CHANNELS;  // Create all possible control channels, user can enable them later
    
    if (a_size < 1 || a_size > 4) {
        sr_warn("Invalid analog size (%d), defaulting to 2 (firmware may have a bug)", a_size);
        a_size = 2;
    }
    
    sr_info("Final channel config: %d analog (%d bytes each), %d digital, %d control", num_a, a_size, num_d, num_c);
    
    /* If we had to clamp values, provide a summary */
    if (num_a != original_num_a || a_size != original_a_size) {
        sr_warn("Channel configuration was adjusted due to firmware reporting invalid values");
        sr_warn("Expected format: SRJLV5,A%%02d2D%%02d,02 (e.g., SRJLV5,A052D08,02)");
    }
 
     devc = g_malloc0(sizeof(struct dev_context));
     devc->a_size = a_size;
     devc->num_a_channels = num_a;
     devc->num_d_channels = num_d;
     devc->num_c_channels = num_c;
     /* Load saved configuration or use defaults */
     devc->a_chan_mask = load_saved_analog_mask();
     devc->d_chan_mask = load_saved_digital_mask();
     devc->c_chan_mask = load_saved_control_mask();
     devc->sample_rate = load_saved_sample_rate();
     devc->limit_samples = load_saved_limit_samples();
     
     /* If no saved configuration exists, use defaults */
     if (devc->a_chan_mask == 0xffffffff) {
         devc->a_chan_mask = ((1 << JULSEVIEW_DEFAULT_ANALOG_CHANNELS) - 1);
     }
     if (devc->d_chan_mask == 0xffffffff) {
         devc->d_chan_mask = ((1 << JULSEVIEW_DEFAULT_DIGITAL_CHANNELS) - 1);
     }
     if (devc->c_chan_mask == 0xffffffff) {
         devc->c_chan_mask = ((1 << JULSEVIEW_DEFAULT_CONTROL_CHANNELS) - 1);
     }
     if (devc->sample_rate == 0xffffffff) {
         devc->sample_rate = 10000;  /* Default 10KHz sample rate */
     }
     if (devc->limit_samples == 0xffffffff) {
         devc->limit_samples = 5000;  /* Default 5K samples */
     }
 
     /* The number of bytes that each digital sample in the buffers sent to the
      * session. All logical channels are packed together, where a slice of N
      * channels takes roundup(N/8) bytes. This never changes even if channels
      * are disabled because PV expects disabled channels to still be accounted
      * for in the packing */
     /* Note: This is now handled below for 7-bit protocol compatibility */
     /* These are the slice sizes of the data on the wire
      * 1 7 bit field per byte */
     devc->bytes_per_slice = (devc->num_a_channels * devc->a_size);
 
         if (devc->num_d_channels > 0) {
        /* logic sent in groups of 7, but check for control channels */
        if (devc->num_d_channels > 8 || devc->c_chan_mask != 0) {
            devc->bytes_per_slice += 3;  // 16 channels or control channels enabled = 3 bytes
        } else {
            devc->bytes_per_slice += 2;  // 8 channels = 2 bytes
        }
    }
     
     /* For 16 channels, we need to handle the 7-bit protocol correctly */
    if (devc->num_d_channels > 8 || devc->c_chan_mask != 0) {
        /* 16 channels: 3 bytes transmitted (21 bits), 2 bytes stored (16 bits) */
        devc->dig_sample_bytes = 2;  // Store as 2 bytes (16 bits)
        devc->num_d_channels = 16;  // Ensure reported digital channels is 16
    } else {
        /* 8 channels: 2 bytes transmitted (14 bits), 1 byte stored (8 bits) */
        devc->dig_sample_bytes = 1;  // Store as 1 byte (8 bits)
        devc->num_d_channels = 8;   // Ensure reported digital channels is 8
    }
         sr_dbg("num channels a %d d %d bps %d dsb %d", num_a, num_d,
        devc->bytes_per_slice, devc->dig_sample_bytes);
    sr_info("BYTES_PER_SLICE CALCULATION: analog=%d*%d=%d, digital=(%d+6)/7=%d, total=%d", 
            num_a, a_size, num_a * a_size, num_d, (num_d + 6) / 7, devc->bytes_per_slice);
           /* Create one digital group containing all digital channels */
     devc->digital_groups = g_malloc0(sizeof(struct sr_channel_group *));
     devc->digital_groups[0] = g_malloc0(sizeof(struct sr_channel_group));
     devc->digital_groups[0]->name = g_strdup("GPIO Channels");
     devc->digital_groups[0]->channels = NULL;
     
     for (i = 0; i < 8; i++) {
         /* Use names from global channel mapping */
         channel_name = g_strdup(channel_names[i + 8][0]);  /* Digital channels start at index 8 */
         /* Enable channels based on saved configuration */
         gboolean enabled = (devc->d_chan_mask >> i) & 1;
         ch = sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, enabled, channel_name);
         devc->digital_groups[0]->channels = g_slist_append(devc->digital_groups[0]->channels, ch);
         g_free(channel_name);
     }
     sdi->channel_groups = g_slist_append(sdi->channel_groups, devc->digital_groups[0]);
      
     /* Create one analog group containing all analog channels */
     devc->analog_groups = g_malloc0(sizeof(struct sr_channel_group *));
     devc->analog_groups[0] = g_malloc0(sizeof(struct sr_channel_group));
     devc->analog_groups[0]->name = g_strdup("ADC Channels");
     devc->analog_groups[0]->channels = NULL;
     
     for (i = 0; i < 8; i++) {
         /* Use names from global channel mapping */
         channel_name = g_strdup(channel_names[i][0]);  /* Analog channels start at index 0 */
         /* Enable channels based on saved configuration */
         gboolean enabled = (devc->a_chan_mask >> i) & 1;
         ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, enabled, channel_name);
         devc->analog_groups[0]->channels = g_slist_append(devc->analog_groups[0]->channels, ch);
         g_free(channel_name);
     }
     sdi->channel_groups = g_slist_append(sdi->channel_groups, devc->analog_groups[0]);
 

    /* Create control channels in 1 group of 8 channels (digital-only) */
    devc->control_groups = g_malloc0(sizeof(struct sr_channel_group *) * 1);
     
    /* Control Group 1: C0-C7 */
     devc->control_groups[0] = g_malloc0(sizeof(struct sr_channel_group));
     devc->control_groups[0]->name = g_strdup("Digital Control Channels");
     devc->control_groups[0]->channels = NULL;
     
     for (i = 0; i < 8; i++) {
         /* Use names from global channel mapping */
        channel_name = g_strdup(channel_names[i + 16][0]);  /* Use control channel names */
        /* Enable channels based on saved configuration */
        gboolean enabled = (devc->c_chan_mask >> i) & 1;
        /* Map control channels into logic indices 8..15 so the 16-bit logic word matches channel indices */
        ch = sr_channel_new(sdi, i + 8, SR_CHANNEL_LOGIC, enabled, channel_name);
         devc->control_groups[0]->channels = g_slist_append(devc->control_groups[0]->channels, ch);
         g_free(channel_name);
     }
     sdi->channel_groups = g_slist_append(sdi->channel_groups, devc->control_groups[0]);
     
    /* No analog control channels; control data are 8 digital bits */
 
         /* STREAMING BUFFER OPTIMIZATION: Large buffer for high-speed data streaming
     * Since the CDC serial implementation can silently lose data as it gets close
     * to full, allocate storage for high-speed streaming scenarios.
     * The large buffer provides more headroom for burst data transmission
     * and reduces the frequency of buffer management operations.
     * For 500kHz sampling at 2 bytes/sample = 1MB/s, this provides ~256ms of buffering */
    devc->serial_buffer_size = 256000; /* DOUBLED: 256KB for maximum streaming performance */
     devc->buffer = NULL;
     sr_dbg("Setting serial buffer size: %i.", devc->serial_buffer_size);
 
     devc->cbuf_wrptr = 0;
     /* While slices are sent as a group of one sample across all channels,
      * sigrok wants analog channel data sent as separate packets. Logical trace
      * values are packed together. An RLE byte in normal mode can represent up
      * to 1640 samples. In D4 an RLE byte can represent up to 640 samples.
      * Rather than making the sample_buf_size 1640x the size of serial buffer,
      * we require that the process loops push samples to the session as we get
      * anywhere close to full. */
 
     devc->sample_buf_size = devc->serial_buffer_size;
     for (i = 0; i < devc->num_a_channels; i++) {
         devc->a_data_bufs[i] = NULL;
         devc->a_pretrig_bufs[i] = NULL;
     }
     devc->d_data_buf = NULL;
     /* Keep previously loaded sample_rate/limit_samples; fall back elsewhere if zero */
     devc->capture_ratio = 0;
     devc->rxstate = RX_IDLE;
 
     sdi->priv = devc;
 
         if (raspberrypi_pico_get_dev_cfg(sdi) != SR_OK) {
        sr_err("Failed to get device configuration");
        serial_close(serial);  /* Ensure port is freed on error */
        return NULL;
    };
 
         /* Close port after all configuration is complete */
    sr_dbg("Closing port after successful device configuration");
    serial_close(serial);
    return std_scan_complete(di, g_slist_append(NULL, sdi));
 }
 
 /* Note that on the initial driver load we pull all values into local storage.
  * Thus gets can return local data, but sets have to issue commands to device. */
 static int config_set(uint32_t key, GVariant * data,
     const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
 {
     struct dev_context *devc;
     int ret;
     (void) cg;
 
     if (!sdi)
         return SR_ERR_ARG;
 
     devc = sdi->priv;
     ret = SR_OK;
 
     sr_dbg("Got config_set key %d \n", key);
     switch (key) {
     case SR_CONF_SAMPLERATE:
         devc->sample_rate = g_variant_get_uint64(data);
         sr_dbg("config_set sr %" PRIu64 "\n", devc->sample_rate);
         save_timing_params(devc->sample_rate, devc->limit_samples);
         break;
     case SR_CONF_LIMIT_SAMPLES:
         devc->limit_samples = g_variant_get_uint64(data);
         sr_dbg("config_set slimit %" PRIu64 "\n", devc->limit_samples);
         save_timing_params(devc->sample_rate, devc->limit_samples);
         break;
     case SR_CONF_CAPTURE_RATIO:
         // Pre-trigger capture disabled - ignore any attempts to set capture ratio
         break;
     case SR_CONF_CONTROL_CHANNELS:
         {
             int num_channels = g_variant_get_int32(data);
             ret = enable_control_channels((struct sr_dev_inst *)sdi, num_channels);
         }
         break;
 
     default:
         sr_err("ERROR: config_set given undefined key %d\n", key);
         ret = SR_ERR_NA;
     }
 
     return ret;
 }
 
 static int config_get(uint32_t key, GVariant ** data,
     const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
 {
     struct dev_context *devc;
 
     sr_dbg("config_get given key %d", key);
 
     (void) cg;
 
     if (!sdi)
         return SR_ERR_ARG;
 
     devc = sdi->priv;
     switch (key) {
     case SR_CONF_SAMPLERATE:
      /* Return default if unset */
      if (devc->sample_rate == 0)
          *data = g_variant_new_uint64(10000);
      else
          *data = g_variant_new_uint64(devc->sample_rate);
         sr_spew("sample rate get of %" PRIu64 "", devc->sample_rate);
         break;
     case SR_CONF_CAPTURE_RATIO:
         // Pre-trigger capture disabled - return 0
         *data = g_variant_new_uint64(0);
         break;
     case SR_CONF_LIMIT_SAMPLES:
      /* Return default if unset */
      if (devc->limit_samples == 0)
          *data = g_variant_new_uint64(10000);
      else
          *data = g_variant_new_uint64(devc->limit_samples);
      sr_spew("config_get limit_samples of %" PRIu64 "", devc->limit_samples);
         break;
     case SR_CONF_CONTROL_CHANNELS:
         *data = g_variant_new_int32(devc->num_c_channels);
         break;
     default:
         sr_spew("unsupported config_get key %d", key);
         return SR_ERR_NA;
     }
     return SR_OK;
 }
 
 static int config_list(uint32_t key, GVariant ** data,
     const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
 {
     (void) cg;
 
     /* Scan or device options are the only ones that can be called without a
      * defined instance */
     if ((key == SR_CONF_SCAN_OPTIONS) || (key == SR_CONF_DEVICE_OPTIONS)) {
         return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
     }
 
         if (!sdi) {
        sr_dbg("Call to config list with null sdi - this is normal for device options");
        if (key == SR_CONF_CONTROL_CHANNELS) {
            *data = std_gvar_tuple_u64(0, MAX_CONTROL_CHANNELS);
            return SR_OK;
        }
        if ((key == SR_CONF_SCAN_OPTIONS) || (key == SR_CONF_DEVICE_OPTIONS)) {
            return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
        }
        return SR_ERR_NA; // Not SR_ERR_ARG!
    }
 
     sr_spew("Start config_list with key %X", key);
     switch (key) {
     case SR_CONF_SAMPLERATE:
         *data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
         break;
     /* This must be set to get SW trigger support */
     case SR_CONF_TRIGGER_MATCH:
         *data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
         break;
     case SR_CONF_LIMIT_SAMPLES:
         /* Really this limit is up to the memory capacity of the host,
          * and users that pick huge values deserve what they get.
          * But setting this limit to prevent really crazy things. */
         *data = std_gvar_tuple_u64(1LL, 1000000000LL);

         break;
     case SR_CONF_CONTROL_CHANNELS:
         /* Control channels range from 0 to MAX_CONTROL_CHANNELS */
         *data = std_gvar_tuple_u64(0, MAX_CONTROL_CHANNELS);
         break;
     default:
         sr_dbg("Reached default statement of config_list");
 
         return SR_ERR_NA;
     }
 
     return SR_OK;
 }
 
 static int dev_acquisition_start(const struct sr_dev_inst *sdi)
 {
     struct sr_serial_dev_inst *serial;
     struct dev_context *devc;
     struct sr_channel *ch;
     struct sr_trigger *trigger;
     char tmpstr[20];
     char buf[32];
     GSList *l;
     int a_enabled = 0, d_enabled = 0, len;
     serial = sdi->conn;
     int i, num_read;
 
     devc = sdi->priv;
     sr_dbg("Enter acq start");
     sr_dbg("dsbstart %d", devc->dig_sample_bytes);
 
     devc->buffer = g_malloc(devc->serial_buffer_size);
     if (!(devc->buffer)) {
         sr_err("ERROR: serial buffer malloc fail");
         return SR_ERR_MALLOC;
     }
 
     /* Get device in idle state */
     if (serial_drain(serial) != SR_OK) {
         sr_err("Initial Drain Failed");
         return SR_ERR;
     }
 
     send_serial_char(serial, '*');
     if (serial_drain(serial) != SR_OK) {
         sr_err("Second Drain Failed");
         return SR_ERR;
     }
 
     for (l = sdi->channels; l; l = l->next) {
         ch = l->data;
         sr_dbg("c %d enabled %d name %s\n", ch->index, ch->enabled, ch->name);
 
         /* Get channel type from name */
         char channel_type = get_channel_type(ch->name);
         
         if (channel_type == 'A') {
             devc->a_chan_mask &= ~(1 << ch->index);
             if (ch->enabled) {
                 devc->a_chan_mask |= (1 << ch->index);
                 a_enabled++;
             }
         }
        else if (channel_type == 'D') {
            /* Digital GPIO channels use indices 0..7 directly */
            int firmware_index = ch->index;  /* 0..7 */
            devc->d_chan_mask &= ~(1 << firmware_index);
            if (ch->enabled) {
                devc->d_chan_mask |= (1 << firmware_index);
                d_enabled++;
            }
        } else if (channel_type == 'C') {
            /* Control channels are mapped into logic indices 8..15; control index is 0..7 */
            int control_index = ch->index - 8;  /* 8..15 -> 0..7 */
            if (control_index < 0) control_index = 0;
            if (control_index > 7) control_index = 7;
            devc->c_chan_mask &= ~(1 << control_index);
            if (ch->enabled) {
                devc->c_chan_mask |= (1 << control_index);
            }
        }
 
         sr_info("Channel enable masks D 0x%X A 0x%X C 0x%X",
             devc->d_chan_mask, devc->a_chan_mask, devc->c_chan_mask);
         
         /* Save the updated configuration */
         save_channel_masks(devc->a_chan_mask, devc->d_chan_mask, devc->c_chan_mask);
         
         /* Send serial command for all analog/digital channels, but only enabled control channels */
         if (channel_type != 0 && (channel_type != 'C' || ch->enabled)) {
             int channel_number = get_channel_number(ch->name);
             if (channel_number >= 0) {
                 /* Convert channel number to firmware format */
                 int firmware_number;
                 if (channel_type == 'D') {
                     firmware_number = channel_number - 1;  /* GPIO_1->0, GPIO_2->1, etc. */
                 } else if (channel_type == 'A') {
                     firmware_number = channel_number;  /* ADC_0->0, ADC_1->1, etc. */
                 } else if (channel_type == 'C') {
                     firmware_number = channel_number;  /* Control channels use their array numbers */
                 } else {
                     firmware_number = channel_number;
                 }
                 
                 sprintf(tmpstr, "%c%d%d\n", channel_type, ch->enabled, firmware_number);
                 if (send_serial_w_ack(serial, tmpstr) != SR_OK) {
                     sr_dbg("Channel enable failed for %s - device may be busy", ch->name);
                     return SR_ERR;
                 }
             } else {
                 sr_dbg("Unknown channel number for: %s", ch->name);
                 continue;
             }
         } else if (channel_type == 0) {
             sr_dbg("Unknown channel type: %s", ch->name);
             continue;
         }
     }
 
         /* Note: Digital channel continuity check removed - firmware handles this automatically */
 
        /* Recalculate bytes_per_slice based on enabled channels */
    devc->bytes_per_slice = (a_enabled * devc->a_size);

    /* Add digital bytes - check for control channels to determine format */
    if (d_enabled > 0) {
        if (devc->num_d_channels > 8 || devc->c_chan_mask != 0) {
            devc->bytes_per_slice += 3;  // 16 channels or control channels enabled = 3 bytes
        } else {
            devc->bytes_per_slice += 2;  // 8 channels = 2 bytes
        }
    }

    /* Ensure digital storage size and reported digital channel count match control usage */
    if (d_enabled > 0) {
        if (devc->c_chan_mask != 0) {
            devc->num_d_channels = 16;           /* GPIO (8) + Control (8) */
            devc->dig_sample_bytes = 2;          /* store 16 bits per sample */
        } else {
            devc->num_d_channels = 8;            /* GPIO only */
            devc->dig_sample_bytes = 1;          /* store 8 bits per sample */
        }
    } else {
        devc->dig_sample_bytes = 0;
    }
 
     if ((a_enabled == 0) && (d_enabled == 0)) {
         sr_dbg("No channels enabled - this is normal if device is not ready");
         return SR_ERR;
     }
 
    sr_dbg("bps %d\n", devc->bytes_per_slice);
    sr_info("ACQ START: bytes_per_slice=%d, dig_sample_bytes=%d, num_d_channels=%d (analog=%d*%d=%d, digital=%d bytes)",
            devc->bytes_per_slice, devc->dig_sample_bytes, devc->num_d_channels,
            a_enabled, devc->a_size, a_enabled * devc->a_size,
            (d_enabled > 0) ? ((devc->num_d_channels > 8 || devc->c_chan_mask != 0) ? 3 : 2) : 0);
 
     /* Apply sample rate limits; while earlier versions forced a lower sample
      * rate, the PICO seems to allow ADC overclocking, and by not enforcing
      * these limits it may support other devices. Thus call sr_err to get
      * something into the device logs, but allowing it to progress. */
     if ((a_enabled == 3) && (devc->sample_rate > 160000))
         sr_dbg("Note: 3 channel ADC sample rate above 160khz");
     if ((a_enabled == 2) && (devc->sample_rate > 250000))
         sr_dbg("Note: 2 channel ADC sample rate above 250khz");
     if ((a_enabled == 1) && (devc->sample_rate > 500000))
         sr_dbg("Note: 1 channel ADC sample rate above 500khz");
 
     /* Depending on channel configs, rates below 5ksps are possible but such a
      * low rate can easily stream and this eliminates a lot	of special cases. */
    //  if (devc->sample_rate < 500) {
    //      sr_dbg("Sample rate adjusted to minimum of 500sps");
    //      devc->sample_rate = 500;
    //  }
 
     /* While PICO specs a max clock ~120-125Mhz, it does overclock in many cases
      * so leaving a warning. */
     if (devc->sample_rate > 150000000)
         sr_warn("WARN: Sample rate above 150Msps");
 
     /* It may take a very large number of samples to notice, but if digital and
      * analog are enabled and either PIO or ADC are fractional the samples will
      * skew over time. 24Mhz is the max common divisor to the 150Mhz and 48Mhz
      * ADC clock so force an integer divisor to 24Mhz. */
    //  if ((a_enabled > 0) && (d_enabled > 0)) {
    //      if (24000000ULL % (devc->sample_rate)) {
    //          uint32_t commondivint = 24000000ULL / (devc->sample_rate);
    //          /* Always increment the divisor so that we go down in frequency to
    //           * avoid max sample rate issues */
    //          commondivint++;
    //          devc->sample_rate = 24000000ULL / commondivint;
    //          /* Make sure the divisor increment didn't make us go too low. */
    //          if (devc->sample_rate < 1)
    //              devc->sample_rate = 1;
    //          sr_warn("WARN: Forcing common integer divisor sample rate of " \
    //              "%" PRIu64 " div %u", devc->sample_rate, commondivint);
    //      }
    //  }
 
     /* If we are only digital or only analog print a warning that the fractional
      * divisors aren't a true PLL fractional feedback loop and thus could have
      * sample to sample variation. These warnings of course assume that the
      * device is programmed with the expected ratios but non PICO
      * implementations, or PICO implementations that use different divisors
      * could avoid. This generally won't be a problem because most of the
      * sample_rate pulldown values are integer divisors. */
     if ((a_enabled > 0) && (48000000ULL % (devc->sample_rate * a_enabled)))
         sr_warn("WARN: Non integer ADC divisor of 48Mhz clock for sample " \
             "rate %" PRIu64 " may cause sample to sample variability.",
             devc->sample_rate);
     if ((d_enabled > 0) && (150000000ULL % (devc->sample_rate)))
         sr_warn("WARN: Non integer PIO divisor of 120Mhz for sample rate " \
             "%" PRIu64 " may cause sample to sample variability.", devc->sample_rate);
 
     sprintf(tmpstr, "L%" PRIu64 "\n", devc->limit_samples);
     if (send_serial_w_ack(serial, tmpstr) != SR_OK) {
         sr_err("Sample limit to device failed");
         return SR_ERR;
     }
 
     /* To support future devices that may allow the analog scale/offset to
      * change, call get_dev_cfg again to get new values */
     if (raspberrypi_pico_get_dev_cfg(sdi) != SR_OK) {
         sr_err("get_dev_cfg failure on start");
         return SR_ERR;
     }
 
     /* With all other params set, we use the final sample rate setting as an
      * opportunity for the device to communicate any errors in configuration.
      * A single  "*" indicates success.
      * A "*" with subsequent data is success, but allows for the device to
      * print something to the error console without aborting.
      * A non "*" in the first character blocks the start.
      * The firmware now also sends the decimation factor in the format "*<factor>". */
     sprintf(tmpstr, "R%" PRIu64 "\n", devc->sample_rate);
     num_read = send_serial_w_resp(serial, tmpstr, buf, 30);
     buf[num_read] = 0;
     if ((num_read > 1) && (buf[0] == '*')) {
         sr_dbg("Sample rate to device success with resp %s", buf);
         
         /* Parse decimation factor from response */
         if (num_read > 1) {
             char *decimation_str = buf + 1; /* Skip the '*' */
             int decimation_factor = atoi(decimation_str);
                                                if (decimation_factor > 0) {
                      devc->analog_decimation_factor = decimation_factor;
                      devc->decimation_mode_active = (decimation_factor > 1);
                      sr_info("Received decimation factor from firmware: %d (mode: %s)", 
                              decimation_factor, devc->decimation_mode_active ? "active" : "normal");
                  } else {
                      devc->analog_decimation_factor = 1;
                      devc->decimation_mode_active = FALSE;
                       sr_info("No decimation factor received, using normal mode");
                   }
         }
     } else if (!((num_read == 1) && (buf[0] == '*'))) {
         sr_err("Sample rate to device failed");
         if (num_read > 0) {
             buf[num_read]=0;
             sr_err("sample_rate error string %s",buf);
         }
         return SR_ERR;
     }
 
     devc->sent_samples = 0;
     devc->byte_cnt = 0;
     devc->bytes_avail = 0;
     devc->wrptr = 0;
     devc->cbuf_wrptr = 0;
     len = serial_read_blocking(serial, devc->buffer, devc->serial_buffer_size,
         serial_timeout(serial, 4));
 
     if (len > 0) {
         sr_info("Pre-ARM drain had %d characters:", len);
         devc->buffer[len] = 0;
         sr_info("%s", devc->buffer);
     }
 
     for (i = 0; i < devc->num_a_channels; i++) {
         devc->a_data_bufs[i] = g_malloc(devc->sample_buf_size * sizeof(float));
         if (!(devc->a_data_bufs[i])) {
             sr_err("ERROR: analog buffer malloc fail");
             return SR_ERR_MALLOC;
         }
     }
 
     if (devc->num_d_channels > 0) {
         devc->d_data_buf = g_malloc(devc->sample_buf_size *
             devc->dig_sample_bytes);
         if (!(devc->d_data_buf)) {
             sr_err("ERROR: logic buffer malloc fail");
             return SR_ERR_MALLOC;
         }
     }
 
     // Disable pre-trigger capture entirely - always set to 0
    devc->pretrig_entries = 0;
     /* While the driver supports the passing of trigger info to the device
      * it has been found that the sw overhead of supporting triggering and
      * pretrigger buffer entries etc.. ends up slowing the cores down enough
      * that the effective continous sample rate isn't much higher than that of
      * sending untriggered samples across USB.  Thus this code will remain but
      * likely may not be used by the device, unless HW based triggers are
      * implemented */
     if ((trigger = sr_session_trigger_get(sdi->session))) {
         if (g_slist_length(trigger->stages) > 1)
             return SR_ERR_NA;
 
         struct sr_trigger_stage *stage;
         struct sr_trigger_match *match;
         GSList *l;
         stage = g_slist_nth_data(trigger->stages, 0);
         if (!stage)
             return SR_ERR_ARG;
         for (l = stage->matches; l; l = l->next) {
             match = l->data;
             if (!match->match)
                 continue;
             if (!match->channel->enabled)
                 continue;
             int idx = match->channel->index;
             int8_t val;
             switch(match->match) {
             case SR_TRIGGER_ZERO:
                 val = 0; break;
             case SR_TRIGGER_ONE:
                 val = 1; break;
             case SR_TRIGGER_RISING:
                 val = 2; break;
             case SR_TRIGGER_FALLING:
                 val = 3; break;
             case SR_TRIGGER_EDGE:
                 val = 4; break;
             case SR_TRIGGER_OVER:
                 val = 6; break;
             case SR_TRIGGER_UNDER:
                 val = 7; break;
             default:
                 val = -1;
             }
             sr_info("Trigger value idx %d match %d", idx, match->match);
                         
             /* Handle different trigger types */
             if (val >= 0) {
                 if (val <= 4) {
                     /* Digital triggers (ZERO, ONE, RISING, FALLING, EDGE) */
                     if ((devc->d_chan_mask >> idx) & 1) {
                         sprintf(&tmpstr[0], "t%d%02d\n", val, idx+2);
                         if (send_serial_w_ack(serial, tmpstr) != SR_OK) {
                             sr_err("Digital trigger cfg to device failed");
                             return SR_ERR;
                         }
                     }
                 } else if (val == 6 || val == 7) {
                     /* Analog triggers (OVER, UNDER) */
                     if ((devc->a_chan_mask >> idx) & 1) {
                         /* For analog triggers, we need to include the threshold value */
                         /* The threshold should be in match->value, but we'll use a default for now */
                         uint16_t threshold = 2048; /* Default threshold - should come from match->value */
                         sprintf(&tmpstr[0], "t%d%02d%04d\n", val, idx, threshold);
                         if (send_serial_w_ack(serial, tmpstr) != SR_OK) {
                             sr_err("Analog trigger cfg to device failed");
                             return SR_ERR;
                         }
                     }

             }
            }
         }
        
         sprintf(&tmpstr[0], "p%d\n", devc->pretrig_entries);
         if (send_serial_w_ack(serial, tmpstr) != SR_OK) {
             sr_err("Pretrig to device failed");
             return SR_ERR;
         }
 
         devc->stl = soft_trigger_logic_new(sdi, trigger, devc->pretrig_entries);
         if (!devc->stl)
             return SR_ERR_MALLOC;
 
         devc->trigger_fired = TRUE;
         if (devc->pretrig_entries > 0) {
             sr_dbg("Allocating pretrig buffers size %d", devc->pretrig_entries);
             for (i = 0; i < devc->num_a_channels; i++) {
                 if ((devc->a_chan_mask >> i) & 1) {
                     devc->a_pretrig_bufs[i] = g_malloc0(sizeof(float) *
                         devc->pretrig_entries);
                     if (!devc->a_pretrig_bufs[i]) {
                         sr_err("ERROR:Analog pretrigger buffer malloc " \
                             "failure, disabling");
                         devc->trigger_fired = TRUE;
                     }
                 }
             }
         }
 
                 sr_info("Entering sw triggered mode");
        /* Post the receive before starting the device to ensure we are ready
         * to receive data ASAP - AGGRESSIVE optimization for high-speed streaming */
        serial_source_add(sdi->session, serial, G_IO_IN, 10, /* AGGRESSIVE: 10ms for maximum streaming performance */
            raspberrypi_pico_receive, (void*)sdi);
 
         sprintf(tmpstr, "F\n");
         if (send_serial_str(serial, tmpstr) != SR_OK)
             return SR_ERR;
 
     } else {
                 devc->trigger_fired = TRUE;
        devc->pretrig_entries = 0;
        sr_info("Entering fixed sample mode");
        serial_source_add(sdi->session, serial, G_IO_IN, 10, /* AGGRESSIVE: 10ms for maximum streaming performance */
            raspberrypi_pico_receive, (void*)sdi);
 
         sprintf(tmpstr, "F\n");
         if (send_serial_str(serial, tmpstr) != SR_OK)
             return SR_ERR;
     }
 
     std_session_send_df_header(sdi);
 
     sr_dbg("dsbstartend %d", devc->dig_sample_bytes);
 
     if (devc->trigger_fired)
         std_session_send_df_trigger(sdi);
 
     /* Keep this at the end as we don't want to be RX_ACTIVE unless everything
      * is ok */
     devc->rxstate = RX_ACTIVE;
 
     return SR_OK;
 }
 
 /* This function is called either by the protocol code if we reached all of the
  * samples or an error condition, and also by the user clicking stop in
  * pulseview. It must always be called for any acquistion that was started to
  * free memory. */
 static int dev_acquisition_stop(struct sr_dev_inst *sdi)
 {
     struct dev_context *devc;
     struct sr_serial_dev_inst *serial;
     int len;
     devc = sdi->priv;
     serial = sdi->conn;
 
     sr_dbg("At dev_acquisition_stop");
 
     std_session_send_df_end(sdi);
 
     /* If we reached this while still active it is likely because the stop
      * button was pushed in pulseview. That is generally some kind of error
      * condition, so we don't try to check the bytenct */
     if (devc->rxstate == RX_ACTIVE)
         sr_err("Reached dev_acquisition_stop in RX_ACTIVE");
 
     if (devc->rxstate != RX_IDLE) {
         uint64_t current_time = g_get_monotonic_time();
         uint64_t time_since_last = current_time - devc->last_stop_command_time;
         
         /* Rate limit: only send + command every 250ms */
         if (time_since_last >= 250000) { /* 250ms in microseconds */
             sr_err("Sending plus to stop device stream");
             send_serial_char(serial, '+');
             devc->last_stop_command_time = current_time;
         } else {
             sr_dbg("Rate limiting + command (last sent %llu us ago)", (unsigned long long)time_since_last);
         }
     }
 
     /* In case we get calls to receive force it to exit */
     devc->rxstate = RX_IDLE;
 
     /* Drain data from device so that it doesn't confuse subsequent commands */
     do {
         len = serial_read_blocking(serial, devc->buffer,
             devc->serial_buffer_size, 100);
         if (len)
             sr_err("Dropping %d device bytes", len);
     } while (len > 0);
 
     if (devc->buffer) {
         g_free(devc->buffer);
         devc->buffer = NULL;
     }
 
     for (int i = 0; i < devc->num_a_channels; i++) {
         if (devc->a_data_bufs[i]) {
             g_free(devc->a_data_bufs[i]);
             devc->a_data_bufs[i] = NULL;
         }
     }
     if (devc->d_data_buf) {
         g_free(devc->d_data_buf);
         devc->d_data_buf = NULL;
     }
 
     for (int i = 0; i < devc->num_a_channels; i++) {
         if (devc->a_pretrig_bufs[i])
             g_free(devc->a_pretrig_bufs[i]);
         devc->a_pretrig_bufs[i] = NULL;
     }
 
     serial = sdi->conn;
     serial_source_remove(sdi->session, serial);
 
         return SR_OK;
}

/* Function to enable control channels for firmware control signals */
static char get_channel_type(const char *channel_name)
{

        /* Use global channel name mapping */

    /* Find channel type from name mapping */
    for (int i = 0; channel_names[i][0] != NULL; i++) {
        if (strcmp(channel_name, channel_names[i][0]) == 0) {
            return channel_names[i][1][0];
        }
    }
    
    return 0;  /* Unknown channel type */
}

static int get_channel_number(const char *channel_name)
{
    /* Find channel number from global name mapping */
    for (int i = 0; channel_names[i][0] != NULL; i++) {
        if (strcmp(channel_name, channel_names[i][0]) == 0) {
            return atoi(channel_names[i][2]);
        }
    }
    
    return -1;  /* Unknown channel */
}

static const char *get_channel_name(int channel_index, char channel_type)
{
    /* Find channel name by type and index from global mapping */
    for (int i = 0; channel_names[i][0] != NULL; i++) {
        if (channel_names[i][1][0] == channel_type && atoi(channel_names[i][2]) == channel_index) {
            return channel_names[i][0];
        }
    }
    
    return NULL;  /* Unknown channel */
}

static int get_total_channels(void)
{
    /* Count total channels in global array */
    int count = 0;
    for (int i = 0; channel_names[i][0] != NULL; i++) {
        count++;
    }
    return count;
}

static int enable_control_channels(struct sr_dev_inst *sdi, int num_channels)
{
    struct dev_context *devc;
    struct sr_serial_dev_inst *serial;
    struct sr_channel *ch;
    int i;
    
    if (!sdi || !sdi->priv) {
        sr_err("Invalid device instance for control channel enable");
        return SR_ERR_ARG;
    }
    
    devc = sdi->priv;
    serial = sdi->conn;
    
    if (num_channels < 0 || num_channels > MAX_CONTROL_CHANNELS) {
        sr_dbg("Invalid number of control channels: %d (max: %d)", 
               num_channels, MAX_CONTROL_CHANNELS);
        return SR_ERR_ARG;
    }
    
    /* Update device context */
    devc->c_chan_mask = ((1 << num_channels) - 1);
    
    /* Save the updated control channel mask */
    save_channel_masks(devc->a_chan_mask, devc->d_chan_mask, devc->c_chan_mask);
    
    /* Enable/disable control channels based on the mask */
    for (i = 0; i < devc->num_c_channels; i++) {
        struct sr_channel_group *group;
        int group_index = i / 8;  // Which group (0 or 1)
        int channel_in_group = i % 8;  // Which channel within the group
        
        if (group_index < 2 && devc->control_groups[group_index]) {
            group = devc->control_groups[group_index];
            ch = g_slist_nth_data(group->channels, channel_in_group);
            if (ch) {
                if (i < num_channels) {
                    /* Enable this control channel */
                    ch->enabled = TRUE;
                    sr_dbg("Enabled control channel %d (%s)", i, ch->name);
                } else {
                    /* Disable this control channel */
                    ch->enabled = FALSE;
                    sr_dbg("Disabled control channel %d (%s)", i, ch->name);
                }
            }
        }
    }
    
    /* Send control channel enable command to firmware */
    char cmd[32];
    sprintf(cmd, "E%d\n", num_channels);  // Enable control channels command
    if (send_serial_w_ack(serial, cmd) != SR_OK) {
        sr_dbg("Failed to enable control channels on device - firmware may not support this feature");
        return SR_ERR;
    }
    
    sr_info("Updated control channels: %d enabled", num_channels);
    return SR_OK;
}

/* Function to update control channel names from firmware */
SR_PRIV int update_control_channel_name(struct sr_dev_inst *sdi, int channel_index, const char *new_name)
{
    struct dev_context *devc;
    struct sr_channel *ch;
    struct sr_channel_group *group;
    int group_index, channel_in_group;
    
    if (!sdi || !sdi->priv || !new_name) {
        sr_err("Invalid parameters for control channel name update");
        return SR_ERR_ARG;
    }
    
    if (channel_index < 0 || channel_index >= MAX_CONTROL_CHANNELS) {
        sr_err("Invalid control channel index: %d", channel_index);
        return SR_ERR_ARG;
    }
    
    devc = sdi->priv;
    group_index = channel_index / 8;  // Which group (0 or 1)
    channel_in_group = channel_index % 8;  // Which channel within the group
    
    if (group_index >= 2 || !devc->control_groups[group_index]) {
        sr_err("Invalid control group for channel %d", channel_index);
        return SR_ERR_ARG;
    }
    
    group = devc->control_groups[group_index];
    ch = g_slist_nth_data(group->channels, channel_in_group);
    
    if (!ch) {
        sr_err("Control channel %d not found", channel_index);
        return SR_ERR_ARG;
    }
    
    /* Update the channel name */
    if (ch->name) {
        g_free(ch->name);
    }
    ch->name = g_strdup(new_name);
    
    sr_dbg("Updated control channel %d name to: %s", channel_index, new_name);
    return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
    /* Use standard serial device close - let libsigrok handle cleanup */
    return std_serial_dev_close(sdi);
}

static struct sr_dev_driver jumperless_driver_info = {
    .name = "jumperless",
    .longname = "Jumperless V5",
     .api_version = 1,
     .init = std_init,
     .cleanup = std_cleanup,
     .scan = scan,
     .dev_list = std_dev_list,
     .dev_clear = std_dev_clear,
     .config_get = config_get,
     .config_set = config_set,
     .config_list = config_list,
     .dev_open = std_serial_dev_open,
     .dev_close = dev_close,
     .dev_acquisition_start = dev_acquisition_start,
     .dev_acquisition_stop = dev_acquisition_stop,
     .context = NULL,
 };
 
 SR_REGISTER_DEV_DRIVER(jumperless_driver_info);
