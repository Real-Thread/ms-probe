/*
 * Copyright (c) 2006-2023 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-09-04     Bernard      add bare-metal scope probe interface
 */

#include "ms_probe.h"

#define MS_PROBE_MAGIC_0               0x53U
#define MS_PROBE_MAGIC_1               0x50U
#define MS_PROBE_PARSER_MAGIC_0        0U
#define MS_PROBE_PARSER_MAGIC_1        1U
#define MS_PROBE_PARSER_HEADER         2U
#define MS_PROBE_PARSER_PAYLOAD        3U
#define MS_PROBE_PARSER_PAYLOAD_CRC    4U
#define MS_PROBE_TRANSFER_NONE         0U
#define MS_PROBE_TRANSFER_DATA         1U
#define MS_PROBE_TRANSFER_CONTEXT_CRC  2U
#define MS_PROBE_TRANSFER_CONTEXT      3U
#define MS_PROBE_DATA_HEADER_SIZE      14U
#define MS_PROBE_HELP_SIZE             16U

static void ms_probe_copy(ms_probe_u8 *destination,
                          const ms_probe_u8 *source,
                          ms_probe_u32 length)
{
    volatile ms_probe_u8 *volatile_destination;
    const volatile ms_probe_u8 *volatile_source;
    ms_probe_u32 index;

    volatile_destination = (volatile ms_probe_u8 *)destination;
    volatile_source = (const volatile ms_probe_u8 *)source;
    for (index = 0U; index < length; index++)
    {
        volatile_destination[index] = volatile_source[index];
    }
}

static void ms_probe_zero(void *destination, ms_probe_u32 length)
{
    ms_probe_u8 *bytes;
    ms_probe_u32 index;

    bytes = (ms_probe_u8 *)destination;
    for (index = 0U; index < length; index++)
    {
        bytes[index] = 0U;
    }
}

static void ms_probe_copy_config(struct ms_probe_config *destination,
                                 const struct ms_probe_config *source)
{
    destination->ops.enter_poll_mode = source->ops.enter_poll_mode;
    destination->ops.poll_read = source->ops.poll_read;
    destination->ops.poll_write = source->ops.poll_write;
    destination->ops.read_memory = source->ops.read_memory;
    destination->ops.now_ms = source->ops.now_ms;
    destination->ops.service_watchdog = source->ops.service_watchdog;
    destination->ops.reset = source->ops.reset;
    destination->user = source->user;
    destination->regions = source->regions;
    destination->region_count = source->region_count;
    destination->build_id = source->build_id;
    destination->build_id_length = source->build_id_length;
    destination->context = source->context;
    destination->context_length = source->context_length;
    destination->context_version = source->context_version;
    destination->max_payload = source->max_payload;
    destination->max_read_length = source->max_read_length;
    destination->frame_timeout_ms = source->frame_timeout_ms;
    destination->device_id = source->device_id;
    destination->capabilities = source->capabilities;
    destination->architecture = source->architecture;
    destination->endianness = source->endianness;
    destination->address_width = source->address_width;
    destination->probe_version_major = source->probe_version_major;
    destination->probe_version_minor = source->probe_version_minor;
}

static ms_probe_u16 ms_probe_get_u16(const ms_probe_u8 *data)
{
    return (ms_probe_u16)((ms_probe_u16)data[0] |
                          ((ms_probe_u16)data[1] << 8));
}

static ms_probe_u32 ms_probe_get_u24(const ms_probe_u8 *data)
{
    return (ms_probe_u32)data[0] |
           ((ms_probe_u32)data[1] << 8) |
           ((ms_probe_u32)data[2] << 16);
}

static ms_probe_u32 ms_probe_get_u32(const ms_probe_u8 *data)
{
    return (ms_probe_u32)data[0] |
           ((ms_probe_u32)data[1] << 8) |
           ((ms_probe_u32)data[2] << 16) |
           ((ms_probe_u32)data[3] << 24);
}

static void ms_probe_put_u16(ms_probe_u8 *data, ms_probe_u16 value)
{
    data[0] = (ms_probe_u8)value;
    data[1] = (ms_probe_u8)(value >> 8);
}

static void ms_probe_put_u24(ms_probe_u8 *data, ms_probe_u32 value)
{
    data[0] = (ms_probe_u8)value;
    data[1] = (ms_probe_u8)(value >> 8);
    data[2] = (ms_probe_u8)(value >> 16);
}

static void ms_probe_put_u32(ms_probe_u8 *data, ms_probe_u32 value)
{
    data[0] = (ms_probe_u8)value;
    data[1] = (ms_probe_u8)(value >> 8);
    data[2] = (ms_probe_u8)(value >> 16);
    data[3] = (ms_probe_u8)(value >> 24);
}

ms_probe_u8 ms_probe_crc8(const ms_probe_u8 *data, ms_probe_u16 length)
{
    ms_probe_u16 index;
    ms_probe_u8 bit;
    ms_probe_u8 crc;

    crc = 0U;
    for (index = 0U; index < length; index++)
    {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x80U) != 0U)
            {
                crc = (ms_probe_u8)((crc << 1) ^ 0x07U);
            }
            else
            {
                crc = (ms_probe_u8)(crc << 1);
            }
        }
    }

    return crc;
}

