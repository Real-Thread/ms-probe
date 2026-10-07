/*
 * Copyright (c) 2026 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include <rtthread.h>
#include <cpuport.h>
#include "ms_probe.h"

#define TEST_UART_BASE   0x10009000U
#define TEST_REG(offset) \
    (*(volatile ms_probe_u32 *)(TEST_UART_BASE + (offset)))
#define TEST_MEMORY_BASE 0x20000000U
#define TEST_MEMORY_SIZE 256U

static struct ms_probe test_probe;
struct ms_probe_qemu_sample
{
    ms_probe_u32 value;
    ms_probe_u32 counter;
    char message[8];
};

struct ms_probe_qemu_sample ms_probe_qemu_sample =
{
    0x12345678U, 7U, "qemu"
};

static struct ms_probe_region test_regions[2] =
{
    {TEST_MEMORY_BASE, TEST_MEMORY_SIZE},
    {0U, 0U}
};
static ms_probe_u32 test_stack[2048];
static volatile int test_armed;

extern void ms_probe_qemu_trigger(void);
extern void ms_probe_qemu_enter_fault(struct ms_probe *probe, void *stack_top);
extern void __real_rt_hw_trap_undef(struct rt_hw_exp_stack *regs);

static int test_enter_poll_mode(void *user)
{
    (void)user;
    TEST_REG(0x38U) = 0U;
    TEST_REG(0x44U) = 0x7ffU;
    return 0;
}

static int test_poll_read(void *user, ms_probe_u8 *data,
                          ms_probe_u16 capacity)
{
    ms_probe_u16 count;

    (void)user;
    count = 0U;
    while ((count < capacity) && ((TEST_REG(0x18U) & 0x10U) == 0U))
    {
        data[count++] = (ms_probe_u8)TEST_REG(0x00U);
    }
    return (int)count;
}

static int test_poll_write(void *user, const ms_probe_u8 *data,
                           ms_probe_u16 length)
{
    (void)user;
    /* Force partial TX even when the emulated UART has spare capacity. */
    if ((length == 0U) || ((TEST_REG(0x18U) & 0x20U) != 0U))
    {
        return 0;
    }
    TEST_REG(0x00U) = data[0];
    return 1;
}

static int test_read_memory(void *user, ms_probe_u32 address,
                            ms_probe_u8 *data, ms_probe_u16 length)
{
    static const ms_probe_u8 prefix[4] = {'-', '>', '\r', '\n'};
    ms_probe_u32 offset;
    ms_probe_u16 index;
    const ms_probe_u8 *sample;

    (void)user;
    if (address >= test_regions[1].start)
    {
        offset = address - test_regions[1].start;
        if ((offset < test_regions[1].size) &&
            ((ms_probe_u32)length <= test_regions[1].size - offset))
        {
            sample = (const ms_probe_u8 *)&ms_probe_qemu_sample;
            for (index = 0U; index < length; index++)
            {
                data[index] = sample[offset + index];
            }
            return 0;
        }
    }
    if (address < TEST_MEMORY_BASE)
    {
        return -1;
    }
    offset = address - TEST_MEMORY_BASE;
    if ((offset >= TEST_MEMORY_SIZE) ||
        ((ms_probe_u32)length > TEST_MEMORY_SIZE - offset))
    {
        return -1;
    }
    for (index = 0U; index < length; index++)
    {
        data[index] = (offset < 4U) ? prefix[offset] : (ms_probe_u8)offset;
        offset++;
    }
    return 0;
}

void __wrap_rt_hw_trap_undef(struct rt_hw_exp_stack *regs)
{
    if ((test_armed == 0) ||
        (regs->pc != (unsigned long)ms_probe_qemu_trigger + 4U))
    {
        __real_rt_hw_trap_undef(regs);
        return;
    }
    ms_probe_activate(&test_probe, 1U);
    ms_probe_qemu_enter_fault(&test_probe, &test_stack[2048]);
}

static void msprobe_test(void)
{
    struct ms_probe_config config = {0};

    config.ops.enter_poll_mode = test_enter_poll_mode;
    config.ops.poll_read = test_poll_read;
    config.ops.poll_write = test_poll_write;
    config.ops.read_memory = test_read_memory;
    test_regions[1].start = (ms_probe_u32)&ms_probe_qemu_sample;
    test_regions[1].size = (ms_probe_u32)sizeof(ms_probe_qemu_sample);
    config.regions = test_regions;
    config.region_count = 2U;
    config.max_payload = 64U;
    config.max_read_length = TEST_MEMORY_SIZE;
    config.device_id = 0xa900U;
    config.address_width = 4U;
    config.endianness = MS_PROBE_ENDIAN_LITTLE;
    config.architecture = MS_PROBE_ARCH_ARMV7_A;
    /* No ARMv7-A context codec is advertised by this protocol-only fixture. */
    if (ms_probe_init(&test_probe, &config) != 0)
    {
        rt_kprintf("ms-probe test initialization failed\n");
        return;
    }
    rt_kprintf("ms-probe test: triggering undefined instruction\n");
    test_armed = 1;
    ms_probe_qemu_trigger();
}
MSH_CMD_EXPORT(msprobe_test, trigger the test-only ms-probe exception);
