/*
 * Copyright (c) 2006-2023 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "backtrace_internal.h"

#define KUNGFU_LJMP_DIRECT_MASK       0xffe0U
#define KUNGFU_LJMP_DIRECT_OPCODE     0x0080U
#define KUNGFU_LJMP_REGISTER_MASK     0xfff0U
#define KUNGFU_LJMP_REGISTER_OPCODE   0x5c00U

extern const rt_uint8_t __kernel_rom_start__ RT_WEAK;
extern const rt_uint8_t __kernel_rom_end__ RT_WEAK;
extern const rt_uint8_t __text_end__ RT_WEAK;
extern const rt_uint8_t __share_code_end__ RT_WEAK;
extern const rt_uint8_t __data_start__ RT_WEAK;
extern const rt_uint8_t __data_end__ RT_WEAK;

static rt_ubase_t backtrace_code_end(void)
{
    rt_ubase_t end;
    rt_ubase_t address;

    end = (rt_ubase_t)&__kernel_rom_end__;
    address = (rt_ubase_t)&__text_end__;
    if (address > end)
    {
        end = address;
    }

    address = (rt_ubase_t)&__share_code_end__;
    if (address > end)
    {
        end = address;
    }

    return end;
}

static rt_bool_t backtrace_range_contains(rt_ubase_t start,
                                          rt_ubase_t end,
                                          rt_ubase_t address,
                                          rt_size_t size)
{
    rt_bool_t result;

    result = RT_FALSE;

    if ((end > start) && (address >= start) && (address < end) &&
        ((rt_ubase_t)size <= (end - address)))
    {
        result = RT_TRUE;
    }

    return result;
}

static rt_bool_t backtrace_code_contains(rt_ubase_t address, rt_size_t size)
{
    rt_ubase_t start;
    rt_ubase_t end;

    start = (rt_ubase_t)&__kernel_rom_start__;
    end = backtrace_code_end();
    if (backtrace_range_contains(start, end, address, size) == RT_TRUE)
    {
        return RT_TRUE;
    }

    start = (rt_ubase_t)&__data_start__;
    end = (rt_ubase_t)&__data_end__;

    return backtrace_range_contains(start, end, address, size);
}

static rt_uint16_t backtrace_read_u16(rt_ubase_t address)
{
    return *((const volatile rt_uint16_t *)address);
}

static rt_bool_t backtrace_get_call_site(rt_ubase_t return_addr,
                                         rt_ubase_t *call_site)
{
    rt_ubase_t instruction_addr;
    rt_uint16_t instruction;

    if ((call_site == RT_NULL) || ((return_addr & 1U) != 0U))
    {
        return RT_FALSE;
    }

    if (return_addr >= 4U)
    {
        instruction_addr = return_addr - 4U;
        if (backtrace_code_contains(instruction_addr, 4U) == RT_TRUE)
        {
            instruction = backtrace_read_u16(instruction_addr);
            if ((instruction & KUNGFU_LJMP_DIRECT_MASK) ==
                KUNGFU_LJMP_DIRECT_OPCODE)
            {
                *call_site = instruction_addr;
                return RT_TRUE;
            }
        }
    }

    if (return_addr >= 2U)
    {
        instruction_addr = return_addr - 2U;
        if (backtrace_code_contains(instruction_addr, 2U) == RT_TRUE)
        {
            instruction = backtrace_read_u16(instruction_addr);
            if ((instruction & KUNGFU_LJMP_REGISTER_MASK) ==
                KUNGFU_LJMP_REGISTER_OPCODE)
            {
                *call_site = instruction_addr;
                return RT_TRUE;
            }
        }
    }

    return RT_FALSE;
}

extern rt_ubase_t rt_backtrace_arch_get_sp(void);

rt_err_t rt_backtrace_arch_get_current_frame(struct rt_backtrace_frame *frame)
{
    if (frame == RT_NULL)
    {
        return -RT_EINVAL;
    }

    frame->sp = rt_backtrace_arch_get_sp();
    frame->valid |= RT_BACKTRACE_FRAME_SP_VALID;

    return RT_EOK;
}

rt_size_t rt_backtrace_arch_unwind(rt_ubase_t *buffer,
                                   rt_size_t size,
                                   const struct rt_backtrace_frame *frame)
{
    rt_ubase_t stack_end;
    rt_ubase_t scan;
    rt_ubase_t return_addr;
    rt_ubase_t call_site;
    rt_size_t depth;

    if ((frame->valid & (RT_BACKTRACE_FRAME_SP_VALID |
                         RT_BACKTRACE_FRAME_STACK_VALID)) !=
        (RT_BACKTRACE_FRAME_SP_VALID | RT_BACKTRACE_FRAME_STACK_VALID))
    {
        return 0U;
    }

    depth = 0U;
    stack_end = frame->stack_addr + (rt_ubase_t)frame->stack_size;
    scan = RT_ALIGN(frame->sp, sizeof(rt_ubase_t));
    if ((scan < frame->sp) || (scan >= stack_end))
    {
        return 0U;
    }

    while (((stack_end - scan) >= sizeof(rt_ubase_t)) && (depth < size))
    {
        return_addr = *((const volatile rt_ubase_t *)scan);
        if (backtrace_get_call_site(return_addr, &call_site) == RT_TRUE)
        {
            buffer[depth] = call_site;
            depth++;
        }
        scan += sizeof(rt_ubase_t);
    }

    return depth;
}