static ms_probe_u32 ms_probe_crc32_update(ms_probe_u32 crc,
                                           const ms_probe_u8 *data,
                                           ms_probe_u32 length)
{
    ms_probe_u32 index;
    ms_probe_u32 bit;

    for (index = 0U; index < length; index++)
    {
        crc ^= (ms_probe_u32)data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 1U) != 0U)
            {
                crc = (crc >> 1) ^ 0xedb88320U;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

ms_probe_u32 ms_probe_crc32(const ms_probe_u8 *data, ms_probe_u32 length)
{
    return ms_probe_crc32_update(0xffffffffU, data, length) ^ 0xffffffffU;
}

static void ms_probe_parser_reset(struct ms_probe *probe)
{
    probe->header_position = 0U;
    probe->payload_position = 0U;
    probe->payload_length = 0U;
    probe->payload_crc_position = 0U;
    probe->parser_state = MS_PROBE_PARSER_MAGIC_0;
    probe->parser_idle_ms = 0U;
    probe->rx_crc = 0xffffffffU;
}

static void ms_probe_request_prompt(struct ms_probe *probe)
{
    if (probe->prompt_requests < 0xffU)
    {
        probe->prompt_requests = (ms_probe_u8)(probe->prompt_requests + 1U);
    }
}

static int ms_probe_queue_bytes(struct ms_probe *probe,
                                const ms_probe_u8 *data,
                                ms_probe_u16 length)
{
    if ((probe->tx_position < probe->tx_length) ||
        (data == NULL) || (length == 0U) ||
        (length > MS_PROBE_FRAME_MAX_SIZE))
    {
        return -1;
    }

    ms_probe_copy(probe->tx_frame, data, length);
    probe->tx_position = 0U;
    probe->tx_length = length;
    return 0;
}

static int ms_probe_queue_frame(struct ms_probe *probe, ms_probe_u8 command,
                                ms_probe_u8 flags, ms_probe_u8 sequence,
                                ms_probe_u32 session,
                                const ms_probe_u8 *payload,
                                ms_probe_u16 payload_length)
{
    ms_probe_u16 frame_length;
    ms_probe_u32 crc;

    if ((probe->tx_position < probe->tx_length) ||
        (payload_length > probe->config.max_payload) ||
        (payload_length > MS_PROBE_PROTOCOL_MAX_PAYLOAD) ||
        ((payload_length > 0U) && (payload == NULL)))
    {
        return -1;
    }

    probe->tx_frame[0] = MS_PROBE_MAGIC_0;
    probe->tx_frame[1] = MS_PROBE_MAGIC_1;
    probe->tx_frame[2] = MS_PROBE_PROTOCOL_VERSION;
    probe->tx_frame[3] = command;
    probe->tx_frame[4] = (ms_probe_u8)(flags | MS_PROBE_FLAG_RESPONSE);
    probe->tx_frame[5] = sequence;
    ms_probe_put_u16(&probe->tx_frame[6], payload_length);
    ms_probe_put_u24(&probe->tx_frame[8], session);
    probe->tx_frame[11] = ms_probe_crc8(probe->tx_frame, 11U);

    frame_length = MS_PROBE_HEADER_SIZE;
    if (payload_length > 0U)
    {
        ms_probe_copy(&probe->tx_frame[frame_length], payload, payload_length);
        frame_length = (ms_probe_u16)(frame_length + payload_length);
    }
    if ((flags & MS_PROBE_FLAG_PAYLOAD_CRC) != 0U)
    {
        crc = ms_probe_crc32(payload, payload_length);
        ms_probe_put_u32(&probe->tx_frame[frame_length], crc);
        frame_length = (ms_probe_u16)(frame_length + 4U);
    }

    probe->tx_position = 0U;
    probe->tx_length = frame_length;
    return 0;
}

static void ms_probe_queue_error_for(struct ms_probe *probe,
                                     ms_probe_u8 command,
                                     ms_probe_u8 sequence,
                                     ms_probe_u32 session,
                                     ms_probe_u16 error,
                                     const char *detail)
{
    ms_probe_u16 detail_length;
    ms_probe_u16 max_detail;

    detail_length = 0U;
    max_detail = (ms_probe_u16)(probe->config.max_payload - 4U);
    if (max_detail > 255U)
    {
        max_detail = 255U;
    }
    if (detail != NULL)
    {
        while ((detail[detail_length] != '\0') &&
               (detail_length < max_detail))
        {
            probe->payload[4U + detail_length] =
                (ms_probe_u8)detail[detail_length];
            detail_length++;
        }
    }

    probe->payload[0] = command;
    ms_probe_put_u16(&probe->payload[1], error);
    probe->payload[3] = (ms_probe_u8)detail_length;
    (void)ms_probe_queue_frame(probe, MS_PROBE_CMD_ERROR, 0U,
                               sequence, session, probe->payload,
                               (ms_probe_u16)(4U + detail_length));
}

static void ms_probe_queue_error(struct ms_probe *probe, ms_probe_u16 error,
                                 const char *detail)
{
    ms_probe_queue_error_for(probe, probe->request_command,
                             probe->request_sequence,
                             probe->request_session, error, detail);
}

static int ms_probe_region_contains(const struct ms_probe_region *region,
                                    ms_probe_u32 address,
                                    ms_probe_u32 length)
{
    ms_probe_u32 end;
    ms_probe_u32 region_end;

    if ((region == NULL) || (length == 0U) ||
        (region->size == 0U) || (address < region->start))
    {
        return 0;
    }
    end = address + length;
    region_end = region->start + region->size;
    if ((end < address) || (region_end < region->start))
    {
        return 0;
    }

    return (end <= region_end) ? 1 : 0;
}

static int ms_probe_address_allowed(const struct ms_probe *probe,
                                    ms_probe_u32 address,
                                    ms_probe_u32 length)
{
    ms_probe_u8 index;

    for (index = 0U; index < probe->config.region_count; index++)
    {
        if (ms_probe_region_contains(&probe->config.regions[index],
                                     address, length) != 0)
        {
            return 1;
        }
    }

    return 0;
}

static int ms_probe_transfer_busy(const struct ms_probe *probe)
{
    return (probe->transfer_kind != MS_PROBE_TRANSFER_NONE) ? 1 : 0;
}

static void ms_probe_handle_handshake(struct ms_probe *probe)
{
    ms_probe_u8 client_version;
    ms_probe_u32 session;

    if (probe->state == MS_PROBE_STATE_INACTIVE)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_NOT_ACTIVE,
                             "probe not active");
        return;
    }
    if (ms_probe_transfer_busy(probe) != 0)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_BUSY, "probe busy");
        return;
    }
    if ((probe->request_session != 0U) ||
        (probe->request_payload_length != 4U))
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_BAD_FRAME,
                             "invalid handshake");
        return;
    }

    client_version = probe->payload[0];
    if ((client_version >> 4) != MS_PROBE_PROTOCOL_MAJOR)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_UNSUPPORTED_VERSION,
                             "unsupported version");
        return;
    }

    probe->session_counter++;
    session = (probe->fault_epoch * 2654435761U) ^
              probe->config.device_id ^ probe->session_counter;
    session &= 0x00ffffffU;
    if (session == 0U)
    {
        session = 1U;
    }
    probe->session_id = session;

    probe->payload[0] = MS_PROBE_PROTOCOL_VERSION;
    probe->payload[1] = 0U;
    ms_probe_put_u16(&probe->payload[2], probe->config.max_payload);
    ms_probe_put_u32(&probe->payload[4], probe->config.max_read_length);
    (void)ms_probe_queue_frame(probe, MS_PROBE_CMD_HANDSHAKE, 0U,
                               probe->request_sequence, probe->session_id,
                               probe->payload, 8U);
}

