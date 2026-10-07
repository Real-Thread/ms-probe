/*
 * Copyright (c) 2006-2023 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

    .section .text.rt_backtrace_arch_get_sp, "ax"
    .align 2
    .global rt_backtrace_arch_get_sp
    .type rt_backtrace_arch_get_sp, %function

rt_backtrace_arch_get_sp:
    MOV     R0, SP
    JMP     LR

    .size rt_backtrace_arch_get_sp, . - rt_backtrace_arch_get_sp
