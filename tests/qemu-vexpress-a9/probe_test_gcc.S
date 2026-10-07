/*
 * Copyright (c) 2026 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

    .syntax unified
    .arm
    .text
    .align 2
    .global ms_probe_qemu_trigger
    .type ms_probe_qemu_trigger, %function
ms_probe_qemu_trigger:
    udf #0
    bx lr
    .size ms_probe_qemu_trigger, . - ms_probe_qemu_trigger

    .global ms_probe_qemu_enter_fault
    .type ms_probe_qemu_enter_fault, %function
ms_probe_qemu_enter_fault:
    cpsid if
    mov sp, r1
    bic sp, sp, #7
    bl ms_probe_run_fault
1:
    b 1b
    .size ms_probe_qemu_enter_fault, . - ms_probe_qemu_enter_fault
    .section .note.GNU-stack,"",%progbits
