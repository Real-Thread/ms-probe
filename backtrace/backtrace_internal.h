/*
 * Copyright (c) 2006-2023 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#ifndef __RT_BACKTRACE_INTERNAL_H__
#define __RT_BACKTRACE_INTERNAL_H__

#include "rt_backtrace.h"

/* Architecture ports provide the current processor frame and unwind strategy. */
rt_err_t rt_backtrace_arch_get_current_frame(struct rt_backtrace_frame *frame);
rt_size_t rt_backtrace_arch_unwind(rt_ubase_t *buffer,
                                   rt_size_t size,
                                   const struct rt_backtrace_frame *frame);

#endif /* __RT_BACKTRACE_INTERNAL_H__ */
