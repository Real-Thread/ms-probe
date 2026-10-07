/*
 * This file is part of ms-probe.
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * Available under GPL-2.0 or a commercial license.
 * See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "rtconfig.h"

#if defined(RT_USING_SCOPE) && defined(ARCH_ARM_CORTEX_R52)

    .syntax unified
    .arm
    .text
    .align  2

    .global ms_scope_qemu_fault_entry
    .type   ms_scope_qemu_fault_entry, %function
ms_scope_qemu_fault_entry:
    cpsid   if
    ldr     r3, =ms_scope_qemu_emergency_stack
    ldr     r1, =RT_SCOPE_EMERGENCY_STACK_SIZE
    add     sp, r3, r1
    bic     sp, sp, #7
    mov     r1, r2
    b       ms_scope_qemu_fault_main

    .size   ms_scope_qemu_fault_entry, .-ms_scope_qemu_fault_entry

#endif
