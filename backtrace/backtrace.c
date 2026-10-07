/*
 * Copyright (c) 2006-2023 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "rt_backtrace.h"
#include "backtrace_internal.h"

#define BACKTRACE_ADDRESS_HEX_DIGITS (sizeof(rt_ubase_t) * 2U)
#define BACKTRACE_ADDRESS_TEXT_SIZE  (BACKTRACE_ADDRESS_HEX_DIGITS + 3U)

static const char backtrace_hex_digits[] = "0123456789abcdef";

static rt_size_t backtrace_append_address(char *output,
                                          rt_size_t offset,
                                          rt_ubase_t address)
{
    rt_size_t digit_index;
    rt_ubase_t digit;

    output[offset] = '0';
    offset++;
    output[offset] = 'x';
    offset++;

    digit_index = BACKTRACE_ADDRESS_HEX_DIGITS;
    while (digit_index > 0U)
    {
        digit_index--;
        digit = (address >> (digit_index * 4U)) & (rt_ubase_t)0x0fU;
        output[offset] = backtrace_hex_digits[(rt_size_t)digit];
        offset++;
    }

    return offset;
}

static void backtrace_print_buffer(const rt_ubase_t *buffer, rt_size_t depth)
{
    char output[(RT_STACK_BACKTRACE_DEPTH_MAX * BACKTRACE_ADDRESS_TEXT_SIZE) + 1U];
    rt_size_t index;
    rt_size_t offset;

    offset = 0U;
    for (index = 0U; index < depth; index++)
    {
        if (index != 0U)
        {
            output[offset] = ' ';
            offset++;
        }

        offset = backtrace_append_address(output, offset, buffer[index]);
    }

    output[offset] = '\0';
    (void)rt_kprintf("%s\n", output);
}

rt_size_t rt_backtrace_from_frame(rt_ubase_t *buffer,
                                  rt_size_t size,
                                  const struct rt_backtrace_frame *frame)
{
    rt_ubase_t stack_end;

    if ((buffer == RT_NULL) || (size == 0U) || (frame == RT_NULL))
    {
        return 0U;
    }

    if ((frame->valid & RT_BACKTRACE_FRAME_STACK_VALID) != 0U)
    {
        stack_end = frame->stack_addr + (rt_ubase_t)frame->stack_size;
        if ((stack_end < frame->stack_addr) ||
            (((frame->valid & RT_BACKTRACE_FRAME_SP_VALID) != 0U) &&
             ((frame->sp < frame->stack_addr) || (frame->sp >= stack_end))))
        {
            return 0U;
        }
    }

    return rt_backtrace_arch_unwind(buffer, size, frame);
}

rt_size_t rt_backtrace_from_stack(rt_ubase_t *buffer,
                                  rt_size_t size,
                                  rt_ubase_t sp,
                                  rt_ubase_t stack_addr,
                                  rt_size_t stack_size)
{
    struct rt_backtrace_frame frame;

    (void)rt_memset(&frame, 0, sizeof(frame));
    frame.sp = sp;
    frame.stack_addr = stack_addr;
    frame.stack_size = stack_size;
    frame.valid = RT_BACKTRACE_FRAME_SP_VALID |
        RT_BACKTRACE_FRAME_STACK_VALID;

    return rt_backtrace_from_frame(buffer, size, &frame);
}

rt_size_t rt_backtrace_capture(rt_ubase_t *buffer, rt_size_t size)
{
    rt_thread_t thread;
    struct rt_backtrace_frame frame;
    volatile rt_size_t depth;

    thread = rt_thread_self();
    if (thread == RT_NULL)
    {
        return 0U;
    }

    (void)rt_memset(&frame, 0, sizeof(frame));
    frame.stack_addr = (rt_ubase_t)thread->stack_addr;
    frame.stack_size = (rt_size_t)thread->stack_size;
    frame.valid = RT_BACKTRACE_FRAME_STACK_VALID;
    if (rt_backtrace_arch_get_current_frame(&frame) != RT_EOK)
    {
        return 0U;
    }

    depth = rt_backtrace_from_frame(buffer, size, &frame);

    return depth;
}

rt_size_t rt_backtrace_print_frame(const struct rt_backtrace_frame *frame)
{
    rt_ubase_t buffer[RT_STACK_BACKTRACE_DEPTH_MAX];
    rt_size_t depth;

    depth = rt_backtrace_from_frame(buffer,
                                    RT_STACK_BACKTRACE_DEPTH_MAX,
                                    frame);
    backtrace_print_buffer(buffer, depth);

    return depth;
}

rt_size_t rt_backtrace(void)
{
    rt_ubase_t buffer[RT_STACK_BACKTRACE_DEPTH_MAX];
    rt_size_t depth;

    depth = rt_backtrace_capture(buffer, RT_STACK_BACKTRACE_DEPTH_MAX);
    backtrace_print_buffer(buffer, depth);

    return depth;
}
