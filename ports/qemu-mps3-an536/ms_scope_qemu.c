/*
 * This file is part of ms-probe.
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * Available under GPL-2.0 or a commercial license.
 * See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "rtconfig.h"

#if defined(RT_USING_SCOPE) && defined(ARCH_ARM_CORTEX_R52)

#include "ms_probe.h"

#ifndef RT_SCOPE_EMERGENCY_STACK_SIZE
#define RT_SCOPE_EMERGENCY_STACK_SIZE 1024U
#endif

#define MS_SCOPE_UART0_BASE             0xe0205000U
#define MS_SCOPE_UART_STATE_TXFULL      0x00000001U
#define MS_SCOPE_UART_STATE_RXFULL      0x00000002U
#define MS_SCOPE_UART_STATE_TXOVERRUN   0x00000004U
#define MS_SCOPE_UART_STATE_RXOVERRUN   0x00000008U
#define MS_SCOPE_UART_STATE_OVERRUN     \
    (MS_SCOPE_UART_STATE_TXOVERRUN | MS_SCOPE_UART_STATE_RXOVERRUN)
#define MS_SCOPE_UART_CTRL_TXEN         0x00000001U
#define MS_SCOPE_UART_CTRL_RXEN         0x00000002U
#define MS_SCOPE_UART_CTRL_TXIRQEN      0x00000004U
#define MS_SCOPE_UART_CTRL_RXIRQEN      0x00000008U
#define MS_SCOPE_R52_CONTEXT_VERSION    1U
#define MS_SCOPE_R52_CONTEXT_SIZE       108U
#define MS_SCOPE_CONTEXT_FLAG_VALID     0x01U
#define MS_SCOPE_CONTEXT_PAYLOAD_SIZE   84U

struct ms_scope_uart
{
    volatile ms_probe_u32 data;
    volatile ms_probe_u32 state;
    volatile ms_probe_u32 control;
    volatile ms_probe_u32 interrupt;
    volatile ms_probe_u32 baud_divisor;
};

struct rt_hw_exp_stack
{
    unsigned long r0;
    unsigned long r1;
    unsigned long r2;
    unsigned long r3;
    unsigned long r4;
    unsigned long r5;
    unsigned long r6;
    unsigned long r7;
    unsigned long r8;
    unsigned long r9;
    unsigned long r10;
    unsigned long fp;
    unsigned long ip;
    unsigned long sp;
    unsigned long lr;
    unsigned long pc;
    unsigned long cpsr;
};

struct ms_scope_qemu_port
{
    struct ms_scope_uart *uart;
    unsigned long long timer_last_count;
    unsigned long long timer_remainder;
    ms_probe_u32 timer_counts_per_ms;
    ms_probe_u32 elapsed_ms;
};

#if defined(__GNUC__) || defined(__clang__)
#define MS_SCOPE_WORKSPACE \
    __attribute__((section(".ms_scope_workspace"), aligned(8)))
ms_probe_u8 ms_scope_qemu_emergency_stack[RT_SCOPE_EMERGENCY_STACK_SIZE]
    MS_SCOPE_WORKSPACE;
static ms_probe_u8 ms_scope_fault_context[MS_SCOPE_R52_CONTEXT_SIZE]
    MS_SCOPE_WORKSPACE;
#else
#define MS_SCOPE_WORKSPACE
ms_probe_u8 ms_scope_qemu_emergency_stack[RT_SCOPE_EMERGENCY_STACK_SIZE];
static ms_probe_u8 ms_scope_fault_context[MS_SCOPE_R52_CONTEXT_SIZE];
#endif

static struct ms_probe ms_scope_probe MS_SCOPE_WORKSPACE;
static struct ms_probe_region ms_scope_regions[3] MS_SCOPE_WORKSPACE;
static struct ms_scope_qemu_port ms_scope_port MS_SCOPE_WORKSPACE;
static volatile ms_probe_u32 ms_scope_fault_active MS_SCOPE_WORKSPACE;
static ms_probe_u32 ms_scope_fault_epoch;
static int ms_scope_initialized;

static void ms_scope_put_u16(ms_probe_u8 *data, ms_probe_u16 value)
{
    data[0] = (ms_probe_u8)value;
    data[1] = (ms_probe_u8)(value >> 8);
}

static void ms_scope_put_u32(ms_probe_u8 *data, ms_probe_u32 value)
{
    data[0] = (ms_probe_u8)value;
    data[1] = (ms_probe_u8)(value >> 8);
    data[2] = (ms_probe_u8)(value >> 16);
    data[3] = (ms_probe_u8)(value >> 24);
}

static void ms_scope_zero(ms_probe_u8 *data, ms_probe_u32 length)
{
    ms_probe_u32 index;

    for (index = 0U; index < length; index++)
    {
        data[index] = 0U;
    }
}

static ms_probe_u32 ms_scope_read_dfsr(void)
{
    ms_probe_u32 value;

    __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(value));
    return value;
}

static ms_probe_u32 ms_scope_read_dfar(void)
{
    ms_probe_u32 value;

    __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(value));
    return value;
}

static ms_probe_u32 ms_scope_read_ifsr(void)
{
    ms_probe_u32 value;

    __asm__ volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(value));
    return value;
}

static ms_probe_u32 ms_scope_read_ifar(void)
{
    ms_probe_u32 value;

    __asm__ volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(value));
    return value;
}

static ms_probe_u32 ms_scope_read_timer_frequency(void)
{
    ms_probe_u32 value;

    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(value));
    return value;
}

static unsigned long long ms_scope_read_timer_count(void)
{
    unsigned long long value;

    __asm__ volatile("mrrc p15, 0, %Q0, %R0, c14" : "=r"(value));
    return value;
}

static int ms_scope_enter_poll_mode(void *user)
{
    struct ms_scope_qemu_port *port;
    ms_probe_u32 control;
    ms_probe_u32 state;

    port = (struct ms_scope_qemu_port *)user;
    control = port->uart->control;
    control &= (ms_probe_u32)(~(MS_SCOPE_UART_CTRL_RXIRQEN |
                                MS_SCOPE_UART_CTRL_TXIRQEN));
    control |= MS_SCOPE_UART_CTRL_RXEN | MS_SCOPE_UART_CTRL_TXEN;
    port->uart->control = control;
    state = port->uart->state;
    if ((state & MS_SCOPE_UART_STATE_OVERRUN) != 0U)
    {
        port->uart->state = state & MS_SCOPE_UART_STATE_OVERRUN;
    }
    return 0;
}

static int ms_scope_poll_read(void *user, ms_probe_u8 *data,
                              ms_probe_u16 capacity)
{
    struct ms_scope_qemu_port *port;
    ms_probe_u16 length;
    ms_probe_u32 state;

    port = (struct ms_scope_qemu_port *)user;
    length = 0U;
    while (length < capacity)
    {
        state = port->uart->state;
        if ((state & MS_SCOPE_UART_STATE_RXOVERRUN) != 0U)
        {
            port->uart->state = MS_SCOPE_UART_STATE_RXOVERRUN;
        }
        if ((state & MS_SCOPE_UART_STATE_RXFULL) == 0U)
        {
            break;
        }
        data[length] = (ms_probe_u8)port->uart->data;
        length++;
    }
    return (int)length;
}

static int ms_scope_poll_write(void *user, const ms_probe_u8 *data,
                               ms_probe_u16 length)
{
    struct ms_scope_qemu_port *port;
    ms_probe_u16 written;
    ms_probe_u32 state;

    port = (struct ms_scope_qemu_port *)user;
    written = 0U;
    while (written < length)
    {
        state = port->uart->state;
        if ((state & MS_SCOPE_UART_STATE_TXOVERRUN) != 0U)
        {
            port->uart->state = MS_SCOPE_UART_STATE_TXOVERRUN;
        }
        if ((state & MS_SCOPE_UART_STATE_TXFULL) != 0U)
        {
            break;
        }
        port->uart->data = data[written];
        written++;
    }
    return (int)written;
}

static int ms_scope_read_memory(void *user, ms_probe_u32 address,
                                ms_probe_u8 *data, ms_probe_u16 length)
{
    const volatile ms_probe_u8 *source;
    ms_probe_u16 index;

    (void)user;
    source = (const volatile ms_probe_u8 *)(unsigned long)address;
    for (index = 0U; index < length; index++)
    {
        data[index] = source[index];
    }
    return 0;
}

static ms_probe_u32 ms_scope_now_ms(void *user)
{
    struct ms_scope_qemu_port *port;
    unsigned long long current;
    unsigned long long delta;
    unsigned long long milliseconds;

    port = (struct ms_scope_qemu_port *)user;
    current = ms_scope_read_timer_count();
    delta = current - port->timer_last_count;
    port->timer_last_count = current;
    port->timer_remainder += delta;
    milliseconds = port->timer_remainder / port->timer_counts_per_ms;
    port->timer_remainder %= port->timer_counts_per_ms;
    port->elapsed_ms += (ms_probe_u32)milliseconds;
    return port->elapsed_ms;
}

static void ms_scope_reset(void *user)
{
    ms_probe_u32 value;

    (void)user;
    __asm__ volatile("mrc p15, 0, %0, c12, c0, 2" : "=r"(value));
    value |= 0x00000002U;
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("mcr p15, 0, %0, c12, c0, 2" :: "r"(value) : "memory");
    __asm__ volatile("isb" ::: "memory");
}

static void ms_scope_capture_context(const struct rt_hw_exp_stack *regs,
                                     ms_probe_u32 cause)
{
    ms_probe_u8 *payload;
    ms_probe_u32 values[21];
    ms_probe_u32 index;

    ms_scope_zero(ms_scope_fault_context, MS_SCOPE_R52_CONTEXT_SIZE);
    ms_scope_zero((ms_probe_u8 *)values, (ms_probe_u32)sizeof(values));
    ms_scope_fault_context[0] = (ms_probe_u8)'M';
    ms_scope_fault_context[1] = (ms_probe_u8)'S';
    ms_scope_fault_context[2] = (ms_probe_u8)'C';
    ms_scope_fault_context[3] = (ms_probe_u8)'T';
    ms_scope_put_u16(&ms_scope_fault_context[4],
                     MS_SCOPE_R52_CONTEXT_VERSION);
    ms_scope_fault_context[6] = MS_PROBE_ARCH_ARMV8_R_AARCH32;
    ms_scope_put_u32(&ms_scope_fault_context[8],
                     MS_SCOPE_R52_CONTEXT_SIZE);
    ms_scope_put_u32(&ms_scope_fault_context[12], ms_scope_fault_epoch);
    ms_scope_put_u32(&ms_scope_fault_context[16], cause);

    if (regs != NULL)
    {
        values[0] = (ms_probe_u32)regs->r0;
        values[1] = (ms_probe_u32)regs->r1;
        values[2] = (ms_probe_u32)regs->r2;
        values[3] = (ms_probe_u32)regs->r3;
        values[4] = (ms_probe_u32)regs->r4;
        values[5] = (ms_probe_u32)regs->r5;
        values[6] = (ms_probe_u32)regs->r6;
        values[7] = (ms_probe_u32)regs->r7;
        values[8] = (ms_probe_u32)regs->r8;
        values[9] = (ms_probe_u32)regs->r9;
        values[10] = (ms_probe_u32)regs->r10;
        values[11] = (ms_probe_u32)regs->fp;
        values[12] = (ms_probe_u32)regs->ip;
        values[13] = (ms_probe_u32)regs->sp;
        values[14] = (ms_probe_u32)regs->lr;
        values[15] = (ms_probe_u32)regs->pc;
        values[16] = (ms_probe_u32)regs->cpsr;
    }
    values[17] = ms_scope_read_dfsr();
    values[18] = ms_scope_read_dfar();
    values[19] = ms_scope_read_ifsr();
    values[20] = ms_scope_read_ifar();

    payload = &ms_scope_fault_context[MS_PROBE_CONTEXT_HEADER_SIZE];
    for (index = 0U; index < 21U; index++)
    {
        ms_scope_put_u32(&payload[index * 4U], values[index]);
    }
    ms_scope_put_u32(&ms_scope_fault_context[20],
                     ms_probe_crc32(payload,
                                    MS_SCOPE_CONTEXT_PAYLOAD_SIZE));
    ms_scope_fault_context[7] = MS_SCOPE_CONTEXT_FLAG_VALID;
    ms_probe_set_context(&ms_scope_probe, ms_scope_fault_context,
                         MS_SCOPE_R52_CONTEXT_SIZE,
                         MS_SCOPE_R52_CONTEXT_VERSION, 1);
}

static void ms_scope_nested_fault(void)
{
    for (;;)
    {
        if ((ms_scope_initialized != 0) &&
            (ms_scope_probe.config.ops.service_watchdog != NULL))
        {
            ms_scope_probe.config.ops.service_watchdog(
                ms_scope_probe.config.user);
        }
    }
}

int ms_scope_qemu_probe_init(void)
{
    struct ms_probe_config config;
    ms_probe_u32 timer_frequency;

    if (ms_scope_initialized != 0)
    {
        return 0;
    }

    ms_scope_zero((ms_probe_u8 *)&ms_scope_port,
                  (ms_probe_u32)sizeof(ms_scope_port));
    ms_scope_zero((ms_probe_u8 *)ms_scope_regions,
                  (ms_probe_u32)sizeof(ms_scope_regions));
    ms_scope_zero(ms_scope_fault_context,
                  MS_SCOPE_R52_CONTEXT_SIZE);
    ms_scope_fault_active = 0U;
    ms_scope_fault_epoch = 0U;
    ms_scope_zero((ms_probe_u8 *)&config,
                  (ms_probe_u32)sizeof(config));
    ms_scope_port.uart =
        (struct ms_scope_uart *)(unsigned long)MS_SCOPE_UART0_BASE;
    timer_frequency = ms_scope_read_timer_frequency();
    ms_scope_port.timer_counts_per_ms = timer_frequency / 1000U;
    if (ms_scope_port.timer_counts_per_ms == 0U)
    {
        ms_scope_port.timer_counts_per_ms = 1U;
    }
    ms_scope_port.timer_last_count = ms_scope_read_timer_count();

    config.ops.enter_poll_mode = ms_scope_enter_poll_mode;
    config.ops.poll_read = ms_scope_poll_read;
    config.ops.poll_write = ms_scope_poll_write;
    config.ops.read_memory = ms_scope_read_memory;
    config.ops.now_ms = ms_scope_now_ms;
    config.ops.reset = ms_scope_reset;
    config.user = &ms_scope_port;
    config.region_count = 0U;
    if ((ms_probe_u32)RT_SCOPE_READ_REGION_SIZE > 0U)
    {
        ms_scope_regions[config.region_count].start =
            (ms_probe_u32)RT_SCOPE_READ_REGION_START;
        ms_scope_regions[config.region_count].size =
            (ms_probe_u32)RT_SCOPE_READ_REGION_SIZE;
        config.region_count++;
    }
    if ((ms_probe_u32)RT_SCOPE_SECONDARY_READ_REGION_SIZE > 0U)
    {
        ms_scope_regions[config.region_count].start =
            (ms_probe_u32)RT_SCOPE_SECONDARY_READ_REGION_START;
        ms_scope_regions[config.region_count].size =
            (ms_probe_u32)RT_SCOPE_SECONDARY_READ_REGION_SIZE;
        config.region_count++;
    }
    if ((ms_probe_u32)RT_SCOPE_TERTIARY_READ_REGION_SIZE > 0U)
    {
        ms_scope_regions[config.region_count].start =
            (ms_probe_u32)RT_SCOPE_TERTIARY_READ_REGION_START;
        ms_scope_regions[config.region_count].size =
            (ms_probe_u32)RT_SCOPE_TERTIARY_READ_REGION_SIZE;
        config.region_count++;
    }
    config.regions = ms_scope_regions;
    config.max_payload = (ms_probe_u16)RT_SCOPE_MAX_PAYLOAD;
    config.max_read_length = (ms_probe_u32)RT_SCOPE_MAX_READ_LENGTH;
    config.frame_timeout_ms = (ms_probe_u32)RT_SCOPE_FRAME_TIMEOUT_MS;
    config.device_id = (ms_probe_u32)RT_SCOPE_DEVICE_ID;
    config.capabilities = MS_PROBE_CAP_MEMORY_STABLE |
                          MS_PROBE_CAP_RTTHREAD_PRESENT;
    config.architecture = MS_PROBE_ARCH_ARMV8_R_AARCH32;
    config.endianness = MS_PROBE_ENDIAN_LITTLE;
    config.address_width = 4U;
    config.probe_version_major = 1U;
    config.probe_version_minor = 0U;

    if (ms_probe_init(&ms_scope_probe, &config) != 0)
    {
        return -1;
    }
    ms_scope_initialized = 1;
    return 0;
}

void ms_scope_qemu_fault_main(struct rt_hw_exp_stack *regs,
                              unsigned int exception_type)
{
    if ((ms_scope_initialized == 0) || (ms_scope_fault_active != 0U))
    {
        ms_scope_nested_fault();
    }
    ms_scope_fault_active = 1U;
    ms_scope_fault_epoch++;
    ms_scope_capture_context(regs, (ms_probe_u32)exception_type);
    ms_probe_activate(&ms_scope_probe, ms_scope_fault_epoch);
    ms_probe_run_fault(&ms_scope_probe);
}

#endif
