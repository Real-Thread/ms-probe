/*
 * This file is part of ms-probe.
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * Available under GPL-2.0 or a commercial license.
 * See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "rtconfig.h"

#if defined(RT_USING_SCOPE) && defined(ARCH_ARM_CORTEX_R52)

#include <rtthread.h>

#include "cpuport.h"
#include "ms_probe.h"

#if defined(__GNUC__) || defined(__clang__)
#define MS_SCOPE_NOINLINE __attribute__((noinline))
#else
#define MS_SCOPE_NOINLINE
#endif

extern int ms_scope_qemu_probe_init(void);
extern void ms_scope_qemu_fault_entry(struct rt_hw_exp_stack *regs,
                                      const char *name,
                                      unsigned int exception_type);
extern void rt_hw_trap_set_hook(
    void (*hook)(struct rt_hw_exp_stack *, const char *, unsigned int));

int ms_scope_rtthread_init(void)
{
    if (ms_scope_qemu_probe_init() != 0)
    {
        return -RT_ERROR;
    }
    rt_hw_trap_set_hook(ms_scope_qemu_fault_entry);
    return RT_EOK;
}

#if defined(RT_SCOPE_USING_FAULT_INJECTION)
static MS_SCOPE_NOINLINE void ms_scope_fault_inject_level3(
    const volatile ms_probe_u32 *address)
{
    volatile ms_probe_u32 value;

    value = *address;
    RT_UNUSED(value);
}

static MS_SCOPE_NOINLINE void ms_scope_fault_inject_level2(
    const volatile ms_probe_u32 *address)
{
    ms_scope_fault_inject_level3(address);
}

static MS_SCOPE_NOINLINE void ms_scope_fault_inject_level1(
    const volatile ms_probe_u32 *address)
{
    ms_scope_fault_inject_level2(address);
}

static void msfault(int argc, char **argv)
{
    const volatile ms_probe_u32 *address;

    if ((argc != 2) || (rt_strcmp(argv[1], "data-abort") != 0))
    {
        rt_kprintf("usage: msfault data-abort\n");
        return;
    }
    if (ms_scope_rtthread_init() != RT_EOK)
    {
        rt_kprintf("msfault: Scope initialization failed\n");
        return;
    }

    address = (const volatile ms_probe_u32 *)(rt_ubase_t)
        RT_SCOPE_FAULT_INJECTION_ADDRESS;
    rt_kprintf("msfault: triggering data abort at 0x%08x\n",
               (unsigned int)RT_SCOPE_FAULT_INJECTION_ADDRESS);
    ms_scope_fault_inject_level1(address);
}
MSH_CMD_EXPORT(msfault, Trigger a Scope data abort for verification.);
#endif

#endif
