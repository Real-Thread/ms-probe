/*
 * Copyright (c) 2006-2023 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "ms_probe.h"

#include <stdio.h>
#include <string.h>

#define TEST_OUTPUT_SIZE 8192U
#define TEST_INPUT_SIZE  256U
#define TEST_MEMORY_SIZE 512U
#define TEST_MEMORY_BASE 0x20000000U

struct test_port
{
    ms_probe_u8 output[TEST_OUTPUT_SIZE];
    ms_probe_u8 input[TEST_INPUT_SIZE];
    ms_probe_u8 memory[TEST_MEMORY_SIZE];
    ms_probe_u16 output_length;
    ms_probe_u16 input_length;
    ms_probe_u16 input_position;
    ms_probe_u16 write_limit;
    ms_probe_u16 max_write_request;
    ms_probe_u16 max_read_memory;
    ms_probe_u16 last_read_capacity;
    ms_probe_u32 now_ms;
    int enter_called;
    int reset_called;
    int watchdog_calls;
};

static int test_enter_poll_mode(void *user)
{
    ((struct test_port *)user)->enter_called++;
    return 0;
}

static int test_poll_read(void *user, ms_probe_u8 *data,
                          ms_probe_u16 capacity)
{
    struct test_port *port;
    ms_probe_u16 available;
    ms_probe_u16 length;

    port = (struct test_port *)user;
    port->last_read_capacity = capacity;
    available = (ms_probe_u16)(port->input_length - port->input_position);
    length = (available < capacity) ? available : capacity;
    if (length > 0U)
    {
        (void)memcpy(data, &port->input[port->input_position], length);
        port->input_position = (ms_probe_u16)(port->input_position + length);
    }
    return (int)length;
}

static int test_poll_write(void *user, const ms_probe_u8 *data,
                           ms_probe_u16 length)
{
    struct test_port *port;
    ms_probe_u16 written;

    port = (struct test_port *)user;
    if (length > port->max_write_request)
    {
        port->max_write_request = length;
    }
    written = length;
    if ((port->write_limit > 0U) && (written > port->write_limit))
    {
        written = port->write_limit;
    }
    if (((ms_probe_u32)port->output_length + written) > TEST_OUTPUT_SIZE)
    {
        return -1;
    }
    (void)memcpy(&port->output[port->output_length], data, written);
    port->output_length = (ms_probe_u16)(port->output_length + written);
    return (int)written;
}

static int test_read_memory(void *user, ms_probe_u32 address,
                            ms_probe_u8 *data, ms_probe_u16 length)
{
    struct test_port *port;
    ms_probe_u32 offset;

    port = (struct test_port *)user;
    if (length > port->max_read_memory)
    {
        port->max_read_memory = length;
    }
    if ((address < TEST_MEMORY_BASE) ||
        ((address + length) < address))
    {
        return -1;
    }
    offset = address - TEST_MEMORY_BASE;
    if ((offset + length) > TEST_MEMORY_SIZE)
    {
        return -1;
    }
    (void)memcpy(data, &port->memory[offset], length);
    return 0;
}

static ms_probe_u32 test_now_ms(void *user)
{
    return ((struct test_port *)user)->now_ms;
}

static void test_reset(void *user)
{
    ((struct test_port *)user)->reset_called++;
}

static void test_watchdog(void *user)
{
    ((struct test_port *)user)->watchdog_calls++;
}

static ms_probe_u8 test_crc8(const ms_probe_u8 *data, ms_probe_u16 length)
{
    ms_probe_u8 crc;
    ms_probe_u8 bit;
    ms_probe_u16 index;

    crc = 0U;
    for (index = 0U; index < length; index++)
    {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 0x80U) != 0U) ?
                  (ms_probe_u8)((crc << 1) ^ 0x07U) :
                  (ms_probe_u8)(crc << 1);
        }
    }
    return crc;
}

static void test_put_u16(ms_probe_u8 *data, ms_probe_u16 value)
{
    data[0] = (ms_probe_u8)value;
    data[1] = (ms_probe_u8)(value >> 8);
}

static void test_put_u24(ms_probe_u8 *data, ms_probe_u32 value)
{
    data[0] = (ms_probe_u8)value;
    data[1] = (ms_probe_u8)(value >> 8);
    data[2] = (ms_probe_u8)(value >> 16);
}

static void test_put_u32(ms_probe_u8 *data, ms_probe_u32 value)
{
    data[0] = (ms_probe_u8)value;
    data[1] = (ms_probe_u8)(value >> 8);
    data[2] = (ms_probe_u8)(value >> 16);
    data[3] = (ms_probe_u8)(value >> 24);
}

static ms_probe_u16 test_get_u16(const ms_probe_u8 *data)
{
    return (ms_probe_u16)((ms_probe_u16)data[0] |
                          ((ms_probe_u16)data[1] << 8));
}

static ms_probe_u16 test_make_request(ms_probe_u8 *frame,
                                      ms_probe_u8 command,
                                      ms_probe_u8 sequence,
                                      ms_probe_u32 session,
                                      const ms_probe_u8 *payload,
                                      ms_probe_u16 payload_length)
{
    frame[0] = 0x53U;
    frame[1] = 0x50U;
    frame[2] = 0x20U;
    frame[3] = command;
    frame[4] = 0U;
    frame[5] = sequence;
    test_put_u16(&frame[6], payload_length);
    test_put_u24(&frame[8], session);
    frame[11] = test_crc8(frame, 11U);
    if (payload_length > 0U)
    {
        (void)memcpy(&frame[12], payload, payload_length);
    }
    return (ms_probe_u16)(12U + payload_length);
}

static ms_probe_u16 test_frame_size(const ms_probe_u8 *frame)
{
    ms_probe_u16 size;

    size = (ms_probe_u16)(12U + test_get_u16(&frame[6]));
    if ((frame[4] & MS_PROBE_FLAG_PAYLOAD_CRC) != 0U)
    {
        size = (ms_probe_u16)(size + 4U);
    }
    return size;
}

static ms_probe_u32 test_session(const ms_probe_u8 *frame)
{
    return (ms_probe_u32)frame[8] |
           ((ms_probe_u32)frame[9] << 8) |
           ((ms_probe_u32)frame[10] << 16);
}

static int test_expect(int condition, const char *message)
{
    if (condition == 0)
    {
        (void)fprintf(stderr, "FAILED: %s\n", message);
        return -1;
    }
    return 0;
}

static int test_probe_idle(const struct ms_probe *probe)
{
    return ((probe->tx_length == 0U) &&
            (probe->request_ready == 0U) &&
            (probe->prompt_requests == 0U) &&
            (probe->transfer_kind == 0U) &&
            (probe->reset_pending == 0U)) ? 1 : 0;
}

static int test_drain(struct ms_probe *probe, ms_probe_u16 limit)
{
    ms_probe_u16 step;

    for (step = 0U; step < limit; step++)
    {
        if (test_probe_idle(probe) != 0)
        {
            return 0;
        }
        ms_probe_step(probe);
    }
    return -1;
}

static void test_clear_output(struct test_port *port)
{
    port->output_length = 0U;
    port->max_write_request = 0U;
}

static int test_protocol_flow(void)
{
    struct ms_probe probe;
    struct ms_probe_config config;
    struct ms_probe_region region;
    struct test_port port;
    ms_probe_u8 context[100];
    ms_probe_u8 request[64];
    ms_probe_u8 status_request[16];
    ms_probe_u8 pasted_requests[32];
    ms_probe_u8 payload[8];
    ms_probe_u8 line_breaks[2];
    ms_probe_u32 session;
    ms_probe_u16 request_length;
    ms_probe_u16 status_length;
    ms_probe_u16 frame_length;
    ms_probe_u16 output_offset;
    ms_probe_u16 copied;
    ms_probe_u16 chunk;
    ms_probe_u16 index;
    int watchdog_before;

    (void)memset(&port, 0, sizeof(port));
    port.write_limit = 3U;
    for (index = 0U; index < TEST_MEMORY_SIZE; index++)
    {
        port.memory[index] = (ms_probe_u8)(index & 0xffU);
    }
    for (index = 0U; index < sizeof(context); index++)
    {
        context[index] = (ms_probe_u8)(0xa0U + (index & 0x0fU));
    }

    (void)memset(&config, 0, sizeof(config));
    region.start = TEST_MEMORY_BASE;
    region.size = TEST_MEMORY_SIZE;
    config.ops.enter_poll_mode = test_enter_poll_mode;
    config.ops.poll_read = test_poll_read;
    config.ops.poll_write = test_poll_write;
    config.ops.read_memory = test_read_memory;
    config.ops.now_ms = test_now_ms;
    config.ops.service_watchdog = test_watchdog;
    config.ops.reset = test_reset;
    config.user = &port;
    config.regions = &region;
    config.region_count = 1U;
    config.context = context;
    config.context_length = sizeof(context);
    config.context_version = 1U;
    config.max_payload = 64U;
    config.max_read_length = TEST_MEMORY_SIZE;
    config.frame_timeout_ms = 10U;
    config.device_id = 0xa536U;
    config.capabilities = MS_PROBE_CAP_MEMORY_STABLE |
                          MS_PROBE_CAP_CONTEXT_VALID |
                          MS_PROBE_CAP_WATCHDOG_REQUIRED |
                          MS_PROBE_CAP_CANCEL_SUPPORTED;
    config.architecture = MS_PROBE_ARCH_ARMV8_R_AARCH32;
    config.endianness = MS_PROBE_ENDIAN_LITTLE;
    config.address_width = 4U;
    config.probe_version_major = 1U;

    if (test_expect(ms_probe_init(&probe, &config) == 0,
                    "probe init") != 0 ||
        test_expect((probe.config.capabilities &
                     MS_PROBE_CAP_CANCEL_SUPPORTED) == 0U,
                    "cancel capability sanitized") != 0)
    {
        return -1;
    }
    ms_probe_activate(&probe, 7U);
    if (test_expect(test_drain(&probe, 16U) == 0,
                    "initial prompt drain") != 0 ||
        test_expect(port.output_length == 2U,
                    "initial prompt emitted") != 0 ||
        test_expect(port.output[0] == (ms_probe_u8)'-' &&
                    port.output[1] == (ms_probe_u8)'>',
                    "initial prompt bytes") != 0)
    {
        return -1;
    }

    test_clear_output(&port);
    line_breaks[0] = (ms_probe_u8)'\r';
    line_breaks[1] = (ms_probe_u8)'\n';
    ms_probe_input(&probe, line_breaks, 2U);
    if (test_expect(test_drain(&probe, 16U) == 0,
                    "CRLF prompt drain") != 0 ||
        test_expect(port.output_length == 2U,
                    "CRLF requests one prompt") != 0)
    {
        return -1;
    }

    test_clear_output(&port);
    payload[0] = 0x20U;
    payload[1] = (ms_probe_u8)'\r';
    payload[2] = (ms_probe_u8)'\n';
    payload[3] = 0U;
    request_length = test_make_request(request, MS_PROBE_CMD_HANDSHAKE,
                                       1U, 0U, payload, 4U);
    ms_probe_input(&probe, request, request_length);
    if (test_expect(port.output_length == 0U,
                    "input does not write synchronously") != 0 ||
        test_expect(test_drain(&probe, 64U) == 0,
                    "handshake drain") != 0 ||
        test_expect(port.output_length == 20U,
                    "handshake response length") != 0 ||
        test_expect(port.output[3] == MS_PROBE_CMD_HANDSHAKE,
                    "handshake response command") != 0 ||
        test_expect(port.max_write_request <= MS_PROBE_IO_BUDGET,
                    "TX request uses fixed budget") != 0)
    {
        return -1;
    }
    session = test_session(port.output);
    if (test_expect(session != 0U, "session allocated") != 0)
    {
        return -1;
    }

    test_clear_output(&port);
    status_length = test_make_request(status_request, MS_PROBE_CMD_STATUS,
                                      8U, session, NULL, 0U);
    request_length = test_make_request(request, MS_PROBE_CMD_STATUS,
                                       9U, session, NULL, 0U);
    (void)memcpy(pasted_requests, status_request, status_length);
    (void)memcpy(&pasted_requests[status_length], request, request_length);
    ms_probe_input(&probe, pasted_requests,
                   (ms_probe_u16)(status_length + request_length));
    if (test_expect(test_drain(&probe, 128U) == 0,
                    "pasted first status drain") != 0 ||
        test_expect(port.output_length == 40U,
                    "pasted two status responses") != 0 ||
        test_expect(port.output[5] == 8U && port.output[25] == 9U,
                    "pasted sequences preserved") != 0)
    {
        return -1;
    }

    test_clear_output(&port);
    request_length = test_make_request(request, MS_PROBE_CMD_STATUS,
                                       2U, session, NULL, 0U);
    ms_probe_input(&probe, request, 5U);
    port.now_ms += 10U;
    ms_probe_step(&probe);
    ms_probe_input(&probe, request, request_length);
    if (test_expect(test_drain(&probe, 64U) == 0,
                    "timeout status drain") != 0 ||
        test_expect(port.output_length == 20U,
                    "header timeout recovery") != 0 ||
        test_expect(port.output[3] == MS_PROBE_CMD_STATUS,
                    "status after timeout") != 0)
    {
        return -1;
    }

    test_clear_output(&port);
    test_put_u32(&payload[0], TEST_MEMORY_BASE + 4U);
    test_put_u32(&payload[4], 120U);
    request_length = test_make_request(request, MS_PROBE_CMD_DATA_READ,
                                       3U, session, payload, 8U);
    ms_probe_input(&probe, request, request_length);
    ms_probe_step(&probe);
    if (test_expect(port.output_length == 0U,
                    "dispatch only starts data transfer") != 0 ||
        test_expect(probe.transfer_kind != 0U,
                    "data transfer active") != 0)
    {
        return -1;
    }
    ms_probe_step(&probe);
    if (test_expect(port.output_length == 0U,
                    "chunk generation does not write") != 0 ||
        test_expect(probe.tx_length > 0U,
                    "first data frame queued") != 0)
    {
        return -1;
    }

    status_length = test_make_request(status_request, MS_PROBE_CMD_STATUS,
                                      4U, session, NULL, 0U);
    ms_probe_input(&probe, status_request, status_length);
    if (test_expect(test_drain(&probe, 512U) == 0,
                    "interleaved transfer drain") != 0)
    {
        return -1;
    }

    output_offset = 0U;
    copied = 0U;
    frame_length = test_frame_size(&port.output[output_offset]);
    chunk = test_get_u16(&port.output[output_offset + 24U]);
    if (test_expect(port.output[output_offset + 3U] ==
                    MS_PROBE_CMD_DATA_READ,
                    "first interleaved frame is data") != 0 ||
        test_expect(chunk <= 50U, "data chunk payload budget") != 0 ||
        test_expect(memcmp(&port.output[output_offset + 26U],
                           &port.memory[4U], chunk) == 0,
                    "first data bytes") != 0)
    {
        return -1;
    }
    copied = chunk;
    output_offset = frame_length;
    frame_length = test_frame_size(&port.output[output_offset]);
    if (test_expect(port.output[output_offset + 3U] == MS_PROBE_CMD_STATUS,
                    "status is serviced between data chunks") != 0)
    {
        return -1;
    }
    output_offset = (ms_probe_u16)(output_offset + frame_length);
    while (output_offset < port.output_length)
    {
        frame_length = test_frame_size(&port.output[output_offset]);
        chunk = test_get_u16(&port.output[output_offset + 24U]);
        if (test_expect(port.output[output_offset + 3U] ==
                        MS_PROBE_CMD_DATA_READ,
                        "remaining data command") != 0 ||
            test_expect(memcmp(&port.output[output_offset + 26U],
                               &port.memory[4U + copied], chunk) == 0,
                        "remaining data bytes") != 0)
        {
            return -1;
        }
        copied = (ms_probe_u16)(copied + chunk);
        output_offset = (ms_probe_u16)(output_offset + frame_length);
    }
    if (test_expect(copied == 120U, "segmented read complete") != 0 ||
        test_expect(port.max_read_memory <= MS_PROBE_MEMORY_BUDGET,
                    "memory read uses fixed budget") != 0)
    {
        return -1;
    }

    test_clear_output(&port);
    request_length = test_make_request(request, MS_PROBE_CMD_CONTEXT_READ,
                                       5U, session, NULL, 0U);
    ms_probe_input(&probe, request, request_length);
    if (test_expect(test_drain(&probe, 512U) == 0,
                    "context transfer drain") != 0 ||
        test_expect(port.output_length > sizeof(context),
                    "segmented context response") != 0)
    {
        return -1;
    }

    test_clear_output(&port);
    request_length = test_make_request(request, MS_PROBE_CMD_TARGET_INFO,
                                       6U, session, NULL, 0U);
    ms_probe_input(&probe, request, request_length);
    if (test_expect(test_drain(&probe, 128U) == 0,
                    "target info drain") != 0 ||
        test_expect((test_get_u16(&port.output[18]) &
                     MS_PROBE_CAP_CANCEL_SUPPORTED) == 0U,
                    "target info omits cancel capability") != 0)
    {
        return -1;
    }

    test_clear_output(&port);
    payload[0] = 3U;
    request_length = test_make_request(request, MS_PROBE_CMD_CANCEL,
                                       7U, session, payload, 1U);
    ms_probe_input(&probe, request, request_length);
    if (test_expect(test_drain(&probe, 128U) == 0,
                    "cancel error drain") != 0 ||
        test_expect(port.output[3] == MS_PROBE_CMD_ERROR,
                    "cancel rejected") != 0 ||
        test_expect(test_get_u16(&port.output[13]) ==
                    MS_PROBE_ERROR_UNSUPPORTED_COMMAND,
                    "cancel unsupported error") != 0)
    {
        return -1;
    }

    test_clear_output(&port);
    watchdog_before = port.watchdog_calls;
    request_length = test_make_request(request, MS_PROBE_CMD_DOG_FEED,
                                       8U, session, NULL, 0U);
    (void)memcpy(port.input, request, request_length);
    port.input_length = request_length;
    port.input_position = 0U;
    ms_probe_step(&probe);
    if (test_expect(port.last_read_capacity == MS_PROBE_IO_BUDGET,
                    "RX uses fixed budget") != 0 ||
        test_expect(port.watchdog_calls > watchdog_before,
                    "each step services watchdog") != 0 ||
        test_expect(test_drain(&probe, 128U) == 0,
                    "poll RX watchdog response") != 0 ||
        test_expect(port.output[3] == MS_PROBE_CMD_DOG_FEED,
                    "poll RX command dispatched") != 0)
    {
        return -1;
    }

    port.input_length = 0U;
    port.input_position = 0U;
    test_clear_output(&port);
    request_length = test_make_request(request, MS_PROBE_CMD_RESET,
                                       9U, session, NULL, 0U);
    ms_probe_input(&probe, request, request_length);
    if (test_expect(test_drain(&probe, 128U) == 0,
                    "reset drain") != 0 ||
        test_expect(port.reset_called == 1,
                    "reset callback after response") != 0 ||
        test_expect(probe.state == MS_PROBE_STATE_FAULT,
                    "returned reset keeps fault loop active") != 0)
    {
        return -1;
    }
    return 0;
}

int main(void)
{
    if (test_protocol_flow() != 0)
    {
        return 1;
    }
    (void)printf("ms-probe tests passed\n");
    return 0;
}
