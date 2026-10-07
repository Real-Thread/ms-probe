/*
 * This file is part of ms-probe.
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * Available under GPL-2.0 or a commercial license.
 * See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "rtconfig.h"

#if defined(RT_USING_SCOPE) && defined(RT_SCOPE_STM32_PORT_AVAILABLE)

#include "ms_probe.h"
#include "stm32f4xx.h"

#ifndef RT_SCOPE_EMERGENCY_STACK_SIZE
#define RT_SCOPE_EMERGENCY_STACK_SIZE 2048U
#endif

#define MS_SCOPE_VECTOR_COUNT              128U
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
#define MS_SCOPE_CFSR_MSTKERR              0x00000010U
#define MS_SCOPE_CFSR_STKERR               0x00001000U
#define MS_SCOPE_SRAM_START                0x20000000U
#define MS_SCOPE_SRAM_SIZE                 0x00020000U
#define MS_SCOPE_CCMRAM_START              0x10000000U
#define MS_SCOPE_CCMRAM_SIZE               0x00010000U
#define MS_SCOPE_IWDG_RELOAD_KEY           0x0000AAAAU
#define MS_SCOPE_RESET_TX_SPIN_LIMIT       1000000U

#if defined(__GNUC__)
#define MS_SCOPE_WORKSPACE(section_name, alignment) \
    __attribute__((section(section_name), aligned(alignment)))
#define MS_SCOPE_NORETURN __attribute__((noreturn))
#else
#define MS_SCOPE_WORKSPACE(section_name, alignment)
#define MS_SCOPE_NORETURN
#endif

struct ms_scope_stm32_entry_state
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

struct ms_scope_stm32_core_frame
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

struct ms_scope_stm32_port
{
    ms_probe_u32 last_cycles;
    ms_probe_u32 cycle_remainder;
    ms_probe_u32 cycles_per_ms;
    ms_probe_u32 elapsed_ms;
};

ms_probe_u8 ms_scope_stm32_emergency_stack[RT_SCOPE_EMERGENCY_STACK_SIZE]
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.stack", 8);
struct ms_scope_stm32_entry_state ms_scope_stm32_entry_state
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.entry", 8);

static ms_probe_u32 ms_scope_vector_table[MS_SCOPE_VECTOR_COUNT]
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.vector", 512);
static ms_probe_u8 ms_scope_fault_context[MS_SCOPE_M_CONTEXT_SIZE]
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.context", 8);
static struct ms_probe ms_scope_probe
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.probe", 8);
static struct ms_scope_stm32_port ms_scope_port
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
     (ms_probe_u32)RT_SCOPE_TERTIARY_READ_REGION_SIZE}
};

extern void ms_scope_stm32_fault_entry(void);

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
    if (ms_scope_range_contains(MS_SCOPE_SRAM_START, MS_SCOPE_SRAM_SIZE,
                                address, length) != 0)
    {
        return 1;
    }
    return ms_scope_range_contains(MS_SCOPE_CCMRAM_START,
                                   MS_SCOPE_CCMRAM_SIZE,
                                   address, length);
}

static int ms_scope_enter_poll_mode(void *user)
{
    struct ms_scope_stm32_port *port;
    volatile ms_probe_u32 discard;
    ms_probe_u32 status;
    ms_probe_u32 interrupt_mask;

    port = (struct ms_scope_stm32_port *)user;
    USART1->CR1 &= (ms_probe_u32)(~(USART_CR1_RXNEIE |
                                    USART_CR1_TXEIE |
                                    USART_CR1_PEIE));
    USART1->CR1 |= USART_CR1_UE | USART_CR1_RE | USART_CR1_TE;
    USART1->CR3 &= (ms_probe_u32)(~(USART_CR3_EIE |
                                    USART_CR3_DMAR |
                                    USART_CR3_DMAT));
    interrupt_mask = 1UL << ((ms_probe_u32)USART1_IRQn & 0x1fU);
    NVIC->ICER[(ms_probe_u32)USART1_IRQn >> 5] = interrupt_mask;
    NVIC->ICPR[(ms_probe_u32)USART1_IRQn >> 5] = interrupt_mask;

    status = USART1->SR;
    if ((status & USART_SR_ORE) != 0U)
    {
        discard = USART1->DR;
        (void)discard;
    }

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
    ms_probe_u32 value;

    (void)user;
    length = 0U;
    while (length < capacity)
    {
        status = USART1->SR;
        if ((status & USART_SR_RXNE) != 0U)
        {
            value = USART1->DR;
            data[length] = (ms_probe_u8)value;
            length++;
        }
        else
        {
            if ((status & (USART_SR_ORE | USART_SR_NE |
                           USART_SR_FE | USART_SR_PE)) != 0U)
            {
                value = USART1->DR;
                (void)value;
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
    while ((written < length) && ((USART1->SR & USART_SR_TXE) != 0U))
    {
        USART1->DR = (ms_probe_u32)data[written];
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
    struct ms_scope_stm32_port *port;
    ms_probe_u32 current;
    ms_probe_u32 delta;
    ms_probe_u32 milliseconds;

    port = (struct ms_scope_stm32_port *)user;
    current = DWT->CYCCNT;
    delta = current - port->last_cycles;
    port->last_cycles = current;
    port->cycle_remainder += delta;
    milliseconds = port->cycle_remainder / port->cycles_per_ms;
    port->cycle_remainder %= port->cycles_per_ms;
    port->elapsed_ms += milliseconds;
    return port->elapsed_ms;
}

#if defined(RT_SCOPE_STM32_SERVICE_IWDG)
static void ms_scope_service_iwdg(void *user)
{
    (void)user;
    IWDG->KR = MS_SCOPE_IWDG_RELOAD_KEY;
}
#endif

static void ms_scope_reset(void *user)
{
    ms_probe_u32 spin_count;
    ms_probe_u32 value;

    (void)user;
    spin_count = 0U;
    while (((USART1->SR & USART_SR_TC) == 0U) &&
           (spin_count < MS_SCOPE_RESET_TX_SPIN_LIMIT))
    {
#if defined(RT_SCOPE_STM32_SERVICE_IWDG)
        IWDG->KR = MS_SCOPE_IWDG_RELOAD_KEY;
#endif
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

static int ms_scope_core_frame(const struct ms_scope_stm32_entry_state *entry,
                               ms_probe_u32 cfsr,
                               const volatile struct ms_scope_stm32_core_frame **frame,
                               ms_probe_u32 *fault_sp)
{
    ms_probe_u32 address;
    ms_probe_u32 prefix_size;
    ms_probe_u32 total_size;

    if ((cfsr & (MS_SCOPE_CFSR_MSTKERR | MS_SCOPE_CFSR_STKERR)) != 0U)
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
    *frame = (const volatile struct ms_scope_stm32_core_frame *)
        (unsigned long)(address + prefix_size);
    *fault_sp = address + total_size;
    if (((*frame)->xpsr & MS_SCOPE_XPSR_STACK_ALIGN) != 0U)
    {
        *fault_sp += 4U;
    }
    return 1;
}

static void ms_scope_capture_context(
    const struct ms_scope_stm32_entry_state *entry)
{
    const volatile struct ms_scope_stm32_core_frame *frame;
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
#if defined(RT_SCOPE_STM32_SERVICE_IWDG)
        IWDG->KR = MS_SCOPE_IWDG_RELOAD_KEY;
#endif
        __NOP();
    }
}

static void ms_scope_install_vectors(void)
{
    const volatile ms_probe_u32 *source;
    ms_probe_u32 primask;
    ms_probe_u32 index;
    ms_probe_u32 handler;

    primask = __get_PRIMASK();
    __disable_irq();
    source = (const volatile ms_probe_u32 *)(unsigned long)SCB->VTOR;
    for (index = 0U; index < MS_SCOPE_VECTOR_COUNT; index++)
    {
        ms_scope_vector_table[index] = source[index];
    }
    handler = (ms_probe_u32)(unsigned long)&ms_scope_stm32_fault_entry;
    handler |= 1U;
    ms_scope_vector_table[MS_SCOPE_HARDFAULT_VECTOR] = handler;
    ms_scope_vector_table[MS_SCOPE_BUSFAULT_VECTOR] = handler;
    ms_scope_vector_table[MS_SCOPE_USAGEFAULT_VECTOR] = handler;
#if defined(RT_SCOPE_STM32_TAKE_MEMMANAGE)
    ms_scope_vector_table[MS_SCOPE_MEMMANAGE_VECTOR] = handler;
#endif
    __DSB();
    SCB->VTOR = (ms_probe_u32)(unsigned long)ms_scope_vector_table;
    __DSB();
    __ISB();
    if (primask == 0U)
    {
        __enable_irq();
    }
}

int ms_scope_stm32_probe_init(void)
{
    struct ms_probe_config config;

    ms_scope_zero((ms_probe_u8 *)&ms_scope_port,
                  (ms_probe_u32)sizeof(ms_scope_port));
    ms_scope_zero(ms_scope_fault_context, MS_SCOPE_M_CONTEXT_SIZE);
    ms_scope_zero((ms_probe_u8 *)&ms_scope_stm32_entry_state,
                  (ms_probe_u32)sizeof(ms_scope_stm32_entry_state));
    ms_scope_zero((ms_probe_u8 *)&config,
                  (ms_probe_u32)sizeof(config));
    ms_scope_fault_active = 0U;
    ms_scope_fault_epoch = 0U;
    ms_scope_initialized = 0;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    ms_scope_port.last_cycles = DWT->CYCCNT;
    ms_scope_port.cycles_per_ms = SystemCoreClock / 1000U;
    if (ms_scope_port.cycles_per_ms == 0U)
    {
        ms_scope_port.cycles_per_ms = 1U;
    }

    config.ops.enter_poll_mode = ms_scope_enter_poll_mode;
    config.ops.poll_read = ms_scope_poll_read;
    config.ops.poll_write = ms_scope_poll_write;
    config.ops.read_memory = ms_scope_read_memory;
    config.ops.now_ms = ms_scope_now_ms;
#if defined(RT_SCOPE_STM32_SERVICE_IWDG)
    config.ops.service_watchdog = ms_scope_service_iwdg;
#endif
    config.ops.reset = ms_scope_reset;
    config.user = &ms_scope_port;
    config.regions = ms_scope_regions;
    config.region_count = (ms_probe_u8)(sizeof(ms_scope_regions) /
                                        sizeof(ms_scope_regions[0]));
    config.max_payload = (ms_probe_u16)RT_SCOPE_MAX_PAYLOAD;
    config.max_read_length = (ms_probe_u32)RT_SCOPE_MAX_READ_LENGTH;
    config.frame_timeout_ms = (ms_probe_u32)RT_SCOPE_FRAME_TIMEOUT_MS;
    config.device_id = (ms_probe_u32)RT_SCOPE_DEVICE_ID;
    config.capabilities = MS_PROBE_CAP_MEMORY_STABLE |
                          MS_PROBE_CAP_RTTHREAD_PRESENT;
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
    ms_scope_install_vectors();
    return 0;
}

MS_SCOPE_NORETURN void ms_scope_stm32_fault_main(
    struct ms_scope_stm32_entry_state *entry)
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
