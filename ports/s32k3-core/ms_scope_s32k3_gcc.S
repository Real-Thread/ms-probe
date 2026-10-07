/*
 * This file is part of ms-probe.
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * Available under GPL-2.0 or a commercial license.
 * See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "rtconfig.h"

#if defined(RT_USING_SCOPE) && defined(RT_SCOPE_S32K3_PORT_AVAILABLE)

    .syntax unified
    .thumb
    .text
    .align  2

    .global ms_scope_s32k3_fault_entry
    .type   ms_scope_s32k3_fault_entry, %function
    .thumb_func
ms_scope_s32k3_fault_entry:
    mrs     r0, primask
    cpsid   i
    ldr     r1, =ms_scope_s32k3_entry_state
    str     r0, [r1, #52]
    mrs     r0, msp
    mrs     r1, psp
    mov     r2, lr
    mrs     r3, ipsr
    ldr     r12, =ms_scope_s32k3_entry_state
    stmia   r12!, {r0-r3}
    stmia   r12!, {r4-r11}
    mrs     r0, control
    str     r0, [r12, #0]
    mrs     r0, basepri
    str     r0, [r12, #8]
    mrs     r0, faultmask
    str     r0, [r12, #12]
    ldr     r0, =ms_scope_s32k3_emergency_stack
    ldr     r1, =RT_SCOPE_EMERGENCY_STACK_SIZE
    add     r0, r0, r1
    bic     r0, r0, #7
    msr     msp, r0
    ldr     r0, =ms_scope_s32k3_entry_state
    b       ms_scope_s32k3_fault_main

    .size   ms_scope_s32k3_fault_entry, .-ms_scope_s32k3_fault_entry

#endif
