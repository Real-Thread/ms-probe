/*
 * Copyright (c) 2006-2026 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "rtconfig.h"

#if defined(RT_USING_SCOPE) && defined(RT_SCOPE_USING_FAULT_INJECTION)

#include <rtthread.h>

#include "kf32a1x8_reg_intr.h"

static void fault_test(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    (void)rt_kprintf("fault test: triggering HardFault; reset is required\n");
    INTR_PTR->EIF0.bits.HARDFAULTIF = 1U;
}
MSH_CMD_EXPORT(fault_test, Trigger a HardFault exception.);

#endif
