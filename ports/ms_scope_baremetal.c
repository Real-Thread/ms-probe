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

#include "ms_scope_baremetal.h"

void ms_scope_baremetal_bind(struct ms_probe_fault_ops *ops,
                             struct ms_scope_baremetal_io *io)
{
    if ((ops == NULL) || (io == NULL))
    {
        return;
    }
    ops->poll_read = ms_scope_baremetal_poll_read;
    ops->poll_write = ms_scope_baremetal_poll_write;
    ops->now_ms = (io->now_ms != NULL) ? ms_scope_baremetal_now_ms : NULL;
    ops->service_watchdog =
        (io->service_watchdog != NULL) ?
        ms_scope_baremetal_service_watchdog : NULL;
    ops->reset = (io->reset != NULL) ? ms_scope_baremetal_reset : NULL;
}

int ms_scope_baremetal_poll_read(void *user, ms_probe_u8 *data,
                                 ms_probe_u16 capacity)
{
    struct ms_scope_baremetal_io *io;
    ms_probe_u16 count;
    ms_probe_u8 value;

    if ((user == NULL) || (data == NULL) || (capacity == 0U))
    {
        return -1;
    }
    io = (struct ms_scope_baremetal_io *)user;
    if (io->read_byte == NULL)
    {
        return -1;
    }
    count = 0U;
    while (count < capacity)
    {
        if (io->read_byte(io->user, &value) <= 0)
        {
            break;
        }
        data[count] = value;
        count++;
    }
    return (int)count;
}

int ms_scope_baremetal_poll_write(void *user, const ms_probe_u8 *data,
                                  ms_probe_u16 length)
{
    struct ms_scope_baremetal_io *io;
    ms_probe_u16 count;

    if ((user == NULL) || (data == NULL) || (length == 0U))
    {
        return -1;
    }
    io = (struct ms_scope_baremetal_io *)user;
    if (io->write_byte == NULL)
    {
        return -1;
    }
    count = 0U;
    while (count < length)
    {
        if (io->write_byte(io->user, data[count]) <= 0)
        {
            break;
        }
        count++;
    }
    return (int)count;
}

ms_probe_u32 ms_scope_baremetal_now_ms(void *user)
{
    struct ms_scope_baremetal_io *io;

    if (user == NULL)
    {
        return 0U;
    }
    io = (struct ms_scope_baremetal_io *)user;
    if (io->now_ms == NULL)
    {
        return 0U;
    }
    return io->now_ms(io->user);
}

void ms_scope_baremetal_service_watchdog(void *user)
{
    struct ms_scope_baremetal_io *io;

    if (user != NULL)
    {
        io = (struct ms_scope_baremetal_io *)user;
        if (io->service_watchdog != NULL)
        {
            io->service_watchdog(io->user);
        }
    }
}

void ms_scope_baremetal_reset(void *user)
{
    struct ms_scope_baremetal_io *io;

    if (user != NULL)
    {
        io = (struct ms_scope_baremetal_io *)user;
        if (io->reset != NULL)
        {
            io->reset(io->user);
        }
    }
}
