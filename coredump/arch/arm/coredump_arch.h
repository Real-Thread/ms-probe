/*
 * Copyright (c) 2006-2023 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#ifndef __ARMV7M_ECD_REGISTERS_DEFINE_H__
#define __ARMV7M_ECD_REGISTERS_DEFINE_H__

#include <stdint.h>

#define ECOREDUMP_ELF_CLASS  ELFCLASS32
#define ECOREDUMP_ELF_ENDIAN ELFDATA2LSB
#define ECOREDUMP_MACHINE    EM_ARM
#define ECOREDUMP_OSABI      ELFOSABI_ARM

#define ECOREDUMP_PRSTATUS_SIZE 148
#define ECOREDUMP_FPREGSET_SIZE (32 * 8 + 4)

#define fill_note_prstatus_desc arm32_fill_note_prstatus_desc
#define fill_note_fpregset_desc arm32_fill_note_fpregset_desc


struct armv7m_core_regset
{
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    uint32_t r12;
    uint32_t sp;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
};

struct arm_vfpv2_regset
{
    uint64_t d0;
    uint64_t d1;
    uint64_t d2;
    uint64_t d3;
    uint64_t d4;
    uint64_t d5;
    uint64_t d6;
    uint64_t d7;
    uint64_t d8;
    uint64_t d9;
    uint64_t d10;
    uint64_t d11;
    uint64_t d12;
    uint64_t d13;
    uint64_t d14;
    uint64_t d15;
    uint32_t fpscr;
};

typedef struct armv7m_core_regset core_regset_type;
typedef struct arm_vfpv2_regset   fp_regset_type;


void arm32_fill_note_prstatus_desc(uint8_t *desc, core_regset_type *regset);
void arm32_fill_note_fpregset_desc(uint8_t *desc, fp_regset_type *regset);

#endif