static int ms_probe_check_session(struct ms_probe *probe)
{
    if ((probe->session_id == 0U) ||
        (probe->request_session != probe->session_id))
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_INVALID_SESSION,
                             "invalid session");
        return -1;
    }
    return 0;
}

static void ms_probe_handle_target_info(struct ms_probe *probe)
{
    ms_probe_u16 offset;

    if (probe->request_payload_length != 0U)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_BAD_FRAME,
                             "target info payload");
        return;
    }

    offset = 0U;
    probe->payload[offset++] = probe->config.probe_version_major;
    probe->payload[offset++] = probe->config.probe_version_minor;
    probe->payload[offset++] = probe->config.architecture;
    probe->payload[offset++] = probe->config.endianness;
    probe->payload[offset++] = probe->config.address_width;
    probe->payload[offset++] = probe->state;
    ms_probe_put_u16(&probe->payload[offset], probe->config.capabilities);
    offset = (ms_probe_u16)(offset + 2U);
    ms_probe_put_u32(&probe->payload[offset], probe->fault_epoch);
    offset = (ms_probe_u16)(offset + 4U);
    ms_probe_put_u32(&probe->payload[offset], probe->config.device_id);
    offset = (ms_probe_u16)(offset + 4U);
    probe->payload[offset++] = probe->config.build_id_length;
    if (probe->config.build_id_length > 0U)
    {
        ms_probe_copy(&probe->payload[offset], probe->config.build_id,
                      probe->config.build_id_length);
        offset = (ms_probe_u16)(offset + probe->config.build_id_length);
    }
    ms_probe_put_u16(&probe->payload[offset], probe->config.context_version);
    offset = (ms_probe_u16)(offset + 2U);
    ms_probe_put_u32(&probe->payload[offset], probe->config.context_length);
    offset = (ms_probe_u16)(offset + 4U);

    (void)ms_probe_queue_frame(probe, MS_PROBE_CMD_TARGET_INFO, 0U,
                               probe->request_sequence, probe->session_id,
                               probe->payload, offset);
}

