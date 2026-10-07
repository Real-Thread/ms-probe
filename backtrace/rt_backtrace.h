/*
 * Copyright (c) 2006-2023 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#ifndef __RT_BACKTRACE_H__
#define __RT_BACKTRACE_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_BACKTRACE_FRAME_PC_VALID       (1UL << 0U)
#define RT_BACKTRACE_FRAME_SP_VALID       (1UL << 1U)
#define RT_BACKTRACE_FRAME_FP_VALID       (1UL << 2U)
#define RT_BACKTRACE_FRAME_LR_VALID       (1UL << 3U)
#define RT_BACKTRACE_FRAME_STACK_VALID    (1UL << 4U)

struct rt_backtrace_frame
{
    rt_ubase_t pc;
    rt_ubase_t sp;
    rt_ubase_t fp;
    rt_ubase_t lr;
    rt_ubase_t stack_addr;
    rt_size_t stack_size;
    rt_uint32_t valid;
};

/* Capture call-site addresses from a normalized processor frame. */
rt_size_t rt_backtrace_from_frame(rt_ubase_t *buffer,
                                  rt_size_t size,
                                  const struct rt_backtrace_frame *frame);
/* Capture call-site addresses from a stack memory range. */
rt_size_t rt_backtrace_from_stack(rt_ubase_t *buffer,
                                  rt_size_t size,
                                  rt_ubase_t sp,
                                  rt_ubase_t stack_addr,
                                  rt_size_t stack_size);
/* Capture call-site addresses from the current thread stack. */
rt_size_t rt_backtrace_capture(rt_ubase_t *buffer, rt_size_t size);
/* Print a backtrace captured from a normalized processor frame. */
rt_size_t rt_backtrace_print_frame(const struct rt_backtrace_frame *frame);
/* Print and return the current thread backtrace depth. */
rt_size_t rt_backtrace(void);

#ifdef __cplusplus
}
#endif

#endif /* __RT_BACKTRACE_H__ */
