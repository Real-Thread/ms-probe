/*
 * Copyright (c) 2006-2023 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "stdint.h"
#include "string.h"
#include "coredump.h"

void arm32_fill_note_prstatus_desc(uint8_t *desc, core_regset_type *regset)
{
    static uint32_t task_id = 3539;
    uint16_t       *signal  = (uint16_t *)&desc[12];
    uint32_t       *lwpid   = (uint32_t *)&desc[24];

    memset(desc, 0, ECOREDUMP_PRSTATUS_SIZE);
    *signal = 0;
    *lwpid  = task_id++;
    memcpy(desc + 72, regset, sizeof(core_regset_type));
}

void arm32_fill_note_fpregset_desc(uint8_t *desc, fp_regset_type *regset)
{
    if (regset != NULL)
    {
        memcpy(desc, regset, sizeof(fp_regset_type) - sizeof(uint32_t));
        memcpy(desc + 32 * 8, &regset->fpscr, sizeof(uint32_t));
    }
    else
    {
        memset(desc, 0, ECOREDUMP_FPREGSET_SIZE);
    }
}
