/*
 * This file is part of ms-probe.
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * Available under GPL-2.0 or a commercial license.
 * See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "rtconfig.h"

#if defined(RT_USING_SCOPE) && defined(RT_SCOPE_KF32_PORT_AVAILABLE)

    .text
    .align  2

    .equ    MS_SCOPE_INT_CTRL0, 0x40200000
    .equ    MS_SCOPE_SYS_MCTL,  0x402000B4

    .global ms_scope_kf32_nmi_entry
    .type ms_scope_kf32_nmi_entry, %function
ms_scope_kf32_nmi_entry:
    MOV     R4, #2
    JMP     ms_scope_kf32_fault_entry

    .global ms_scope_kf32_hardfault_entry
    .type ms_scope_kf32_hardfault_entry, %function
ms_scope_kf32_hardfault_entry:
    MOV     R4, #3
    JMP     ms_scope_kf32_read_recover_or_fault

    .global ms_scope_kf32_sram_ecc_entry
    .type ms_scope_kf32_sram_ecc_entry, %function
ms_scope_kf32_sram_ecc_entry:
    MOV     R4, #107
    JMP     ms_scope_kf32_read_recover_or_fault

    .global ms_scope_kf32_dpram_ecc_entry
    .type ms_scope_kf32_dpram_ecc_entry, %function
ms_scope_kf32_dpram_ecc_entry:
    MOV     R4, #108
    JMP     ms_scope_kf32_read_recover_or_fault

    .global ms_scope_kf32_cache_ecc_entry
    .type ms_scope_kf32_cache_ecc_entry, %function
ms_scope_kf32_cache_ecc_entry:
    MOV     R4, #109
    JMP     ms_scope_kf32_read_recover_or_fault

    .global ms_scope_kf32_flash_ecc_entry
    .type ms_scope_kf32_flash_ecc_entry, %function
ms_scope_kf32_flash_ecc_entry:
    MOV     R4, #110
    JMP     ms_scope_kf32_read_recover_or_fault

    .type ms_scope_kf32_read_recover_or_fault, %function
ms_scope_kf32_read_recover_or_fault:
    LD      R0, #ms_scope_kf32_read_active
    LD.W    R1, [R0]
    CMP     R1, #0
    JZ      ms_scope_kf32_fault_entry
    MOV     R0, MSP
    LD      R1, #ms_scope_kf32_read_fault_resume
    ST.W    [R0+#24], R1
    JMP     LR

    .global ms_scope_kf32_try_read_u8
    .type ms_scope_kf32_try_read_u8, %function
ms_scope_kf32_try_read_u8:
    LD      R2, #ms_scope_kf32_read_active
    MOV     R3, #1
    ST.W    [R2], R3
    SYNC
    LD.B    R2, [R0]
    ST.B    [R1], R2
    LD      R2, #ms_scope_kf32_read_active
    MOV     R3, #0
    ST.W    [R2], R3
    SYNC
    MOV     R0, #0
    JMP     LR

    .type ms_scope_kf32_read_fault_resume, %function
ms_scope_kf32_read_fault_resume:
    LD      R2, #ms_scope_kf32_read_active
    MOV     R3, #0
    ST.W    [R2], R3
    SYNC
    MOV     R0, #1
    JMP     LR

    .global ms_scope_kf32_stackfault_entry
    .type ms_scope_kf32_stackfault_entry, %function
ms_scope_kf32_stackfault_entry:
    MOV     R4, #5
    JMP     ms_scope_kf32_fault_entry

    .global ms_scope_kf32_arifault_entry
    .type ms_scope_kf32_arifault_entry, %function
ms_scope_kf32_arifault_entry:
    MOV     R4, #6
    JMP     ms_scope_kf32_fault_entry

    .global ms_scope_kf32_busfault_entry
    .type ms_scope_kf32_busfault_entry, %function
ms_scope_kf32_busfault_entry:
    MOV     R4, #118
    JMP     ms_scope_kf32_read_recover_or_fault

    .type ms_scope_kf32_fault_entry, %function
ms_scope_kf32_fault_entry:
    LD      R0, #ms_scope_fault_active
    LD.W    R1, [R0]
    CMP     R1, #0
    JNZ     ms_scope_kf32_nested_fault
    MOV     R1, #1
    ST.W    [R0], R1
    SYNC

    LD      R0, #ms_scope_kf32_entry_state
    LD      R1, #MS_SCOPE_INT_CTRL0
    LD.W    R2, [R1]
    ST.W    [R0++], R2
    DSI
    LD      R1, #MS_SCOPE_SYS_MCTL
    LD.W    R2, [R1]
    ST.W    [R0++], R2
    MOV     R1, MSP
    ST.W    [R0++], R1
    MOV     R1, PSP
    ST.W    [R0++], R1
    ST.W    [R0++], LR
    ST.W    [R0++], R4
    ST.W    [R0++], R5
    ST.W    [R0++], R6
    ST.W    [R0++], R7
    ST.W    [R0++], R8
    ST.W    [R0++], R9
    ST.W    [R0++], R10
    ST.W    [R0++], R11
    ST.W    [R0], R12

    LD      R0, #ms_scope_kf32_emergency_stack
    LD      R1, #RT_SCOPE_EMERGENCY_STACK_SIZE
    ADD     R0, R0, R1
    LSR     R0, #3
    LSL     R0, #3
    MOV     MSP, R0
    LD      R0, #ms_scope_kf32_entry_state
    LJMP    ms_scope_kf32_fault_main

    .type ms_scope_kf32_nested_fault, %function
ms_scope_kf32_nested_fault:
    DSI
    SYNC
ms_scope_kf32_nested_fault_loop:
    NOP
    JMP     ms_scope_kf32_nested_fault_loop

#endif
