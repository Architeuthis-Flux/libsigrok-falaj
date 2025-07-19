/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024 Jumperless Project
 * Based on BP5 FALA implementation by Patrick Dussud <phdussud@hotmail.com>
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
};

static const uint32_t drvopts[] = {
        SR_CONF_LOGIC_ANALYZER,
        SR_CONF_OSCILLOSCOPE,  /* For mixed-signal support */
};

static const uint32_t devopts[] = {
        SR_CONF_CONTINUOUS,
        SR_CONF_CONN | SR_CONF_GET,
        SR_CONF_SAMPLERATE | SR_CONF_GET,
        //SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static struct sr_dev_driver jumperless_fala_driver_info;

/* Jumperless logic analyzer channels (based on available GPIO) */
SR_PRIV const char *jumperless_channel_names[] = {
        "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
        "B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7",
        "C0", "C1", "C2", "C3", "C4", "C5", "C6", "C7",
        "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
};

/* Jumperless analog channels (ADC capable pins) */
SR_PRIV const char *jumperless_analog_names[] = {
        "ADC0", "ADC1", "ADC2", "ADC3",
};

static gboolean next_field(const char **buf)
{
        char *f1 = strchr(*buf, ';');
        if (!f1)
                return FALSE;
        *buf = f1 + 1;
        return TRUE;
}

SR_PRIV gboolean parse_header(const char *buf, fala_header *hd)
{
        /* Jumperless FALA header format: $JFALADATA;logic_ch;analog_ch;trigger_ch_mask;trigger_mask;edge;rate;count;pre_trigger; */
        if (strncmp(buf, "$JFALADATA", 10))
                return FALSE;
        buf += 10;
        if (next_field(&buf))
                hd->n_channels = atoi(buf);
        else
                return FALSE;
        if (next_field(&buf))
                hd->analog_channels = atoi(buf);
        else
                return FALSE;
        if (next_field(&buf))
                hd->trigger_channel_mask = atoi(buf);
        else
                return FALSE;
        if (next_field(&buf))
                hd->trigger_mask = atoi(buf);
        else
                return FALSE;
        if (next_field(&buf))
                hd->edge_trigger = (buf[0] == 'Y') ? TRUE : FALSE;
        else
                return FALSE;
        if (next_field(&buf))
                hd->sample_rate = atoi(buf);
        else
                return FALSE;
        if (next_field(&buf))
                hd->sample_count = atoi(buf);
        else
                return FALSE;
        if (next_field(&buf))
                hd->before_trigger_sample_count = atoi(buf);
        else
                return FALSE;
        
        /* Set mixed signal flag if analog channels are present */
        hd->mixed_signal = (hd->analog_channels > 0) ? TRUE : FALSE;
        return TRUE;
}

static GSList *
scan(struct sr_dev_driver *di, GSList *options)
{
        struct drv_context *drvc;
        (void)options;

        drvc = di->context;
        drvc->instances = NULL;

        struct sr_config *src;
        struct sr_dev_inst *sdi;
        struct sr_serial_dev_inst *serial;
        struct dev_context *devc;
        size_t i;
        GSList *l;
        struct fala_header hd;
        const char *conn, *serialcomm, *force_detect;
        char buf[128];
        int len;
        size_t ch_max;
        char **channel_names = sr_parse_probe_names(NULL, jumperless_channel_names, ARRAY_SIZE(jumperless_channel_names),
                                                                                                ARRAY_SIZE(jumperless_channel_names), &ch_max);
        conn = serialcomm = force_detect = NULL;
        for (l = options; l; l = l->next)
        {
                src = l->data;
                switch (src->key)
                {
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
        sr_info("Opening %s.", conn);
        if (serial_open(serial, SERIAL_RDWR) != SR_OK)
        {
                sr_err("1st serial open fail");
                return NULL;
        }
        
        /* Send Jumperless FALA query */
        if (serial_write_blocking(serial, "?fala", 5, 100) != 5)
        {
                serial_close(serial);
                return NULL;
        }
        len = serial_read_blocking(serial, buf, ARRAY_SIZE(buf), 500); 
        if (len < 20 || !parse_header(buf, &hd))
        {
                sr_err("Jumperless FALA identify failed");
                serial_close(serial);
                return NULL;
        }

        sdi = g_malloc0(sizeof(struct sr_dev_inst));
        sdi->status = SR_ST_INACTIVE;
        sdi->vendor = g_strdup("Jumperless");
        sdi->model = g_strdup("FALA Logic Analyzer");
        sdi->version = g_strdup("1.0");
        sdi->inst_type = SR_INST_SERIAL;
        sdi->conn = serial;
        sdi->connection_id = g_strdup(serial->port);
        devc = g_malloc0(sizeof(struct dev_context));
        sdi->priv = devc;
        devc->cur_samplerate = hd.sample_rate;
        devc->num_logic_channels = hd.n_channels;
        devc->num_analog_channels = hd.analog_channels;
        devc->mixed_signal_mode = hd.mixed_signal;
        devc->logic_unitsize = 1;
        devc->all_logic_channels_mask = 1UL << 0;
        devc->all_logic_channels_mask <<= devc->num_logic_channels;
        devc->all_logic_channels_mask--;
        devc->limit_samples = hd.sample_count;

        /* Add logic channels */
        for (i = 0; i < MIN(ch_max, devc->num_logic_channels); i++)
        {
                sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE,
                                           channel_names[i]);
        }
        
        /* Add analog channels if in mixed signal mode */
        if (devc->mixed_signal_mode && devc->num_analog_channels > 0)
        {
                for (i = 0; i < MIN(ARRAY_SIZE(jumperless_analog_names), devc->num_analog_channels); i++)
                {
                        sr_channel_new(sdi, i + devc->num_logic_channels, SR_CHANNEL_ANALOG, TRUE,
                                                   jumperless_analog_names[i]);
                }
        }
        
        sr_free_probe_names(channel_names);

        serial_close(serial);
        return std_scan_complete(di, g_slist_append(NULL, sdi));
}

static int config_get(uint32_t key, GVariant **data,
                                          const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
        int ret;

        struct dev_context *devc = (sdi) ? sdi->priv : NULL;
        (void)data;
        (void)cg;

        ret = SR_OK;
        switch (key)
        {
        case SR_CONF_SAMPLERATE:
                *data = g_variant_new_uint64(devc->cur_samplerate);
                break;
        case SR_CONF_LIMIT_SAMPLES:
                *data = g_variant_new_uint64(devc->limit_samples);
                break;
        default:
                return SR_ERR_NA;
        }

        return ret;
}

static int config_set(uint32_t key, GVariant *data,
                                          const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
        int ret;

        struct dev_context *devc = (sdi) ? sdi->priv : NULL;
        (void)data;
        (void)cg;
        uint64_t tmp_u64;

        ret = SR_OK;
        switch (key)
        {
        case SR_CONF_LIMIT_SAMPLES:
                tmp_u64 = g_variant_get_uint64(data);
                if (tmp_u64 < MIN_NUM_SAMPLES)
                        return SR_ERR;
                devc->limit_samples = tmp_u64;
                break;
        default:
                ret = SR_ERR_NA;
        }

        return ret;
}

static int config_list(uint32_t key, GVariant **data,
                                           const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{

        (void)data;
        (void)cg;
        struct dev_context *devc;

        devc = (sdi) ? sdi->priv : NULL;

        switch (key)
        {
        case SR_CONF_SCAN_OPTIONS:
        case SR_CONF_DEVICE_OPTIONS:
                return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts,
                                                           devopts);
        case SR_CONF_LIMIT_SAMPLES:
                if (!sdi)
                        return SR_ERR_ARG;
                devc = sdi->priv;
                if (devc->limit_samples == 0)
                        /* Device didn't specify sample memory size in metadata. */
                        return SR_ERR_NA;
                else
                        *data = std_gvar_tuple_u64(MIN_NUM_SAMPLES, devc->limit_samples);
                break;
        default:
                return SR_ERR_NA;
        }

        return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
        struct dev_context *devc;
        struct sr_serial_dev_inst *serial;

        devc = sdi->priv;
        serial = sdi->conn;

        /* Reset all operational states. */
        devc->num_samples = 0;
        devc->num_transfers = 0;
        devc->logic_unitsize = 1;
        std_session_send_df_header(sdi);

        /* If the device stops sending for longer than it takes to send a byte,
         * that means it's finished. But wait at least 100 ms to be safe.
         */
        return serial_source_add(sdi->session, serial, G_IO_IN, 100,
                                                         jumperless_fala_receive_data, (struct sr_dev_inst *)sdi);
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
        struct sr_serial_dev_inst *serial;

        serial = sdi->conn;
        serial_source_remove(sdi->session, serial);
        std_session_send_df_end(sdi);
        
        return SR_OK;
}

static struct sr_dev_driver jumperless_fala_driver_info = {
        .name = "jumperless-fala",
        .longname = "Jumperless FALA Logic Analyzer",
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
        .dev_close = std_serial_dev_close,
        .dev_acquisition_start = dev_acquisition_start,
        .dev_acquisition_stop = dev_acquisition_stop,
        .context = NULL,
};
SR_REGISTER_DEV_DRIVER(jumperless_fala_driver_info);