static void ms_probe_handle_data_read(struct ms_probe *probe)
{
    ms_probe_u32 address;
    ms_probe_u32 total_length;

    if (probe->request_payload_length != 8U)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_BAD_FRAME,
                             "data read payload");
        return;
    }
    if (ms_probe_transfer_busy(probe) != 0)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_BUSY, "probe busy");
        return;
    }

    address = ms_probe_get_u32(&probe->payload[0]);
    total_length = ms_probe_get_u32(&probe->payload[4]);
    if ((total_length == 0U) ||
        (total_length > probe->config.max_read_length))
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_INVALID_LENGTH,
                             "invalid read length");
        return;
    }
    if (ms_probe_address_allowed(probe, address, total_length) == 0)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_INVALID_ADDRESS,
                             "address not allowed");
        return;
    }
    if (probe->config.ops.read_memory == NULL)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_INVALID_ADDRESS,
                             "memory unavailable");
        return;
    }

    probe->transfer_kind = MS_PROBE_TRANSFER_DATA;
    probe->transfer_sequence = probe->request_sequence;
    probe->transfer_session = probe->session_id;
    probe->transfer_address = address;
    probe->transfer_length = total_length;
    probe->transfer_offset = 0U;
}

static void ms_probe_handle_context_read(struct ms_probe *probe)
{
    if (probe->request_payload_length != 0U)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_BAD_FRAME,
                             "context payload");
        return;
    }
    if (ms_probe_transfer_busy(probe) != 0)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_BUSY, "probe busy");
        return;
    }
    if (((probe->config.capabilities & MS_PROBE_CAP_CONTEXT_VALID) == 0U) ||
        (probe->config.context == NULL) ||
        (probe->config.context_length == 0U))
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_CONTEXT_UNAVAILABLE,
                             "context unavailable");
        return;
    }

    probe->transfer_kind = MS_PROBE_TRANSFER_CONTEXT_CRC;
    probe->transfer_sequence = probe->request_sequence;
    probe->transfer_session = probe->session_id;
    probe->transfer_length = probe->config.context_length;
    probe->transfer_offset = 0U;
    probe->transfer_crc = 0xffffffffU;
}

static void ms_probe_handle_status(struct ms_probe *probe)
{
    if (probe->request_payload_length != 0U)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_BAD_FRAME,
                             "status payload");
        return;
    }
    probe->payload[0] = probe->state;
    probe->payload[1] = 0U;
    probe->payload[2] = 0U;
    probe->payload[3] = 0U;
    ms_probe_put_u32(&probe->payload[4], probe->fault_epoch);
    (void)ms_probe_queue_frame(probe, MS_PROBE_CMD_STATUS, 0U,
                               probe->request_sequence, probe->session_id,
                               probe->payload, 8U);
}

static void ms_probe_handle_dog_feed(struct ms_probe *probe)
{
    if (probe->request_payload_length != 0U)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_BAD_FRAME,
                             "dog feed payload");
        return;
    }
    if (probe->config.ops.service_watchdog == NULL)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_UNSUPPORTED_COMMAND,
                             "watchdog unavailable");
        return;
    }
    probe->config.ops.service_watchdog(probe->config.user);
    (void)ms_probe_queue_frame(probe, MS_PROBE_CMD_DOG_FEED, 0U,
                               probe->request_sequence, probe->session_id,
                               NULL, 0U);
}

static void ms_probe_help_set(ms_probe_u8 *payload, ms_probe_u8 command)
{
    payload[command >> 3] |=
        (ms_probe_u8)(1U << (command & 7U));
}

static void ms_probe_handle_help(struct ms_probe *probe)
{
    if (probe->request_payload_length != 0U)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_BAD_FRAME,
                             "help payload");
        return;
    }

    ms_probe_zero(probe->payload, MS_PROBE_HELP_SIZE);
    ms_probe_help_set(probe->payload, MS_PROBE_CMD_HANDSHAKE);
    ms_probe_help_set(probe->payload, MS_PROBE_CMD_TARGET_INFO);
    ms_probe_help_set(probe->payload, MS_PROBE_CMD_STATUS);
    ms_probe_help_set(probe->payload, MS_PROBE_CMD_HELP);
    if (probe->config.ops.read_memory != NULL)
    {
        ms_probe_help_set(probe->payload, MS_PROBE_CMD_DATA_READ);
    }
    if ((probe->config.capabilities & MS_PROBE_CAP_CONTEXT_VALID) != 0U)
    {
        ms_probe_help_set(probe->payload, MS_PROBE_CMD_CONTEXT_READ);
    }
    if (probe->config.ops.service_watchdog != NULL)
    {
        ms_probe_help_set(probe->payload, MS_PROBE_CMD_DOG_FEED);
    }
    if (probe->config.ops.reset != NULL)
    {
        ms_probe_help_set(probe->payload, MS_PROBE_CMD_RESET);
    }
    (void)ms_probe_queue_frame(probe, MS_PROBE_CMD_HELP, 0U,
                               probe->request_sequence, probe->session_id,
                               probe->payload, MS_PROBE_HELP_SIZE);
}

static void ms_probe_handle_reset(struct ms_probe *probe)
{
    if (probe->request_payload_length != 0U)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_BAD_FRAME,
                             "reset payload");
        return;
    }
    if (probe->config.ops.reset == NULL)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_UNSUPPORTED_COMMAND,
                             "reset unavailable");
        return;
    }

    probe->transfer_kind = MS_PROBE_TRANSFER_NONE;
    probe->state = MS_PROBE_STATE_RESETTING;
    if (ms_probe_queue_frame(probe, MS_PROBE_CMD_RESET, 0U,
                             probe->request_sequence, probe->session_id,
                             NULL, 0U) == 0)
    {
        probe->reset_pending = 1U;
    }
}

