/*
 * Copyright (c) 2006-2023 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "backtrace_internal.h"

#define DBG_TAG          "Backtrace"
#define DBG_LVL          DBG_WARNING
#include <rtdbg.h>

struct unwind_idx
{
    rt_uint32_t addr_offset;
    rt_uint32_t insn;
};

struct stackframe
{
    rt_ubase_t fp;
    rt_ubase_t sp;
    rt_ubase_t lr;
    rt_ubase_t pc;
};

struct unwind_ctrl_block
{
    rt_ubase_t vrs[16];      /* virtual register set */
    const rt_uint32_t *insn; /* pointer to the current instructions word */
    rt_ubase_t sp_high;      /* highest value of sp allowed */
    /*
     * 1 : check for stack overflow for each register pop.
     * 0 : save overhead if there is plenty of stack remaining.
     */
    int check_each_pop;
    int entries;            /* number of entries left to interpret */
    int byte;           /* current byte number in the instructions word */
};

enum regs
{
#ifdef CONFIG_THUMB2_KERNEL
    FP = 7,
#else
    FP = 11,
#endif
    SP = 13,
    LR = 14,
    PC = 15
};

enum unwind_reason_code
{
    URC_OK = 0,
    URC_FAILURE = 9
};

static rt_ubase_t prel31_to_addr(const rt_uint32_t *ptr)
{
    rt_int32_t offset;

    offset = (rt_int32_t)(*ptr << 1U);
    offset >>= 1U;

    return (rt_ubase_t)ptr + (rt_ubase_t)offset;
}

/*
 * Binary search in the unwind index. The entries are
 * guaranteed to be sorted in ascending order by the linker.
 *
 * start = first entry
 * origin = first entry with positive offset (or stop if there is no such entry)
 * stop - 1 = last entry
 */
static const struct unwind_idx *search_index(rt_ubase_t addr,
        const struct unwind_idx *start,
        const struct unwind_idx *origin,
        const struct unwind_idx *stop)
{
    rt_ubase_t addr_prel31;

    /*
     * only search in the section with the matching sign. This way the
     * prel31 numbers can be compared as unsigned longs.
     */
    if (addr < (rt_ubase_t)start)
        /* negative offsets: [start; origin) */
        stop = origin;
    else
        /* positive offsets: [origin; stop) */
        start = origin;

    /* prel31 for address relavive to start */
    addr_prel31 = (addr - (rt_ubase_t)start) & (rt_ubase_t)0x7fffffffU;

    while (start < stop - 1)
    {
        const struct unwind_idx *mid = start + ((stop - start) >> 1);

        /*
         * As addr_prel31 is relative to start an offset is needed to
         * make it relative to mid.
         */
        if (addr_prel31 - ((rt_ubase_t)mid - (rt_ubase_t)start) <
                mid->addr_offset)
            stop = mid;
        else
        {
            /* keep addr_prel31 relative to start */
            addr_prel31 -= ((rt_ubase_t)mid - (rt_ubase_t)start);
            start = mid;
        }
    }

    if (start->addr_offset <= addr_prel31)
        return start;
    else
    {
        LOG_D("unwind: Unknown symbol address %08lx\n", addr);
        return RT_NULL;
    }
}

static const struct unwind_idx *unwind_find_origin(
        const struct unwind_idx *start, const struct unwind_idx *stop)
{
    while (start < stop)
    {
        const struct unwind_idx *mid = start + ((stop - start) >> 1);

        if (mid->addr_offset >= 0x40000000)
            /* negative offset */
            start = mid + 1;
        else
            /* positive offset */
            stop = mid;
    }

    return stop;
}

static const struct unwind_idx *unwind_find_idx(
        rt_ubase_t addr,
        const struct unwind_idx **origin_idx,
        const struct unwind_idx exidx_start[],
        const struct unwind_idx exidx_end[])
{
    const struct unwind_idx *idx = RT_NULL;

    if (*origin_idx == RT_NULL)
    {
        *origin_idx = unwind_find_origin(exidx_start, exidx_end);
    }
    idx = search_index(addr, exidx_start, *origin_idx, exidx_end);

    return idx;
}

