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
 * 2026-09-04     Bernard      add series bare-metal port helpers
 */

#ifndef MS_SCOPE_BAREMETAL_H
#define MS_SCOPE_BAREMETAL_H

#include "ms_probe.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ms_scope_baremetal_io
{
    int (*read_byte)(void *user, ms_probe_u8 *data);
    int (*write_byte)(void *user, ms_probe_u8 data);
    ms_probe_u32 (*now_ms)(void *user);
    void (*service_watchdog)(void *user);
    void (*reset)(void *user);
    void *user;
};

void ms_scope_baremetal_bind(struct ms_probe_fault_ops *ops,
                             struct ms_scope_baremetal_io *io);
int ms_scope_baremetal_poll_read(void *user, ms_probe_u8 *data,
                                 ms_probe_u16 capacity);
int ms_scope_baremetal_poll_write(void *user, const ms_probe_u8 *data,
                                  ms_probe_u16 length);
ms_probe_u32 ms_scope_baremetal_now_ms(void *user);
void ms_scope_baremetal_service_watchdog(void *user);
void ms_scope_baremetal_reset(void *user);

#ifdef __cplusplus
}
#endif

#endif