static void ms_probe_dispatch(struct ms_probe *probe)
{
    if (probe->request_error != 0U)
    {
        ms_probe_queue_error(probe, probe->request_error, "invalid frame");
        return;
    }
    if ((probe->request_flags & 0xfaU) != 0U)
    {
        ms_probe_queue_error(probe, MS_PROBE_ERROR_BAD_FRAME,
                             "invalid request flags");
        return;
    }
    if (probe->request_command == MS_PROBE_CMD_HANDSHAKE)
    {
        ms_probe_handle_handshake(probe);
        return;
    }
    if (ms_probe_check_session(probe) != 0)
    {
        return;
    }

    switch (probe->request_command)
    {
    case MS_PROBE_CMD_TARGET_INFO:
        ms_probe_handle_target_info(probe);
        break;
    case MS_PROBE_CMD_DATA_READ:
        ms_probe_handle_data_read(probe);
        break;
    case MS_PROBE_CMD_CONTEXT_READ:
        ms_probe_handle_context_read(probe);
        break;
    case MS_PROBE_CMD_STATUS:
        ms_probe_handle_status(probe);
        break;
    case MS_PROBE_CMD_DOG_FEED:
        ms_probe_handle_dog_feed(probe);
        break;
    case MS_PROBE_CMD_RESET:
        ms_probe_handle_reset(probe);
        break;
    case MS_PROBE_CMD_CANCEL:
        ms_probe_queue_error(probe, MS_PROBE_ERROR_UNSUPPORTED_COMMAND,
                             "cancel unsupported");
        break;
    case MS_PROBE_CMD_HELP:
        ms_probe_handle_help(probe);
        break;
    default:
        ms_probe_queue_error(probe, MS_PROBE_ERROR_UNSUPPORTED_COMMAND,
                             "unsupported command");
        break;
    }
}

static void ms_probe_request_complete(struct ms_probe *probe,
                                      ms_probe_u16 error)
{
    probe->request_payload_length = probe->payload_length;
    probe->request_error = error;
    probe->request_ready = 1U;
    ms_probe_parser_reset(probe);
}

static void ms_probe_header_complete(struct ms_probe *probe)
{
    if (ms_probe_crc8(probe->header, 11U) != probe->header[11])
    {
        ms_probe_parser_reset(probe);
        return;
    }

    probe->request_command = probe->header[3];
    probe->request_flags = probe->header[4];
    probe->request_sequence = probe->header[5];
    probe->payload_length = ms_probe_get_u16(&probe->header[6]);
    probe->request_session = ms_probe_get_u24(&probe->header[8]);

    if ((probe->header[2] >> 4) != MS_PROBE_PROTOCOL_MAJOR)
    {
        ms_probe_request_complete(probe,
                                  MS_PROBE_ERROR_UNSUPPORTED_VERSION);
        return;
    }
    if ((probe->payload_length > probe->config.max_payload) ||
        (probe->payload_length > MS_PROBE_PROTOCOL_MAX_PAYLOAD))
    {
        ms_probe_request_complete(probe, MS_PROBE_ERROR_INVALID_LENGTH);
        return;
    }

    probe->payload_position = 0U;
    probe->payload_crc_position = 0U;
    probe->rx_crc = 0xffffffffU;
    if (probe->payload_length > 0U)
    {
        probe->parser_state = MS_PROBE_PARSER_PAYLOAD;
    }
    else if ((probe->request_flags & MS_PROBE_FLAG_PAYLOAD_CRC) != 0U)
    {
        probe->parser_state = MS_PROBE_PARSER_PAYLOAD_CRC;
    }
    else
    {
        ms_probe_request_complete(probe, 0U);
    }
}

static void ms_probe_payload_complete(struct ms_probe *probe)
{
    if ((probe->request_flags & MS_PROBE_FLAG_PAYLOAD_CRC) != 0U)
    {
        probe->payload_crc_position = 0U;
        probe->parser_state = MS_PROBE_PARSER_PAYLOAD_CRC;
    }
    else
    {
        ms_probe_request_complete(probe, 0U);
    }
}

static void ms_probe_payload_crc_complete(struct ms_probe *probe)
{
    ms_probe_u32 expected;
    ms_probe_u32 actual;

    expected = ms_probe_get_u32(probe->payload_crc);
    actual = probe->rx_crc ^ 0xffffffffU;
    if (expected != actual)
    {
        ms_probe_request_complete(probe, MS_PROBE_ERROR_BAD_CRC);
    }
    else
    {
        ms_probe_request_complete(probe, 0U);
    }
}

static ms_probe_u16 ms_probe_transfer_chunk(const struct ms_probe *probe)
{
    ms_probe_u16 limit;

    limit = (ms_probe_u16)(probe->config.max_payload -
                           MS_PROBE_DATA_HEADER_SIZE);
    if (limit > MS_PROBE_MEMORY_BUDGET)
    {
        limit = MS_PROBE_MEMORY_BUDGET;
    }
    return limit;
}

