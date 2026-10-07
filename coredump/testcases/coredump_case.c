/*
 * Copyright (c) 2006-2023 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include <rtthread.h>
#include <finsh.h>

#include "coredump_case.h"

static struct rt_thread cd_tid;
static rt_uint8_t cd_stack[1024];

static void cd_entry(void *parameter)
{
    int *ptr = (int *)INVALID_ADDRESS;

    /* Inject a fault. */
    *ptr = 123;

    /* Never reach here. */
    while (1);
}

void coredump_test(int argc, char **argv)
{
    rt_thread_init(&cd_tid, "cd", cd_entry, RT_NULL, cd_stack, sizeof(cd_stack) / sizeof(cd_stack[0]), 10, 10);
    rt_thread_startup(&cd_tid);
}
MSH_CMD_EXPORT(coredump_test, coredump test);