static rt_ubase_t unwind_get_byte(struct unwind_ctrl_block *ctrl)
{
    rt_ubase_t ret;

    if (ctrl->entries <= 0)
    {
        LOG_D("unwind: Corrupt unwind table\n");
        return 0;
    }

    ret = (*ctrl->insn >> (ctrl->byte * 8)) & 0xff;

    if (ctrl->byte == 0)
    {
        ctrl->insn++;
        ctrl->entries--;
        ctrl->byte = 3;
    }
    else
        ctrl->byte--;

    return ret;
}

/* Before popping a register check whether it is feasible or not */
static int unwind_pop_register(struct unwind_ctrl_block *ctrl,
        rt_ubase_t **vsp, rt_uint32_t reg)
{
    if ((ctrl->check_each_pop != 0) &&
        (*vsp >= (rt_ubase_t *)ctrl->sp_high))
    {
        return -URC_FAILURE;
    }

    ctrl->vrs[reg] = *(*vsp)++;
    return URC_OK;
}

/* Helper functions to execute the instructions */
static int unwind_exec_pop_subset_r4_to_r13(struct unwind_ctrl_block *ctrl,
        rt_ubase_t mask)
{
    rt_ubase_t *vsp;
    int load_sp;
    rt_uint32_t reg;

    vsp = (rt_ubase_t *)ctrl->vrs[SP];
    reg = 4U;

    load_sp = (int)(mask & (1UL << 9U));
    while (mask != 0U)
    {
        if (((mask & 1U) != 0U) &&
            (unwind_pop_register(ctrl, &vsp, reg) != URC_OK))
        {
            return -URC_FAILURE;
        }
        mask >>= 1U;
        reg++;
    }
    if (load_sp == 0)
    {
        ctrl->vrs[SP] = (rt_ubase_t)vsp;
    }

    return URC_OK;
}

static int unwind_exec_pop_r4_to_rN(struct unwind_ctrl_block *ctrl,
        rt_ubase_t insn)
{
    rt_ubase_t *vsp;
    rt_uint32_t reg;

    vsp = (rt_ubase_t *)ctrl->vrs[SP];

    /* pop R4-R[4+bbb] */
    for (reg = 4U; reg <= (4U + (rt_uint32_t)(insn & 7U)); reg++)
    {
        if (unwind_pop_register(ctrl, &vsp, reg) != URC_OK)
        {
            return -URC_FAILURE;
        }
    }

    if ((insn & 0x8U) != 0U)
    {
        if (unwind_pop_register(ctrl, &vsp, 14U) != URC_OK)
        {
            return -URC_FAILURE;
        }
    }

    ctrl->vrs[SP] = (rt_ubase_t)vsp;

    return URC_OK;
}

static int unwind_exec_pop_subset_r0_to_r3(struct unwind_ctrl_block *ctrl,
        rt_ubase_t mask)
{
    rt_ubase_t *vsp;
    rt_uint32_t reg;

    vsp = (rt_ubase_t *)ctrl->vrs[SP];
    reg = 0U;

    /* pop R0-R3 according to mask */
    while (mask != 0U)
    {
        if (((mask & 1U) != 0U) &&
            (unwind_pop_register(ctrl, &vsp, reg) != URC_OK))
        {
            return -URC_FAILURE;
        }
        mask >>= 1U;
        reg++;
    }
    ctrl->vrs[SP] = (rt_ubase_t)vsp;

    return URC_OK;
}

/*
 * Execute the current unwind instruction.
 */