static void ms_probe_step_data(struct ms_probe *probe)
{
    ms_probe_u32 remaining;
    ms_probe_u16 chunk_length;
    ms_probe_u8 flags;

    remaining = probe->transfer_length - probe->transfer_offset;
    chunk_length = ms_probe_transfer_chunk(probe);
    if (remaining < (ms_probe_u32)chunk_length)
    {
        chunk_length = (ms_probe_u16)remaining;
    }

    ms_probe_put_u32(&probe->payload[0], probe->transfer_address);
    ms_probe_put_u32(&probe->payload[4], probe->transfer_length);
    ms_probe_put_u32(&probe->payload[8], probe->transfer_offset);
    ms_probe_put_u16(&probe->payload[12], chunk_length);
    if (probe->config.ops.read_memory(probe->config.user,
                                      probe->transfer_address +
                                      probe->transfer_offset,
                                      &probe->payload[14],
                                      chunk_length) != 0)
    {
        ms_probe_queue_error_for(probe, MS_PROBE_CMD_DATA_READ,
                                 probe->transfer_sequence,
                                 probe->transfer_session,
                                 MS_PROBE_ERROR_INVALID_ADDRESS,
                                 "memory read failed");
        probe->transfer_kind = MS_PROBE_TRANSFER_NONE;
        return;
    }

    flags = MS_PROBE_FLAG_PAYLOAD_CRC;
    if ((probe->transfer_offset + (ms_probe_u32)chunk_length) <
        probe->transfer_length)
    {
        flags = (ms_probe_u8)(flags | MS_PROBE_FLAG_MORE);
    }
    if (ms_probe_queue_frame(probe, MS_PROBE_CMD_DATA_READ, flags,
                             probe->transfer_sequence,
                             probe->transfer_session, probe->payload,
                             (ms_probe_u16)(MS_PROBE_DATA_HEADER_SIZE +
                                            chunk_length)) == 0)
    {
        probe->transfer_offset += chunk_length;
        if (probe->transfer_offset == probe->transfer_length)
        {
            probe->transfer_kind = MS_PROBE_TRANSFER_NONE;
        }
    }
}

static void ms_probe_step_context_crc(struct ms_probe *probe)
{
    ms_probe_u32 remaining;
    ms_probe_u32 length;

    remaining = probe->transfer_length - probe->transfer_offset;
    length = (remaining > MS_PROBE_MEMORY_BUDGET) ?
             MS_PROBE_MEMORY_BUDGET : remaining;
    probe->transfer_crc = ms_probe_crc32_update(
        probe->transfer_crc,
        &probe->config.context[probe->transfer_offset], length);
    probe->transfer_offset += length;
    if (probe->transfer_offset == probe->transfer_length)
    {
        probe->transfer_crc ^= 0xffffffffU;
        probe->transfer_offset = 0U;
        probe->transfer_kind = MS_PROBE_TRANSFER_CONTEXT;
    }
}

static void ms_probe_step_context(struct ms_probe *probe)
{
    ms_probe_u32 remaining;
    ms_probe_u16 chunk_length;
    ms_probe_u8 flags;

    remaining = probe->transfer_length - probe->transfer_offset;
    chunk_length = ms_probe_transfer_chunk(probe);
    if (remaining < (ms_probe_u32)chunk_length)
    {
        chunk_length = (ms_probe_u16)remaining;
    }

    ms_probe_put_u32(&probe->payload[0], probe->transfer_length);
    ms_probe_put_u32(&probe->payload[4], probe->transfer_offset);
    ms_probe_put_u16(&probe->payload[8], chunk_length);
    ms_probe_put_u32(&probe->payload[10], probe->transfer_crc);
    ms_probe_copy(&probe->payload[14],
                  &probe->config.context[probe->transfer_offset],
                  chunk_length);
    flags = MS_PROBE_FLAG_PAYLOAD_CRC;
    if ((probe->transfer_offset + (ms_probe_u32)chunk_length) <
        probe->transfer_length)
    {
        flags = (ms_probe_u8)(flags | MS_PROBE_FLAG_MORE);
    }
    if (ms_probe_queue_frame(probe, MS_PROBE_CMD_CONTEXT_READ, flags,
                             probe->transfer_sequence,
                             probe->transfer_session, probe->payload,
                             (ms_probe_u16)(MS_PROBE_DATA_HEADER_SIZE +
                                            chunk_length)) == 0)
    {
        probe->transfer_offset += chunk_length;
        if (probe->transfer_offset == probe->transfer_length)
        {
            probe->transfer_kind = MS_PROBE_TRANSFER_NONE;
        }
    }
}

