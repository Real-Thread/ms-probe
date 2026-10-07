/*
 * Copyright (c) 2006-2023 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include <finsh.h>

#include "rt_backtrace.h"

#define BACKTRACE_TEST_DEPTH_MIN 3U

#if defined(__GNUC__)
#define BACKTRACE_TEST_NOINLINE __attribute__((noinline))
#else
#define BACKTRACE_TEST_NOINLINE
#endif

static volatile rt_size_t backtrace_test_sink;

static BACKTRACE_TEST_NOINLINE rt_size_t backtrace_test_level_three(void)
{
    rt_size_t depth;

    depth = rt_backtrace();
    backtrace_test_sink = depth;

    return depth;
}

static BACKTRACE_TEST_NOINLINE rt_size_t backtrace_test_level_two(void)
{
    rt_size_t depth;

    depth = backtrace_test_level_three();
    backtrace_test_sink = depth;

    return depth;
}

static BACKTRACE_TEST_NOINLINE rt_size_t backtrace_test_level_one(void)
{
    rt_size_t depth;

    depth = backtrace_test_level_two();
    backtrace_test_sink = depth;

    return depth;
}

static void backtrace_test(int argc, char **argv)
{
    rt_size_t depth;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    depth = backtrace_test_level_one();
    if (depth >= BACKTRACE_TEST_DEPTH_MIN)
    {
        rt_kprintf("backtrace test: passed\n");
    }
    else
    {
        rt_kprintf("backtrace test: failed, depth %u\n", (unsigned int)depth);
    }
}
MSH_CMD_EXPORT(backtrace_test, Run stack backtrace test.);
