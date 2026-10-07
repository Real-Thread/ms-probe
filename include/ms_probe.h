/*
 * Copyright (c) 2006-2023 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-09-04     Bernard      add bare-metal scope probe interface
 */

#ifndef MS_PROBE_H
#define MS_PROBE_H

#include <limits.h>
#include <stddef.h>

#if UINT_MAX != 0xffffffffU
#error "ms-probe requires a 32-bit unsigned int"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MS_PROBE_PROTOCOL_VERSION       0x20U
#define MS_PROBE_PROTOCOL_MAJOR         0x02U
#define MS_PROBE_HEADER_SIZE            12U
#define MS_PROBE_PROTOCOL_MAX_PAYLOAD   1020U
#define MS_PROBE_BUILD_ID_MAX           32U
#define MS_PROBE_CONTEXT_HEADER_SIZE    24U
#define MS_PROBE_FRAME_MAX_SIZE         \
    (MS_PROBE_HEADER_SIZE + MS_PROBE_PROTOCOL_MAX_PAYLOAD + 4U)
#define MS_PROBE_IO_BUDGET              16U
#define MS_PROBE_MEMORY_BUDGET          64U

#define MS_PROBE_FLAG_PAYLOAD_CRC       0x01U
#define MS_PROBE_FLAG_RESPONSE          0x02U
#define MS_PROBE_FLAG_RETRANSMIT        0x04U
#define MS_PROBE_FLAG_MORE              0x08U

#define MS_PROBE_CAP_MEMORY_STABLE      0x0001U
#define MS_PROBE_CAP_CONTEXT_VALID      0x0002U
#define MS_PROBE_CAP_FPU_CONTEXT        0x0004U
#define MS_PROBE_CAP_RTTHREAD_PRESENT   0x0008U
#define MS_PROBE_CAP_WATCHDOG_REQUIRED  0x0010U
#define MS_PROBE_CAP_CANCEL_SUPPORTED   0x0020U

#define MS_PROBE_ARCH_ARMV6_M           0x01U
#define MS_PROBE_ARCH_ARMV7_M           0x02U
#define MS_PROBE_ARCH_ARMV7E_M          0x03U
#define MS_PROBE_ARCH_ARMV8_M           0x04U
#define MS_PROBE_ARCH_ARMV8_R_AARCH32   0x10U
#define MS_PROBE_ARCH_ARMV7_A           0x11U
#define MS_PROBE_ARCH_RV32              0x20U
#define MS_PROBE_ARCH_KUNGFU32          0x30U

#define MS_PROBE_ENDIAN_LITTLE          0x00U
#define MS_PROBE_ENDIAN_BIG             0x01U

#define MS_PROBE_STATE_INACTIVE         0x00U
#define MS_PROBE_STATE_FAULT            0x01U
#define MS_PROBE_STATE_RESETTING        0x03U

#define MS_PROBE_CMD_HANDSHAKE          0x01U
#define MS_PROBE_CMD_TARGET_INFO        0x02U
#define MS_PROBE_CMD_DATA_READ          0x03U
#define MS_PROBE_CMD_CONTEXT_READ       0x04U
#define MS_PROBE_CMD_STATUS             0x05U
#define MS_PROBE_CMD_DOG_FEED           0x06U
#define MS_PROBE_CMD_RESET              0x07U
#define MS_PROBE_CMD_CANCEL             0x08U
#define MS_PROBE_CMD_HELP               0x09U
#define MS_PROBE_CMD_ERROR              0x7fU

#define MS_PROBE_ERROR_BAD_FRAME            0x0001U
#define MS_PROBE_ERROR_BAD_CRC              0x0002U
#define MS_PROBE_ERROR_UNSUPPORTED_VERSION  0x0003U
#define MS_PROBE_ERROR_UNSUPPORTED_COMMAND  0x0004U
#define MS_PROBE_ERROR_INVALID_SESSION      0x0005U
#define MS_PROBE_ERROR_NOT_ACTIVE           0x0006U
#define MS_PROBE_ERROR_INVALID_ADDRESS      0x0007U
#define MS_PROBE_ERROR_INVALID_LENGTH       0x0008U
#define MS_PROBE_ERROR_CONTEXT_UNAVAILABLE  0x0009U
#define MS_PROBE_ERROR_BUSY                 0x000aU
#define MS_PROBE_ERROR_EPOCH_CHANGED        0x000bU