int ms_probe_init(struct ms_probe *probe, const struct ms_probe_config *config)
{
    if ((probe == NULL) || (config == NULL) ||
        (config->ops.poll_read == NULL) ||
        (config->ops.poll_write == NULL) ||
        (config->max_payload < 64U) ||
        (config->max_payload > MS_PROBE_PROTOCOL_MAX_PAYLOAD) ||
        (config->max_read_length == 0U) ||
        (config->build_id_length > MS_PROBE_BUILD_ID_MAX) ||
        ((config->build_id_length > 0U) && (config->build_id == NULL)) ||
        ((config->region_count > 0U) && (config->regions == NULL)))
    {
        return -1;
    }

    ms_probe_zero(probe, (ms_probe_u32)sizeof(*probe));
    ms_probe_copy_config(&probe->config, config);
    probe->config.capabilities &=
        (ms_probe_u16)(~MS_PROBE_CAP_CANCEL_SUPPORTED);
    if (probe->config.ops.service_watchdog == NULL)
    {
        probe->config.capabilities &=
            (ms_probe_u16)(~MS_PROBE_CAP_WATCHDOG_REQUIRED);
    }
    if ((probe->config.context == NULL) ||
        (probe->config.context_length == 0U))
    {
        probe->config.capabilities &=
            (ms_probe_u16)(~(MS_PROBE_CAP_CONTEXT_VALID |
                             MS_PROBE_CAP_FPU_CONTEXT));
    }
    if ((probe->config.ops.read_memory == NULL) ||
        (probe->config.region_count == 0U))
    {
        probe->config.capabilities &=
            (ms_probe_u16)(~MS_PROBE_CAP_MEMORY_STABLE);
    }
    probe->state = MS_PROBE_STATE_INACTIVE;
    ms_probe_parser_reset(probe);
    return 0;
}

void ms_probe_activate(struct ms_probe *probe, ms_probe_u32 fault_epoch)
{
    if (probe == NULL)
    {
        return;
    }
    probe->state = MS_PROBE_STATE_FAULT;
    probe->fault_epoch = fault_epoch;
    probe->session_id = 0U;
    probe->request_ready = 0U;
    probe->request_error = 0U;
    probe->prompt_requests = 1U;
    probe->last_input_was_cr = 0U;
    probe->tx_length = 0U;
    probe->tx_position = 0U;
    probe->input_pending_length = 0U;
    probe->transfer_kind = MS_PROBE_TRANSFER_NONE;
    probe->reset_pending = 0U;
    probe->time_initialized = 0U;
    ms_probe_parser_reset(probe);
}

void ms_probe_set_context(struct ms_probe *probe, const ms_probe_u8 *context,
                          ms_probe_u32 length, ms_probe_u16 version,
                          int valid)
{
    if (probe == NULL)
    {
        return;
    }
    probe->config.context = context;
    probe->config.context_length = length;
    probe->config.context_version = version;
    if ((valid != 0) && (context != NULL) && (length > 0U))
    {
        probe->config.capabilities |= MS_PROBE_CAP_CONTEXT_VALID;
    }
    else
    {
        probe->config.capabilities &=
            (ms_probe_u16)(~(MS_PROBE_CAP_CONTEXT_VALID |
                             MS_PROBE_CAP_FPU_CONTEXT));
    }
}

void ms_probe_input(struct ms_probe *probe, const ms_probe_u8 *data,
                    ms_probe_u16 length)
{
    ms_probe_u16 index;
    ms_probe_u8 byte;

    if ((probe == NULL) || ((data == NULL) && (length > 0U)))
    {
        return;
    }
    if (probe->request_ready != 0U)
    {
        return;
    }
    if (length > 0U)
    {
        probe->parser_idle_ms = 0U;
    }

    for (index = 0U; index < length; index++)
    {
        byte = data[index];
        switch (probe->parser_state)
        {
        case MS_PROBE_PARSER_MAGIC_0:
            if (byte == (ms_probe_u8)'\r')
            {
                ms_probe_request_prompt(probe);
                probe->last_input_was_cr = 1U;
            }
            else if (byte == (ms_probe_u8)'\n')
            {
                if (probe->last_input_was_cr == 0U)
                {
                    ms_probe_request_prompt(probe);
                }
                probe->last_input_was_cr = 0U;
            }
            else if (byte == MS_PROBE_MAGIC_0)
            {
                probe->last_input_was_cr = 0U;
                probe->header[0] = byte;
                probe->header_position = 1U;
                probe->parser_state = MS_PROBE_PARSER_MAGIC_1;
            }
            else
            {
                probe->last_input_was_cr = 0U;
            }
            break;
        case MS_PROBE_PARSER_MAGIC_1:
            if (byte == MS_PROBE_MAGIC_1)
            {
                probe->header[1] = byte;
                probe->header_position = 2U;
                probe->parser_state = MS_PROBE_PARSER_HEADER;
            }
            else if (byte != MS_PROBE_MAGIC_0)
            {
                ms_probe_parser_reset(probe);
            }
            break;
        case MS_PROBE_PARSER_HEADER:
            probe->header[probe->header_position++] = byte;
            if (probe->header_position == MS_PROBE_HEADER_SIZE)
            {
                ms_probe_header_complete(probe);
            }
            break;
        case MS_PROBE_PARSER_PAYLOAD:
            probe->payload[probe->payload_position++] = byte;
            probe->rx_crc = ms_probe_crc32_update(probe->rx_crc, &byte, 1U);
            if (probe->payload_position == probe->payload_length)
            {
                ms_probe_payload_complete(probe);
            }
            break;
        case MS_PROBE_PARSER_PAYLOAD_CRC:
            probe->payload_crc[probe->payload_crc_position++] = byte;
            if (probe->payload_crc_position == 4U)
            {
                ms_probe_payload_crc_complete(probe);
            }
            break;
        default:
            ms_probe_parser_reset(probe);
            break;
        }
        if (probe->request_ready != 0U)
        {
            if ((index + 1U) < length)
            {
                probe->input_pending_length = (ms_probe_u8)(length - index - 1U);
                ms_probe_copy(probe->input_pending,
                              &data[index + 1U],
                              (ms_probe_u32)probe->input_pending_length);
            }
            break;
        }
    }
}

