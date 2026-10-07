/*
 * This file is part of ms-probe.
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * Available under GPL-2.0 or a commercial license.
 * See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "rtconfig.h"

#if defined(RT_USING_SCOPE) && defined(RT_SCOPE_STM32_PORT_AVAILABLE)

    .syntax unified
    .thumb
    .text
    .align  2

    .global ms_scope_stm32_fault_entry
    .type   ms_scope_stm32_fault_entry, %function
    .thumb_func
ms_scope_stm32_fault_entry:
    cpsid   i
    mrs     r0, msp
    mrs     r1, psp
    mov     r2, lr
    mrs     r3, ipsr
    ldr     r12, =ms_scope_stm32_entry_state
    stmia   r12!, {r0-r3}
    stmia   r12!, {r4-r11}
    mrs     r0, control
    mrs     r1, primask
    mrs     r2, basepri
    mrs     r3, faultmask
    stmia   r12, {r0-r3}
    ldr     r0, =ms_scope_stm32_emergency_stack
    ldr     r1, =RT_SCOPE_EMERGENCY_STACK_SIZE
    add     r0, r0, r1
    bic     r0, r0, #7
    msr     msp, r0
    ldr     r0, =ms_scope_stm32_entry_state
    b       ms_scope_stm32_fault_main

    .size   ms_scope_stm32_fault_entry, .-ms_scope_stm32_fault_entry

#endif