static int unwind_exec_insn(struct unwind_ctrl_block *ctrl)
{
    rt_ubase_t insn;
    int ret;

    insn = unwind_get_byte(ctrl);
    ret = URC_OK;

    if ((insn & 0xc0) == 0x00)
        ctrl->vrs[SP] += ((insn & 0x3f) << 2) + 4;
    else if ((insn & 0xc0) == 0x40)
        ctrl->vrs[SP] -= ((insn & 0x3f) << 2) + 4;
    else if ((insn & 0xf0) == 0x80)
    {
        rt_ubase_t mask;

        insn = (insn << 8) | unwind_get_byte(ctrl);
        mask = insn & 0x0fff;
        if (mask == 0)
        {
            LOG_D("unwind: 'Refuse to unwind' instruction %04lx\n", insn);
            return -URC_FAILURE;
        }

        ret = unwind_exec_pop_subset_r4_to_r13(ctrl, mask);
        if (ret != URC_OK)
        {
            goto error;
        }
    }
    else if ((insn & 0xf0) == 0x90 &&
            (insn & 0x0d) != 0x0d)
        ctrl->vrs[SP] = ctrl->vrs[insn & 0x0f];
    else if ((insn & 0xf0) == 0xa0)
    {
        ret = unwind_exec_pop_r4_to_rN(ctrl, insn);
        if (ret != URC_OK)
        {
            goto error;
        }
    }
    else if (insn == 0xb0)
    {
        if (ctrl->vrs[PC] == 0)
            ctrl->vrs[PC] = ctrl->vrs[LR];
        /* no further processing */
        ctrl->entries = 0;
    }
    else if (insn == 0xb1)
    {
        rt_ubase_t mask;

        mask = unwind_get_byte(ctrl);

        if ((mask == 0U) || ((mask & 0xf0U) != 0U))
        {
            LOG_D("unwind: Spare encoding %04lx\n", (insn << 8) | mask);
            return -URC_FAILURE;
        }

        ret = unwind_exec_pop_subset_r0_to_r3(ctrl, mask);
        if (ret != URC_OK)
        {
            goto error;
        }
    }
    else if (insn == 0xb2)
    {
        rt_ubase_t uleb128;

        uleb128 = unwind_get_byte(ctrl);

        ctrl->vrs[SP] += 0x204 + (uleb128 << 2);
    }
    else
    {
        LOG_D("unwind: Unhandled instruction %02lx\n", insn);
        return -URC_FAILURE;
    }

error:
    return ret;
}

/*
 * Unwind a single frame starting with *sp for the symbol at *pc. It
 * updates the *pc and *sp with the new values.
 */
static int unwind_frame(struct stackframe *frame,
                        const struct unwind_idx **origin_idx,
                        const struct unwind_idx exidx_start[],
                        const struct unwind_idx exidx_end[],
                        rt_ubase_t stack_high)
{
    rt_ubase_t low;
    const struct unwind_idx *idx;
    struct unwind_ctrl_block ctrl;

    /* Store the highest address on the stack to avoid crossing it. */
    low = frame->sp;
    ctrl.sp_high = stack_high;

    idx = unwind_find_idx(frame->pc, origin_idx, exidx_start, exidx_end);
    if (idx == RT_NULL)
    {
        LOG_D("unwind: Index not found %08lx\n", frame->pc);
        return -URC_FAILURE;
    }

    ctrl.vrs[FP] = frame->fp;
    ctrl.vrs[SP] = frame->sp;
    ctrl.vrs[LR] = frame->lr;
    ctrl.vrs[PC] = 0;

    if (idx->insn == 1U)
    {
        return -URC_FAILURE;
    }
    if ((idx->insn & 0x80000000U) == 0U)
    {
        ctrl.insn = (const rt_uint32_t *)prel31_to_addr(&idx->insn);
    }
    else if ((idx->insn & 0xff000000U) == 0x80000000U)
    {
        ctrl.insn = &idx->insn;
    }
    else
    {
        LOG_D("unwind: Unsupported personality routine %08lx in the index at %x\n",
                idx->insn, idx);
        return -URC_FAILURE;
    }

    /* check the personality routine */
    if ((*ctrl.insn & 0xff000000U) == 0x80000000U)
    {
        ctrl.byte = 2;
        ctrl.entries = 1;
    }
    else if ((*ctrl.insn & 0xff000000U) == 0x81000000U)
    {
        ctrl.byte = 1;
        ctrl.entries = 1 + (int)((*ctrl.insn & 0x00ff0000U) >> 16U);
    }
    else
    {
        LOG_D("unwind: Unsupported personality routine %08lx at %x\n",
                *ctrl.insn, ctrl.insn);
        return -URC_FAILURE;
    }

    ctrl.check_each_pop = 0;

    while (ctrl.entries > 0)
    {
        int urc;
        if ((ctrl.sp_high - ctrl.vrs[SP]) < sizeof(ctrl.vrs))
        {
            ctrl.check_each_pop = 1;
        }
        urc = unwind_exec_insn(&ctrl);
        if (urc < 0)
        {
            return urc;
        }
        if ((ctrl.vrs[SP] < low) || (ctrl.vrs[SP] >= ctrl.sp_high))
        {
            return -URC_FAILURE;
        }
    }

    if (ctrl.vrs[PC] == 0)
    {
        if (ctrl.vrs[LR] == 0)
        {
            return -URC_FAILURE;
        }
        ctrl.vrs[PC] = ctrl.vrs[LR];
    }

    /* check for infinite loop */
    if (frame->pc == ctrl.vrs[PC])
    {
        return -URC_FAILURE;
    }

    frame->fp = ctrl.vrs[FP];
    frame->sp = ctrl.vrs[SP];
    frame->lr = ctrl.vrs[LR];
    frame->pc = ctrl.vrs[PC];

    return URC_OK;
}

