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

#include <config.h>
#include "protocol.h"

SR_PRIV int jumperless_fala_receive_data(int fd, int revents, void *cb_data)
{
        struct sr_dev_inst *sdi;
        struct dev_context *devc;
        char read_buffer[1024];

        struct sr_serial_dev_inst *serial;
        struct sr_datafeed_packet packet;
        struct sr_datafeed_logic logic;
        struct sr_datafeed_analog analog;
        struct sr_analog_encoding encoding;
        struct sr_analog_meaning meaning;
        struct sr_analog_spec spec;
        int len;
        int offset;
        (void)fd;
        int k;
        uint8_t sample;
        float analog_sample;

        sdi = cb_data;
        if (!sdi)
                return TRUE;

        devc = sdi->priv;
        serial = sdi->conn;
        if (!devc)
                return TRUE;

        if (revents == G_IO_IN) {
                if (devc->num_transfers == 0)
                {
                        fala_header hd;
                        len = serial_read_blocking(serial, read_buffer, 1024, 10);
                        if (len == 0)
                                return TRUE;
                        else if (len < 20 || !jumperless_parse_header(read_buffer, &hd))
                        {
                                sr_err("bad header");
                                sdi->driver->dev_acquisition_stop(sdi);
                                return FALSE;
                        }
                        else
                        {
                                devc->limit_samples = hd.sample_count;
                                devc->num_logic_channels = hd.n_channels;
                                devc->num_analog_channels = hd.analog_channels;
                                devc->mixed_signal_mode = hd.mixed_signal;
                                devc->cur_samplerate = hd.sample_rate;
                                devc->before_trigger_sample_count = hd.before_trigger_sample_count;
                                devc->trigger_channel_mask = hd.trigger_channel_mask;
                                devc->trigger_mask = hd.trigger_mask;
                        }
                        devc->raw_sample_buf = g_try_malloc(devc->limit_samples * (devc->logic_unitsize + devc->num_analog_channels * sizeof(float)));
                        if (!devc->raw_sample_buf)
                        {
                                sr_err("Sample buffer malloc failed.");
                                sdi->driver->dev_acquisition_stop(sdi);
                                return FALSE;
                        }
                        std_session_send_df_frame_begin(sdi);
                        sr_session_send_meta(sdi, SR_CONF_SAMPLERATE,
                                                                 g_variant_new_uint64(devc->cur_samplerate));
                        if (serial_write_blocking(serial, "+", 1, 100) != 1)
                                return FALSE;
                        //next packet will be data
                        devc->num_transfers++;
                        return TRUE;
                }
                devc->num_transfers++;
                if(devc->num_samples < devc->limit_samples)     {
                        len = serial_read_blocking(serial, read_buffer, 1024, 10);
                        if (len <= 0)
                        {
                                if (len == 0)
                                {
                                        return TRUE;
                                }
                                else
                                {
                                        sr_err("ERROR: Negative serial read code %d", len);
                                        sdi->driver->dev_acquisition_stop(sdi);
                                        return FALSE;
                                }
                        }
                        
                        /* Process received data based on mixed signal mode */
                        if (devc->mixed_signal_mode)
                        {
                                /* Mixed signal mode: interleaved logic and analog data */
                                int sample_size = devc->logic_unitsize + devc->num_analog_channels * sizeof(float);
                                for (k = 0; k < len; k += sample_size)
                                {
                                        if (k + sample_size > len)
                                                break; /* Incomplete sample, wait for more data */
                                        
                                        devc->num_samples++;
                                        offset = (devc->limit_samples - devc->num_samples) * sample_size;
                                        
                                        /* Copy logic sample */
                                        sample = read_buffer[k];
                                        devc->raw_sample_buf[offset] = sample;
                                        
                                        /* Copy analog samples if present */
                                        if (devc->num_analog_channels > 0)
                                        {
                                                for (int i = 0; i < devc->num_analog_channels; i++)
                                                {
                                                        memcpy(&analog_sample, &read_buffer[k + devc->logic_unitsize + i * sizeof(float)], sizeof(float));
                                                        memcpy(&devc->raw_sample_buf[offset + devc->logic_unitsize + i * sizeof(float)], &analog_sample, sizeof(float));
                                                }
                                        }
                                        
                                        if (devc->num_samples >= devc->limit_samples)
                                        {
                                                /* Send logic data */
                                                int num_pre_trigger_samples = MIN(devc->before_trigger_sample_count, devc->num_samples);
                                                if (devc->trigger_channel_mask)
                                                {
                                                        if (num_pre_trigger_samples != 0)
                                                        {
                                                                packet.type = SR_DF_LOGIC;
                                                                packet.payload = &logic;
                                                                logic.length = num_pre_trigger_samples * devc->logic_unitsize;
                                                                logic.unitsize = devc->logic_unitsize;
                                                                logic.data = devc->raw_sample_buf +
                                                                                        (devc->limit_samples - devc->num_samples) * sample_size;
                                                                sr_session_send(sdi, &packet);
                                                        }
                                                        std_session_send_df_trigger(sdi);
                                                }
                                                
                                                /* Send remaining logic data */
                                                packet.type = SR_DF_LOGIC;
                                                packet.payload = &logic;
                                                logic.length = (devc->num_samples - num_pre_trigger_samples) * devc->logic_unitsize;
                                                logic.unitsize = devc->logic_unitsize;
                                                logic.data = devc->raw_sample_buf +
                                                                        (num_pre_trigger_samples + devc->limit_samples - devc->num_samples) * sample_size;
                                                sr_session_send(sdi, &packet);
                                                
                                                /* Send analog data if present */
                                                if (devc->num_analog_channels > 0)
                                                {
                                                        sr_analog_init(&analog, &encoding, &meaning, &spec, 0);
                                                        encoding.unitsize = sizeof(float);
                                                        encoding.is_signed = TRUE;
                                                        encoding.is_float = TRUE;
                                                        encoding.is_bigendian = FALSE;
                                                        encoding.digits = 3;
                                                        meaning.mq = SR_MQ_VOLTAGE;
                                                        meaning.unit = SR_UNIT_VOLT;
                                                        meaning.mqflags = 0;
                                                        spec.spec_digits = 3;
                                                        
                                                        analog.num_samples = devc->num_samples;
                                                        analog.data = devc->raw_sample_buf + devc->logic_unitsize;
                                                        
                                                        packet.type = SR_DF_ANALOG;
                                                        packet.payload = &analog;
                                                        sr_session_send(sdi, &packet);
                                                }
                                                
                                                g_free(devc->raw_sample_buf);
                                                std_session_send_df_frame_end(sdi);
                                                
                                                /* Reset parameters for next capture */
                                                devc->num_samples = 0;
                                                devc->num_transfers = 0;
                                                serial_flush(serial);
                                        }
                                }
                        }
                        else
                        {
                                /* Logic-only mode (compatible with Bus Pirate) */
                                for (k = 0; k < len; k++)
                                {
                                        sample = read_buffer[k];
                                        devc->num_samples++;
                                        offset = (devc->limit_samples - devc->num_samples) * devc->logic_unitsize;
                                        devc->raw_sample_buf[offset] = sample;
                                        if (devc->num_samples >= devc->limit_samples)
                                        {
                                                int num_pre_trigger_samples = MIN(devc->before_trigger_sample_count, devc->num_samples);
                                                if (devc->trigger_channel_mask)
                                                {
                                                        if (num_pre_trigger_samples != 0)
                                                        {
                                                                packet.type = SR_DF_LOGIC;
                                                                packet.payload = &logic;
                                                                logic.length = num_pre_trigger_samples * devc->logic_unitsize;
                                                                logic.unitsize = devc->logic_unitsize;
                                                                logic.data = devc->raw_sample_buf +
                                                                                        (devc->limit_samples - devc->num_samples) *
                                                                                                devc->logic_unitsize;
                                                                sr_session_send(sdi, &packet);
                                                        }
                                                        std_session_send_df_trigger(sdi);
                                                }
                                                packet.type = SR_DF_LOGIC;
                                                packet.payload = &logic;
                                                logic.length =
                                                        (devc->num_samples - num_pre_trigger_samples) * devc->logic_unitsize;
                                                logic.unitsize = devc->logic_unitsize;
                                                logic.data = devc->raw_sample_buf +
                                                                                (num_pre_trigger_samples + devc->limit_samples -
                                                                                devc->num_samples) *
                                                                                        devc->logic_unitsize;
                                                sr_session_send(sdi, &packet);

                                                g_free(devc->raw_sample_buf);
                                                std_session_send_df_frame_end(sdi);
                                                
                                                /* Reset parameters for next capture */
                                                devc->num_samples = 0;
                                                devc->num_transfers = 0;
                                                serial_flush(serial);
                                        }
                                }
                        }
                } 
        }
        return TRUE;
}
