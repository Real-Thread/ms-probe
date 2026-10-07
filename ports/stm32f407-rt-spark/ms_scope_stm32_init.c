/*
 * This file is part of ms-probe.
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * Available under GPL-2.0 or a commercial license.
 * See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "rtconfig.h"

#if defined(RT_USING_SCOPE) && \
    defined(RT_SCOPE_STM32_PORT_AVAILABLE) && \
    defined(RT_SCOPE_USING_FAULT_INJECTION)

#include <rtthread.h>

#if defined(__GNUC__)
#define MS_SCOPE_NOINLINE __attribute__((noinline))
#else
#define MS_SCOPE_NOINLINE
#endif

static MS_SCOPE_NOINLINE void ms_scope_fault_inject(void)
{
    __asm__ volatile("udf #0");
}

static void msfault(int argc, char **argv)
{
    if ((argc != 2) || (rt_strcmp(argv[1], "undefined") != 0))
    {
        rt_kprintf("usage: msfault undefined\n");
        return;
    }

    rt_kprintf("msfault: executing an undefined instruction\n");
    ms_scope_fault_inject();
}
MSH_CMD_EXPORT(msfault, Trigger a Scope Cortex-M Fault for verification.);

#endif
