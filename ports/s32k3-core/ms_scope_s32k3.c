/*
 * This file is part of ms-probe.
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * Available under GPL-2.0 or a commercial license.
 * See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "rtconfig.h"

#if defined(RT_USING_SCOPE) && defined(RT_SCOPE_S32K3_PORT_AVAILABLE)

#include <rthw.h>

#include "board.h"
#include "core_cm7.h"
#include "Mcu.h"
#include "S32K344.h"
#include "ms_probe.h"

#ifndef RT_SCOPE_EMERGENCY_STACK_SIZE
#define RT_SCOPE_EMERGENCY_STACK_SIZE 2048U
#endif

#define MS_SCOPE_HARDFAULT_VECTOR          3U
#define MS_SCOPE_MEMMANAGE_VECTOR          4U
#define MS_SCOPE_BUSFAULT_VECTOR           5U
#define MS_SCOPE_USAGEFAULT_VECTOR         6U
#define MS_SCOPE_M_CONTEXT_VERSION         1U
#define MS_SCOPE_M_CONTEXT_VALUE_COUNT     33U
#define MS_SCOPE_M_CONTEXT_PAYLOAD_SIZE    \
    (MS_SCOPE_M_CONTEXT_VALUE_COUNT * 4U)
#define MS_SCOPE_M_CONTEXT_SIZE            \
    (MS_PROBE_CONTEXT_HEADER_SIZE + MS_SCOPE_M_CONTEXT_PAYLOAD_SIZE)
#define MS_SCOPE_CONTEXT_FLAG_VALID        0x01U
#define MS_SCOPE_EXCEPTION_FRAME_SIZE      32U
#define MS_SCOPE_FP_FRAME_PREFIX_SIZE      72U
#define MS_SCOPE_XPSR_STACK_ALIGN          0x00000200U
#define MS_SCOPE_EXC_RETURN_USE_PSP        0x00000004U
#define MS_SCOPE_EXC_RETURN_BASIC_FRAME    0x00000010U
#define MS_SCOPE_CFSR_STACK_ERROR_MASK     0x00003030U
#define MS_SCOPE_DTCM_START                0x20000000U
#define MS_SCOPE_DTCM_SIZE                 0x00020000U
#define MS_SCOPE_SRAM_START                0x20400000U
#define MS_SCOPE_SRAM_SIZE                 0x00050000U
#define MS_SCOPE_RESET_TX_SPIN_LIMIT       1000000U
#define MS_SCOPE_LPUART_INTERRUPT_MASK     \
    (LPUART_CTRL_TIE_MASK | LPUART_CTRL_TCIE_MASK | \
     LPUART_CTRL_RIE_MASK | LPUART_CTRL_ILIE_MASK | \
     LPUART_CTRL_ORIE_MASK | LPUART_CTRL_NEIE_MASK | \
     LPUART_CTRL_FEIE_MASK | LPUART_CTRL_PEIE_MASK)
#define MS_SCOPE_LPUART_ERROR_MASK         \
    (LPUART_STAT_OR_MASK | LPUART_STAT_NF_MASK | \
     LPUART_STAT_FE_MASK | LPUART_STAT_PF_MASK)
#define MS_SCOPE_LPUART_STATUS_W1C_MASK    \
    (LPUART_STAT_LBKDIF_MASK | LPUART_STAT_RXEDGIF_MASK | \
     LPUART_STAT_IDLE_MASK | LPUART_STAT_OR_MASK | \
     LPUART_STAT_NF_MASK | LPUART_STAT_FE_MASK | \
     LPUART_STAT_PF_MASK | LPUART_STAT_MA1F_MASK | \
     LPUART_STAT_MA2F_MASK)

#if defined(RT_SCOPE_S32K3_UART3)
#define MS_SCOPE_LPUART                    IP_LPUART_3
#define MS_SCOPE_LPUART_IRQ                LPUART3_IRQn
#else
#define MS_SCOPE_LPUART                    IP_LPUART_0
#define MS_SCOPE_LPUART_IRQ                LPUART0_IRQn
#endif

#if defined(__GNUC__)
#define MS_SCOPE_WORKSPACE(section_name, alignment) \
    __attribute__((section(section_name), aligned(alignment)))
#define MS_SCOPE_NORETURN __attribute__((noreturn))
#else
#define MS_SCOPE_WORKSPACE(section_name, alignment)
#define MS_SCOPE_NORETURN
#endif

struct ms_scope_s32k3_entry_state
{
    ms_probe_u32 msp;
    ms_probe_u32 psp;
    ms_probe_u32 exc_return;
    ms_probe_u32 ipsr;
    ms_probe_u32 r4_r11[8];
    ms_probe_u32 control;
    ms_probe_u32 primask;
    ms_probe_u32 basepri;
    ms_probe_u32 faultmask;
};

typedef char ms_scope_s32k3_entry_layout_check[
    (sizeof(struct ms_scope_s32k3_entry_state) == 64U) ? 1 : -1];

struct ms_scope_s32k3_core_frame
{
    ms_probe_u32 r0;
    ms_probe_u32 r1;
    ms_probe_u32 r2;
    ms_probe_u32 r3;
    ms_probe_u32 r12;
    ms_probe_u32 lr;
    ms_probe_u32 pc;
    ms_probe_u32 xpsr;
};

struct ms_scope_s32k3_port
{
    ms_probe_u32 last_cycles;
    ms_probe_u32 cycle_remainder;
    ms_probe_u32 cycles_per_ms;
    ms_probe_u32 elapsed_ms;
};

ms_probe_u8 ms_scope_s32k3_emergency_stack[RT_SCOPE_EMERGENCY_STACK_SIZE]
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.stack", 8);
struct ms_scope_s32k3_entry_state ms_scope_s32k3_entry_state
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.entry", 8);

static ms_probe_u8 ms_scope_fault_context[MS_SCOPE_M_CONTEXT_SIZE]
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.context", 8);
static struct ms_probe ms_scope_probe
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.probe", 8);
static struct ms_scope_s32k3_port ms_scope_port
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.port", 8);
static volatile ms_probe_u32 ms_scope_fault_active
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.state", 4);
static ms_probe_u32 ms_scope_fault_epoch
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.state", 4);
static int ms_scope_initialized
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.state", 4);

static const struct ms_probe_region ms_scope_regions[] =
{
    {(ms_probe_u32)RT_SCOPE_READ_REGION_START,
     (ms_probe_u32)RT_SCOPE_READ_REGION_SIZE},
    {(ms_probe_u32)RT_SCOPE_SECONDARY_READ_REGION_START,
     (ms_probe_u32)RT_SCOPE_SECONDARY_READ_REGION_SIZE},
    {(ms_probe_u32)RT_SCOPE_TERTIARY_READ_REGION_START,
     (ms_probe_u32)RT_SCOPE_TERTIARY_READ_REGION_SIZE},
    {(ms_probe_u32)RT_SCOPE_S32K3_FOURTH_READ_REGION_START,
     (ms_probe_u32)RT_SCOPE_S32K3_FOURTH_READ_REGION_SIZE},
    {(ms_probe_u32)RT_SCOPE_S32K3_FIFTH_READ_REGION_START,
     (ms_probe_u32)RT_SCOPE_S32K3_FIFTH_READ_REGION_SIZE}
};

extern void ms_scope_s32k3_fault_entry(void);

static void ms_scope_zero(ms_probe_u8 *data, ms_probe_u32 length)
{
    ms_probe_u32 index;

    for (index = 0U; index < length; index++)
    {
        data[index] = 0U;
    }
}

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

static int ms_scope_range_contains(ms_probe_u32 start, ms_probe_u32 size,
                                   ms_probe_u32 address,
                                   ms_probe_u32 length)
{
    ms_probe_u32 end;
    ms_probe_u32 region_end;

    end = address + length;
    region_end = start + size;
    if ((length == 0U) || (end < address) || (region_end < start))
    {
        return 0;
    }
    return ((address >= start) && (end <= region_end)) ? 1 : 0;
}

static int ms_scope_frame_address_valid(ms_probe_u32 address,
                                        ms_probe_u32 length)
{
    if ((address & 0x3U) != 0U)
    {
        return 0;
    }
    if (ms_scope_range_contains(MS_SCOPE_DTCM_START, MS_SCOPE_DTCM_SIZE,
                                address, length) != 0)
    {
        return 1;
    }
    return ms_scope_range_contains(MS_SCOPE_SRAM_START, MS_SCOPE_SRAM_SIZE,
                                   address, length);
}

static void ms_scope_clear_uart_errors(LPUART_Type *base)
{
    ms_probe_u32 errors;
    ms_probe_u32 status;

    status = base->STAT;
    errors = status & MS_SCOPE_LPUART_ERROR_MASK;
    if (errors != 0U)
    {
        base->STAT = (status &
                      (ms_probe_u32)(~MS_SCOPE_LPUART_STATUS_W1C_MASK)) |
                     errors;
    }
}

static int ms_scope_enter_poll_mode(void *user)
{
    struct ms_scope_s32k3_port *port;
    ms_probe_u32 interrupt_mask;
    ms_probe_u32 interrupt_index;

    port = (struct ms_scope_s32k3_port *)user;
    MS_SCOPE_LPUART->CTRL &= (ms_probe_u32)(~MS_SCOPE_LPUART_INTERRUPT_MASK);
    MS_SCOPE_LPUART->CTRL |= LPUART_CTRL_RE_MASK | LPUART_CTRL_TE_MASK;
    MS_SCOPE_LPUART->BAUD &=
        (ms_probe_u32)(~(LPUART_BAUD_RDMAE_MASK | LPUART_BAUD_TDMAE_MASK));

    interrupt_index = ((ms_probe_u32)MS_SCOPE_LPUART_IRQ) >> 5;
    interrupt_mask = 1UL << (((ms_probe_u32)MS_SCOPE_LPUART_IRQ) & 0x1fU);
    NVIC->ICER[interrupt_index] = interrupt_mask;
    NVIC->ICPR[interrupt_index] = interrupt_mask;
    ms_scope_clear_uart_errors(MS_SCOPE_LPUART);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    port->last_cycles = DWT->CYCCNT;
    return 0;
}

static int ms_scope_poll_read(void *user, ms_probe_u8 *data,
                              ms_probe_u16 capacity)
{
    ms_probe_u16 length;
    ms_probe_u32 status;

    (void)user;
    length = 0U;
    while (length < capacity)
    {
        status = MS_SCOPE_LPUART->STAT;
        if ((status & LPUART_STAT_RDRF_MASK) != 0U)
        {
            data[length] = (ms_probe_u8)MS_SCOPE_LPUART->DATA;
            length++;
        }
        else
        {
            if ((status & MS_SCOPE_LPUART_ERROR_MASK) != 0U)
            {
                ms_scope_clear_uart_errors(MS_SCOPE_LPUART);
            }
            break;
        }
    }
    return (int)length;
}

static int ms_scope_poll_write(void *user, const ms_probe_u8 *data,
                               ms_probe_u16 length)
{
    ms_probe_u16 written;

    (void)user;
    written = 0U;
    while ((written < length) &&
           ((MS_SCOPE_LPUART->STAT & LPUART_STAT_TDRE_MASK) != 0U))
    {
        MS_SCOPE_LPUART->DATA = (ms_probe_u32)data[written];
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
    struct ms_scope_s32k3_port *port;
    ms_probe_u32 current;
    ms_probe_u32 delta;
    ms_probe_u32 milliseconds;

    port = (struct ms_scope_s32k3_port *)user;
    current = DWT->CYCCNT;
    delta = current - port->last_cycles;
    port->last_cycles = current;
    port->cycle_remainder += delta;
    milliseconds = port->cycle_remainder / port->cycles_per_ms;
    port->cycle_remainder %= port->cycles_per_ms;
    port->elapsed_ms += milliseconds;
    return port->elapsed_ms;
}

static void ms_scope_reset(void *user)
{
    ms_probe_u32 spin_count;
    ms_probe_u32 value;

    (void)user;
    spin_count = 0U;
    while (((MS_SCOPE_LPUART->STAT & LPUART_STAT_TC_MASK) == 0U) &&
           (spin_count < MS_SCOPE_RESET_TX_SPIN_LIMIT))
    {
        spin_count++;
    }
    value = SCB->AIRCR & SCB_AIRCR_PRIGROUP_Msk;
    value |= (0x5faUL << SCB_AIRCR_VECTKEY_Pos) |
             SCB_AIRCR_SYSRESETREQ_Msk;
    __DSB();
    SCB->AIRCR = value;
    __DSB();
    for (;;)
    {
        __NOP();
    }
}

static int ms_scope_core_frame(
    const struct ms_scope_s32k3_entry_state *entry,
    ms_probe_u32 cfsr,
    const volatile struct ms_scope_s32k3_core_frame **frame,
    ms_probe_u32 *fault_sp)
{
    ms_probe_u32 address;
    ms_probe_u32 prefix_size;
    ms_probe_u32 total_size;

    if ((cfsr & MS_SCOPE_CFSR_STACK_ERROR_MASK) != 0U)
    {
        return 0;
    }
    if ((entry->exc_return & MS_SCOPE_EXC_RETURN_USE_PSP) != 0U)
    {
        address = entry->psp;
    }
    else
    {
        address = entry->msp;
    }
    prefix_size = ((entry->exc_return & MS_SCOPE_EXC_RETURN_BASIC_FRAME) != 0U) ?
                  0U : MS_SCOPE_FP_FRAME_PREFIX_SIZE;
    total_size = prefix_size + MS_SCOPE_EXCEPTION_FRAME_SIZE;
    if (ms_scope_frame_address_valid(address, total_size) == 0)
    {
        return 0;
    }
    *frame = (const volatile struct ms_scope_s32k3_core_frame *)
        (unsigned long)(address + prefix_size);
    *fault_sp = address + total_size;
    if (((*frame)->xpsr & MS_SCOPE_XPSR_STACK_ALIGN) != 0U)
    {
        *fault_sp += 4U;
    }
    return 1;
}

static void ms_scope_capture_context(
    const struct ms_scope_s32k3_entry_state *entry)
{
    const volatile struct ms_scope_s32k3_core_frame *frame;
    ms_probe_u8 *payload;
    ms_probe_u32 values[MS_SCOPE_M_CONTEXT_VALUE_COUNT];
    ms_probe_u32 cfsr;
    ms_probe_u32 fault_sp;
    ms_probe_u32 index;
    int valid;

    ms_scope_zero(ms_scope_fault_context, MS_SCOPE_M_CONTEXT_SIZE);
    ms_scope_zero((ms_probe_u8 *)values, (ms_probe_u32)sizeof(values));
    cfsr = SCB->CFSR;
    fault_sp = 0U;
    frame = NULL;
    valid = ms_scope_core_frame(entry, cfsr, &frame, &fault_sp);

    ms_scope_fault_context[0] = (ms_probe_u8)'M';
    ms_scope_fault_context[1] = (ms_probe_u8)'S';
    ms_scope_fault_context[2] = (ms_probe_u8)'C';
    ms_scope_fault_context[3] = (ms_probe_u8)'T';
    ms_scope_put_u16(&ms_scope_fault_context[4],
                     MS_SCOPE_M_CONTEXT_VERSION);
    ms_scope_fault_context[6] = MS_PROBE_ARCH_ARMV7E_M;
    ms_scope_put_u32(&ms_scope_fault_context[8], MS_SCOPE_M_CONTEXT_SIZE);
    ms_scope_put_u32(&ms_scope_fault_context[12], ms_scope_fault_epoch);
    ms_scope_put_u32(&ms_scope_fault_context[16], entry->ipsr & 0x1ffU);

    if ((valid != 0) && (frame != NULL))
    {
        values[0] = frame->r0;
        values[1] = frame->r1;
        values[2] = frame->r2;
        values[3] = frame->r3;
        for (index = 0U; index < 8U; index++)
        {
            values[4U + index] = entry->r4_r11[index];
        }
        values[12] = frame->r12;
        values[13] = fault_sp;
        values[14] = frame->lr;
        values[15] = frame->pc;
        values[16] = frame->xpsr;
    }
    values[17] = entry->msp;
    values[18] = entry->psp;
    values[19] = entry->control;
    values[20] = entry->primask;
    values[21] = entry->basepri;
    values[22] = entry->faultmask;
    values[23] = entry->exc_return;
    values[24] = cfsr;
    values[25] = SCB->HFSR;
    values[26] = SCB->DFSR;
    values[27] = SCB->MMFAR;
    values[28] = SCB->BFAR;
    values[29] = SCB->AFSR;
    values[30] = SCB->SHCSR;
    values[31] = SCB->ICSR;
    values[32] = SCB->VTOR;

    payload = &ms_scope_fault_context[MS_PROBE_CONTEXT_HEADER_SIZE];
    for (index = 0U; index < MS_SCOPE_M_CONTEXT_VALUE_COUNT; index++)
    {
        ms_scope_put_u32(&payload[index * 4U], values[index]);
    }
    ms_scope_put_u32(&ms_scope_fault_context[20],
                     ms_probe_crc32(payload,
                                    MS_SCOPE_M_CONTEXT_PAYLOAD_SIZE));
    if (valid != 0)
    {
        ms_scope_fault_context[7] = MS_SCOPE_CONTEXT_FLAG_VALID;
    }
    ms_probe_set_context(&ms_scope_probe, ms_scope_fault_context,
                         MS_SCOPE_M_CONTEXT_SIZE,
                         MS_SCOPE_M_CONTEXT_VERSION, valid);
}

static MS_SCOPE_NORETURN void ms_scope_nested_fault(void)
{
    for (;;)
    {
        __NOP();
    }
}

static int ms_scope_install_vectors(void)
{
    volatile ms_probe_u32 *vectors;
    ms_probe_u32 address;
    ms_probe_u32 handler;
    ms_probe_u32 primask;

    address = SCB->VTOR;
    if (ms_scope_range_contains(MS_SCOPE_DTCM_START, MS_SCOPE_DTCM_SIZE,
                                address,
                                (MS_SCOPE_USAGEFAULT_VECTOR + 1U) * 4U) == 0)
    {
        return -1;
    }
    vectors = (volatile ms_probe_u32 *)(unsigned long)address;
    handler = (ms_probe_u32)(unsigned long)&ms_scope_s32k3_fault_entry;
    handler |= 1U;

    primask = __get_PRIMASK();
    __disable_irq();
    vectors[MS_SCOPE_HARDFAULT_VECTOR] = handler;
    vectors[MS_SCOPE_BUSFAULT_VECTOR] = handler;
    vectors[MS_SCOPE_USAGEFAULT_VECTOR] = handler;
#if defined(RT_SCOPE_S32K3_TAKE_MEMMANAGE)
    vectors[MS_SCOPE_MEMMANAGE_VECTOR] = handler;
#endif
    __DSB();
    __ISB();
    if (primask == 0U)
    {
        __enable_irq();
    }
    return 0;
}

int ms_scope_s32k3_probe_init(void)
{
    struct ms_probe_config config;
    ms_probe_u32 core_clock;

    ms_scope_zero((ms_probe_u8 *)&ms_scope_port,
                  (ms_probe_u32)sizeof(ms_scope_port));
    ms_scope_zero(ms_scope_fault_context, MS_SCOPE_M_CONTEXT_SIZE);
    ms_scope_zero((ms_probe_u8 *)&ms_scope_s32k3_entry_state,
                  (ms_probe_u32)sizeof(ms_scope_s32k3_entry_state));
    ms_scope_zero((ms_probe_u8 *)&config,
                  (ms_probe_u32)sizeof(config));
    ms_scope_fault_active = 0U;
    ms_scope_fault_epoch = 0U;
    ms_scope_initialized = 0;

    core_clock = (ms_probe_u32)Mcu_GetClockFrequency(CORE_CLK);
    if (core_clock < 1000U)
    {
        return -1;
    }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    ms_scope_port.last_cycles = DWT->CYCCNT;
    ms_scope_port.cycles_per_ms = core_clock / 1000U;

    config.ops.enter_poll_mode = ms_scope_enter_poll_mode;
    config.ops.poll_read = ms_scope_poll_read;
    config.ops.poll_write = ms_scope_poll_write;
    config.ops.read_memory = ms_scope_read_memory;
    config.ops.now_ms = ms_scope_now_ms;
    config.ops.reset = ms_scope_reset;
    config.user = &ms_scope_port;
    config.regions = ms_scope_regions;
    config.region_count = (ms_probe_u8)(sizeof(ms_scope_regions) /
                                        sizeof(ms_scope_regions[0]));
    config.max_payload = (ms_probe_u16)RT_SCOPE_MAX_PAYLOAD;
    config.max_read_length = (ms_probe_u32)RT_SCOPE_MAX_READ_LENGTH;
    config.frame_timeout_ms = (ms_probe_u32)RT_SCOPE_FRAME_TIMEOUT_MS;
    config.device_id = (ms_probe_u32)RT_SCOPE_DEVICE_ID;
#if defined(RT_USING_SMP) && (RT_CPUS_NR > 1)
    config.capabilities = MS_PROBE_CAP_RTTHREAD_PRESENT;
#else
    config.capabilities = MS_PROBE_CAP_MEMORY_STABLE |
                          MS_PROBE_CAP_RTTHREAD_PRESENT;
#endif
    config.architecture = MS_PROBE_ARCH_ARMV7E_M;
    config.endianness = MS_PROBE_ENDIAN_LITTLE;
    config.address_width = 4U;
    config.probe_version_major = 1U;
    config.probe_version_minor = 0U;

    if (ms_probe_init(&ms_scope_probe, &config) != 0)
    {
        return -1;
    }
    ms_scope_initialized = 1;
    if (ms_scope_install_vectors() != 0)
    {
        ms_scope_initialized = 0;
        return -1;
    }
    return 0;
}

MS_SCOPE_NORETURN void ms_scope_s32k3_fault_main(
    struct ms_scope_s32k3_entry_state *entry)
{
    if ((ms_scope_initialized == 0) || (ms_scope_fault_active != 0U))
    {
        ms_scope_nested_fault();
    }
    ms_scope_fault_active = 1U;
    ms_scope_fault_epoch++;
    ms_scope_capture_context(entry);
    ms_probe_activate(&ms_scope_probe, ms_scope_fault_epoch);
    ms_probe_run_fault(&ms_scope_probe);
    ms_scope_nested_fault();
}

#endif
