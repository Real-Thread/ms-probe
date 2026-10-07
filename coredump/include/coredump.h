/*
 * Copyright (c) 2006-2023 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#ifndef __COREDUMP_H__
#define __COREDUMP_H__

#include "coredump_arch.h"

#define COREDUMP_ADDR       (0x1801E000)
#define COREDUMP_SIZE       (0x1000)

typedef void (*cd_writeout_func_t)(uint8_t *, int);

struct thread_info_ops
{
    int32_t (*get_threads_count)(struct thread_info_ops *);
    int32_t (*get_current_thread_idx)(struct thread_info_ops *);
    void (*get_thread_regset)(struct thread_info_ops *,
                              int32_t,
                              core_regset_type *core_regset,
                              fp_regset_type   *fp_regset);
    int32_t (*get_memarea_count)(struct thread_info_ops *);
    int32_t (*get_memarea)(struct thread_info_ops *, int32_t, uint32_t *, uint32_t *);
    void *priv;
};

void coredump_init(int with_fp, cd_writeout_func_t func);
int32_t coredump_size(void);
void coredump_set_ops(struct thread_info_ops *ops);
int32_t corefile_size(struct thread_info_ops *ops);

core_regset_type *get_cur_core_regset_address(void);
fp_regset_type   *get_cur_fp_regset_address(void);

void coredump_handle(void);

#endif
