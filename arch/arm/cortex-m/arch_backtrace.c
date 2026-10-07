/*
 * Copyright (c) 2006-2026 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "backtrace_internal.h"

#define CORTEX_M_BL_FIRST_MASK      0xf800U
#define CORTEX_M_BL_FIRST_OPCODE    0xf000U
#define CORTEX_M_BL_SECOND_MASK     0xd000U
#define CORTEX_M_BL_SECOND_OPCODE   0xd000U
#define CORTEX_M_BLX_MASK           0xff87U
#define CORTEX_M_BLX_OPCODE         0x4780U

extern const rt_uint8_t _stext RT_WEAK;
extern const rt_uint8_t _etext RT_WEAK;
extern const rt_uint8_t __text_start__ RT_WEAK;
extern const rt_uint8_t __text_end__ RT_WEAK;
extern const rt_uint8_t __kernel_rom_start__ RT_WEAK;
extern const rt_uint8_t __kernel_rom_end__ RT_WEAK;
extern rt_bool_t is_in_code_range(rt_uint32_t address) RT_WEAK;

static rt_ubase_t backtrace_min_nonzero(rt_ubase_t left, rt_ubase_t right)
{
    rt_ubase_t result;

    result = left;
    if ((result == 0U) || ((right != 0U) && (right < result)))
    {
        result = right;
    }

    return result;
}

static rt_ubase_t backtrace_max(rt_ubase_t left, rt_ubase_t right)
{
    rt_ubase_t result;

    result = left;
    if (right > result)
    {
        result = right;
    }

    return result;
}

static rt_bool_t backtrace_code_contains(rt_ubase_t address, rt_size_t size)
{
    rt_ubase_t start;
    rt_ubase_t end;

    if (((rt_ubase_t)is_in_code_range != 0U) &&
        (address <= (rt_ubase_t)0xffffffffU) &&
        (is_in_code_range((rt_uint32_t)address) == RT_TRUE))
    {
        return RT_TRUE;
    }

    start = backtrace_min_nonzero((rt_ubase_t)&_stext,
                                  (rt_ubase_t)&__text_start__);
    start = backtrace_min_nonzero(start,
                                  (rt_ubase_t)&__kernel_rom_start__);
    end = backtrace_max((rt_ubase_t)&_etext,
                        (rt_ubase_t)&__text_end__);
    end = backtrace_max(end, (rt_ubase_t)&__kernel_rom_end__);

    if ((start == 0U) || (end <= start) || (address < start) ||
        (address >= end) || ((rt_ubase_t)size > (end - address)))
    {
        return RT_FALSE;
    }

    return RT_TRUE;
}

static rt_uint16_t backtrace_read_u16(rt_ubase_t address)
{
    return *((const volatile rt_uint16_t *)address);
}

static rt_bool_t backtrace_get_call_site(rt_ubase_t return_addr,
                                         rt_ubase_t *call_site)
{
    rt_ubase_t address;
    rt_uint16_t first;
    rt_uint16_t second;

    if ((return_addr & 1U) == 0U)
    {
        return RT_FALSE;
    }
    return_addr &= ~(rt_ubase_t)1U;

    if (return_addr >= 4U)
    {
        address = return_addr - 4U;
        if (backtrace_code_contains(address, 4U) == RT_TRUE)
        {
            first = backtrace_read_u16(address);
            second = backtrace_read_u16(address + 2U);
            if (((first & CORTEX_M_BL_FIRST_MASK) ==
                 CORTEX_M_BL_FIRST_OPCODE) &&
                ((second & CORTEX_M_BL_SECOND_MASK) ==
                 CORTEX_M_BL_SECOND_OPCODE))
            {
                *call_site = address;
                return RT_TRUE;
            }
        }
    }

    if (return_addr >= 2U)
    {
        address = return_addr - 2U;
        if ((backtrace_code_contains(address, 2U) == RT_TRUE) &&
            ((backtrace_read_u16(address) & CORTEX_M_BLX_MASK) ==
             CORTEX_M_BLX_OPCODE))
        {
            *call_site = address;
            return RT_TRUE;
        }
    }

    return RT_FALSE;
}

RT_WEAK rt_err_t rt_hw_backtrace_get_msp_stack(rt_ubase_t *stack_addr,
                                                rt_size_t *stack_size)
{
    if ((stack_addr == RT_NULL) || (stack_size == RT_NULL))
    {
        return -RT_EINVAL;
    }

    *stack_addr = 0U;
    *stack_size = 0U;

    return -RT_ENOSYS;
}

rt_err_t rt_backtrace_arch_get_current_frame(struct rt_backtrace_frame *frame)
{
    volatile rt_ubase_t stack_marker;

    if (frame == RT_NULL)
    {
        return -RT_EINVAL;
    }

    stack_marker = 0U;
    frame->sp = (rt_ubase_t)&stack_marker;
    frame->valid |= RT_BACKTRACE_FRAME_SP_VALID;

    return RT_EOK;
}

rt_size_t rt_backtrace_arch_unwind(rt_ubase_t *buffer,
                                   rt_size_t size,
                                   const struct rt_backtrace_frame *frame)
{
    rt_ubase_t stack_end;
    rt_ubase_t scan;
    rt_ubase_t call_site;
    rt_size_t depth;

    depth = 0U;
    if (((frame->valid & RT_BACKTRACE_FRAME_PC_VALID) != 0U) &&
        (depth < size))
    {
        buffer[depth] = frame->pc & ~(rt_ubase_t)1U;
        depth++;
    }

    if ((frame->valid & (RT_BACKTRACE_FRAME_SP_VALID |
                         RT_BACKTRACE_FRAME_STACK_VALID)) !=
        (RT_BACKTRACE_FRAME_SP_VALID | RT_BACKTRACE_FRAME_STACK_VALID))
    {
        return depth;
    }

    stack_end = frame->stack_addr + (rt_ubase_t)frame->stack_size;
    scan = RT_ALIGN(frame->sp, sizeof(rt_ubase_t));

    if ((scan < frame->sp) || (scan >= stack_end))
    {
        return depth;
    }

    while (((stack_end - scan) >= sizeof(rt_ubase_t)) && (depth < size))
    {
        if (backtrace_get_call_site(
                *((const volatile rt_ubase_t *)scan),
                &call_site) == RT_TRUE)
        {
            buffer[depth] = call_site;
            depth++;
        }
        scan += sizeof(rt_ubase_t);
    }

    return depth;
}