void ms_probe_poll(struct ms_probe *probe, ms_probe_u32 elapsed_ms)
{
    ms_probe_u32 remaining;

    if ((probe == NULL) || (elapsed_ms == 0U) ||
        (probe->parser_state == MS_PROBE_PARSER_MAGIC_0) ||
        (probe->config.frame_timeout_ms == 0U))
    {
        return;
    }
    if (probe->parser_idle_ms >= probe->config.frame_timeout_ms)
    {
        ms_probe_parser_reset(probe);
        return;
    }
    remaining = probe->config.frame_timeout_ms - probe->parser_idle_ms;
    if (elapsed_ms >= remaining)
    {
        ms_probe_parser_reset(probe);
    }
    else
    {
        probe->parser_idle_ms += elapsed_ms;
    }
}

static void ms_probe_protocol_step(struct ms_probe *probe)
{
    ms_probe_u16 remaining;
    ms_probe_u16 length;
    int written;
    static const ms_probe_u8 prompt[2] = {'-', '>'};

    if (probe == NULL)
    {
        return;
    }

    if (probe->tx_position < probe->tx_length)
    {
        remaining = (ms_probe_u16)(probe->tx_length - probe->tx_position);
        length = (remaining > MS_PROBE_IO_BUDGET) ?
                 MS_PROBE_IO_BUDGET : remaining;
        written = probe->config.ops.poll_write(
            probe->config.user, &probe->tx_frame[probe->tx_position], length);
        if ((written > 0) && (written <= (int)length))
        {
            probe->tx_position =
                (ms_probe_u16)(probe->tx_position + (ms_probe_u16)written);
            if (probe->tx_position == probe->tx_length)
            {
                probe->tx_position = 0U;
                probe->tx_length = 0U;
            }
        }
        return;
    }

    if (probe->reset_pending != 0U)
    {
        probe->reset_pending = 0U;
        probe->config.ops.reset(probe->config.user);
        probe->state = MS_PROBE_STATE_FAULT;
        return;
    }
    if (probe->prompt_requests > 0U)
    {
        if (ms_probe_queue_bytes(probe, prompt, 2U) == 0)
        {
            probe->prompt_requests--;
        }
        return;
    }
    if (probe->request_ready != 0U)
    {
        ms_probe_dispatch(probe);
        probe->request_ready = 0U;
        probe->request_error = 0U;
        return;
    }

    switch (probe->transfer_kind)
    {
    case MS_PROBE_TRANSFER_DATA:
        ms_probe_step_data(probe);
        break;
    case MS_PROBE_TRANSFER_CONTEXT_CRC:
        ms_probe_step_context_crc(probe);
        break;
    case MS_PROBE_TRANSFER_CONTEXT:
        ms_probe_step_context(probe);
        break;
    default:
        break;
    }
}

void ms_probe_step(struct ms_probe *probe)
{
    ms_probe_u8 input[MS_PROBE_IO_BUDGET];
    ms_probe_u8 pending[MS_PROBE_IO_BUDGET];
    ms_probe_u32 current_ms;
    int length;

    if (probe == NULL)
    {
        return;
    }
    if (probe->config.ops.service_watchdog != NULL)
    {
        probe->config.ops.service_watchdog(probe->config.user);
    }
    if ((probe->request_ready == 0U) &&
        (probe->input_pending_length > 0U))
    {
        length = (int)probe->input_pending_length;
        ms_probe_copy(pending, probe->input_pending, (ms_probe_u32)length);
        probe->input_pending_length = 0U;
        ms_probe_input(probe, pending, (ms_probe_u16)length);
    }
    if (probe->request_ready == 0U)
    {
        length = probe->config.ops.poll_read(probe->config.user, input,
                                             MS_PROBE_IO_BUDGET);
        if (length > (int)MS_PROBE_IO_BUDGET)
        {
            length = (int)MS_PROBE_IO_BUDGET;
        }
        if (length > 0)
        {
            ms_probe_input(probe, input, (ms_probe_u16)length);
        }
    }
    if (probe->config.ops.now_ms != NULL)
    {
        current_ms = probe->config.ops.now_ms(probe->config.user);
        if (probe->time_initialized != 0U)
        {
            ms_probe_poll(probe, current_ms - probe->last_poll_ms);
        }
        else
        {
            probe->time_initialized = 1U;
        }
        probe->last_poll_ms = current_ms;
    }
    ms_probe_protocol_step(probe);
}

void ms_probe_run_fault(struct ms_probe *probe)
{
    if (probe == NULL)
    {
        for (;;)
        {
        }
    }
    if (probe->config.ops.enter_poll_mode != NULL)
    {
        (void)probe->config.ops.enter_poll_mode(probe->config.user);
    }

    for (;;)
    {
        ms_probe_step(probe);
    }
}