#if defined(__ARMCC_VERSION) && defined(__clang__)
extern const struct unwind_idx Image$$EXIDX$$Base[];
extern const struct unwind_idx Image$$EXIDX$$Limit[];
#define __exidx_start   Image$$EXIDX$$Base
#define __exidx_end     Image$$EXIDX$$Limit
#else
extern const struct unwind_idx __exidx_start[];
extern const struct unwind_idx __exidx_end[];
#endif

rt_err_t rt_backtrace_arch_get_current_frame(struct rt_backtrace_frame *frame)
{
    rt_ubase_t current_fp;
    rt_ubase_t current_lr;
    rt_ubase_t caller_fp;
    rt_ubase_t caller_lr;

    if (frame == RT_NULL)
    {
        return -RT_EINVAL;
    }

    __asm volatile ("mov %0, fp" : "=r" (current_fp));
    __asm volatile ("mov %0, lr" : "=r" (current_lr));

    caller_fp = *((rt_ubase_t *)current_fp);
    caller_lr = *((rt_ubase_t *)caller_fp);

    frame->fp = caller_fp;
    frame->sp = current_fp + sizeof(rt_ubase_t);
    frame->lr = caller_lr;
    frame->pc = current_lr;

    frame->valid |= RT_BACKTRACE_FRAME_PC_VALID |
                    RT_BACKTRACE_FRAME_SP_VALID |
                    RT_BACKTRACE_FRAME_FP_VALID |
                    RT_BACKTRACE_FRAME_LR_VALID;

    return RT_EOK;
}

rt_size_t rt_backtrace_arch_unwind(rt_ubase_t *buffer,
                                   rt_size_t size,
                                   const struct rt_backtrace_frame *context)
{
    struct stackframe frame;
    const struct unwind_idx *origin_idx;
    rt_ubase_t stack_high;
    rt_size_t depth;
    int result;

    if ((context->valid & (RT_BACKTRACE_FRAME_PC_VALID |
                           RT_BACKTRACE_FRAME_SP_VALID |
                           RT_BACKTRACE_FRAME_FP_VALID |
                           RT_BACKTRACE_FRAME_LR_VALID |
                           RT_BACKTRACE_FRAME_STACK_VALID)) !=
        (RT_BACKTRACE_FRAME_PC_VALID |
         RT_BACKTRACE_FRAME_SP_VALID |
         RT_BACKTRACE_FRAME_FP_VALID |
         RT_BACKTRACE_FRAME_LR_VALID |
         RT_BACKTRACE_FRAME_STACK_VALID))
    {
        return 0U;
    }

    frame.fp = context->fp;
    frame.sp = context->sp;
    frame.lr = context->lr;
    frame.pc = context->pc;
    stack_high = context->stack_addr + (rt_ubase_t)context->stack_size;
    origin_idx = RT_NULL;
    depth = 0U;

    buffer[depth] = frame.pc;
    depth++;
    while (depth < size)
    {
        result = unwind_frame(&frame,
                              &origin_idx,
                              __exidx_start,
                              __exidx_end,
                              stack_high);
        if (result != URC_OK)
        {
            break;
        }
        buffer[depth] = frame.pc;
        depth++;
    }

    return depth;
}