typedef unsigned char ms_probe_u8;
typedef unsigned short ms_probe_u16;
typedef unsigned int ms_probe_u32;

struct ms_probe_region
{
    ms_probe_u32 start;
    ms_probe_u32 size;
};

struct ms_probe_fault_ops
{
    int (*enter_poll_mode)(void *user);
    int (*poll_read)(void *user, ms_probe_u8 *data,
                     ms_probe_u16 capacity);
    int (*poll_write)(void *user, const ms_probe_u8 *data,
                      ms_probe_u16 length);
    int (*read_memory)(void *user, ms_probe_u32 address,
                       ms_probe_u8 *data, ms_probe_u16 length);
    ms_probe_u32 (*now_ms)(void *user);
    void (*service_watchdog)(void *user);
    void (*reset)(void *user);
};

struct ms_probe_config
{
    struct ms_probe_fault_ops ops;
    void *user;
    const struct ms_probe_region *regions;
    ms_probe_u8 region_count;
    const ms_probe_u8 *build_id;
    ms_probe_u8 build_id_length;
    const ms_probe_u8 *context;
    ms_probe_u32 context_length;
    ms_probe_u16 context_version;
    ms_probe_u16 max_payload;
    ms_probe_u32 max_read_length;
    ms_probe_u32 frame_timeout_ms;
    ms_probe_u32 device_id;
    ms_probe_u16 capabilities;
    ms_probe_u8 architecture;
    ms_probe_u8 endianness;
    ms_probe_u8 address_width;
    ms_probe_u8 probe_version_major;
    ms_probe_u8 probe_version_minor;
};

struct ms_probe
{
    struct ms_probe_config config;
    ms_probe_u8 header[MS_PROBE_HEADER_SIZE];
    ms_probe_u8 payload[MS_PROBE_PROTOCOL_MAX_PAYLOAD];
    ms_probe_u8 payload_crc[4];
    ms_probe_u8 tx_frame[MS_PROBE_FRAME_MAX_SIZE];
    ms_probe_u8 input_pending[MS_PROBE_IO_BUDGET];
    ms_probe_u16 header_position;
    ms_probe_u16 payload_position;
    ms_probe_u16 payload_length;
    ms_probe_u16 request_payload_length;
    ms_probe_u16 tx_length;
    ms_probe_u16 tx_position;
    ms_probe_u8 input_pending_length;
    ms_probe_u8 payload_crc_position;
    ms_probe_u8 parser_state;
    ms_probe_u8 request_command;
    ms_probe_u8 request_flags;
    ms_probe_u8 request_sequence;
    ms_probe_u8 request_ready;
    ms_probe_u8 prompt_requests;
    ms_probe_u8 last_input_was_cr;
    ms_probe_u8 transfer_kind;
    ms_probe_u8 transfer_sequence;
    ms_probe_u8 reset_pending;
    ms_probe_u8 time_initialized;
    ms_probe_u8 state;
    ms_probe_u16 request_error;
    ms_probe_u32 request_session;
    ms_probe_u32 session_id;
    ms_probe_u32 fault_epoch;
    ms_probe_u32 session_counter;
    ms_probe_u32 parser_idle_ms;
    ms_probe_u32 last_poll_ms;
    ms_probe_u32 rx_crc;
    ms_probe_u32 transfer_address;
    ms_probe_u32 transfer_length;
    ms_probe_u32 transfer_offset;
    ms_probe_u32 transfer_session;
    ms_probe_u32 transfer_crc;
};

int ms_probe_init(struct ms_probe *probe, const struct ms_probe_config *config);
void ms_probe_activate(struct ms_probe *probe, ms_probe_u32 fault_epoch);
void ms_probe_set_context(struct ms_probe *probe, const ms_probe_u8 *context,
                          ms_probe_u32 length, ms_probe_u16 version,
                          int valid);
void ms_probe_input(struct ms_probe *probe, const ms_probe_u8 *data,
                    ms_probe_u16 length);
void ms_probe_poll(struct ms_probe *probe, ms_probe_u32 elapsed_ms);
void ms_probe_step(struct ms_probe *probe);
void ms_probe_run_fault(struct ms_probe *probe);
ms_probe_u8 ms_probe_crc8(const ms_probe_u8 *data, ms_probe_u16 length);
ms_probe_u32 ms_probe_crc32(const ms_probe_u8 *data, ms_probe_u32 length);

#ifdef __cplusplus
}
#endif

#endif
