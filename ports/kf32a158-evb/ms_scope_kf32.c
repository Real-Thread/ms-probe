/*
 * This file is part of ms-probe.
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * Available under GPL-2.0 or a commercial license.
 * See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "rtconfig.h"

#if defined(RT_USING_SCOPE) && defined(RT_SCOPE_KF32_PORT_AVAILABLE)

#include <rthw.h>

#include "kf32a1x8_reg_dma.h"
#include "kf32a1x8_reg_gptimer.h"
#include "kf32a1x8_reg_intr.h"
#include "kf32a1x8_reg_mpu.h"
#include "kf32a1x8_reg_osc.h"
#include "kf32a1x8_reg_pm.h"
#include "kf32a1x8_reg_system.h"
#include "kf32a1x8_reg_usart.h"
#include "kf32a1x8_reg_wdt.h"
#include "ms_probe.h"

#if !defined(RT_CPUS_NR)
#error "KF32 Scope requires RT_CPUS_NR"
#elif (RT_CPUS_NR != 1)
#error "KF32 Scope supports one core until multi-core Fault coordination is implemented"
#endif

#ifndef RT_SCOPE_EMERGENCY_STACK_SIZE
#define RT_SCOPE_EMERGENCY_STACK_SIZE 2048U
#endif

#define MS_SCOPE_VECTOR_COUNT              128U
#define MS_SCOPE_NMI_VECTOR                2U
#define MS_SCOPE_HARDFAULT_VECTOR          3U
#define MS_SCOPE_STACKFAULT_VECTOR         5U
#define MS_SCOPE_ARIFAULT_VECTOR           6U
#define MS_SCOPE_SRAM_ECC_VECTOR           107U
#define MS_SCOPE_DPRAM_ECC_VECTOR          108U
#define MS_SCOPE_CACHE_ECC_VECTOR          109U
#define MS_SCOPE_FLASH_ECC_VECTOR          110U
#define MS_SCOPE_BUSFAULT_VECTOR           118U
#define MS_SCOPE_KF32_CONTEXT_VERSION      1U
#define MS_SCOPE_KF32_CONTEXT_VALUE_COUNT  27U
#define MS_SCOPE_KF32_CONTEXT_PAYLOAD_SIZE \
    (MS_SCOPE_KF32_CONTEXT_VALUE_COUNT * 4U)
#define MS_SCOPE_KF32_CONTEXT_SIZE         \
    (MS_PROBE_CONTEXT_HEADER_SIZE + MS_SCOPE_KF32_CONTEXT_PAYLOAD_SIZE)
#define MS_SCOPE_CONTEXT_FLAG_VALID        0x01U
#define MS_SCOPE_EXCEPTION_FRAME_SIZE      32U
#define MS_SCOPE_EXC_RETURN_USE_PSP        0x00000004U
#define MS_SCOPE_RAM_LOW_START             0x10000000U
#define MS_SCOPE_RAM_LOW_SIZE              0x0002E000U
#define MS_SCOPE_RAM_HIGH_START            0x10030000U
#define MS_SCOPE_RAM_HIGH_SIZE             0x00010000U
#define MS_SCOPE_FLASH_START               0x00000000U
#define MS_SCOPE_FLASH_SIZE                0x00200000U
#define MS_SCOPE_RESET_TX_SPIN_LIMIT       1000000U
#define MS_SCOPE_SYSTEM_RESET_VALUE        0x05FA0004U
#define MS_SCOPE_DMA_CHANNEL_COUNT         7U
#define MS_SCOPE_INTHF_UART_BRGR            0x00C10008U
#define MS_SCOPE_INTHF_TIMER_PRESCALER     1599U
#define MS_SCOPE_TIMER_PERIOD              10U

#if defined(__GNUC__)
#define MS_SCOPE_WORKSPACE(section_name, alignment) \
    __attribute__((section(section_name), aligned(alignment)))
#define MS_SCOPE_NORETURN __attribute__((noreturn))
#else
#define MS_SCOPE_WORKSPACE(section_name, alignment)
#define MS_SCOPE_NORETURN
#endif

struct ms_scope_kf32_entry_state
{
    ms_probe_u32 intr_ctrl0;
    ms_probe_u32 mctl;
    ms_probe_u32 msp;
    ms_probe_u32 psp;
    ms_probe_u32 exc_return;
    ms_probe_u32 cause;
    ms_probe_u32 r5_r12[8];
};

typedef char ms_scope_kf32_entry_layout_check[
    (sizeof(struct ms_scope_kf32_entry_state) == 56U) ? 1 : -1];

struct ms_scope_kf32_core_frame
{
    ms_probe_u32 r0;
    ms_probe_u32 r1;
    ms_probe_u32 r2;
    ms_probe_u32 r3;
    ms_probe_u32 r4;
    ms_probe_u32 lr;
    ms_probe_u32 pc;
    ms_probe_u32 xpsr;
};

typedef char ms_scope_kf32_frame_layout_check[
    (sizeof(struct ms_scope_kf32_core_frame) ==
     MS_SCOPE_EXCEPTION_FRAME_SIZE) ? 1 : -1];

struct ms_scope_kf32_port
{
    ms_probe_u32 elapsed_ms;
    ms_probe_u32 fault_cause;
};

ms_probe_u8 ms_scope_kf32_emergency_stack[RT_SCOPE_EMERGENCY_STACK_SIZE]
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.stack", 8);
struct ms_scope_kf32_entry_state ms_scope_kf32_entry_state
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.entry", 8);

static ms_probe_u32 ms_scope_vector_table[MS_SCOPE_VECTOR_COUNT]
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.vector", 512);
static ms_probe_u8 ms_scope_fault_context[MS_SCOPE_KF32_CONTEXT_SIZE]
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.context", 8);
static struct ms_probe ms_scope_probe
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.probe", 8);
static struct ms_scope_kf32_port ms_scope_port
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.port", 8);
volatile ms_probe_u32 ms_scope_fault_active
    MS_SCOPE_WORKSPACE(".ms_scope_workspace.state", 4);
volatile ms_probe_u32 ms_scope_kf32_read_active
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

extern void ms_scope_kf32_nmi_entry(void);
extern void ms_scope_kf32_hardfault_entry(void);
extern void ms_scope_kf32_stackfault_entry(void);
extern void ms_scope_kf32_arifault_entry(void);
extern void ms_scope_kf32_sram_ecc_entry(void);
extern void ms_scope_kf32_dpram_ecc_entry(void);
extern void ms_scope_kf32_cache_ecc_entry(void);
extern void ms_scope_kf32_flash_ecc_entry(void);
extern void ms_scope_kf32_busfault_entry(void);
extern int ms_scope_kf32_try_read_u8(const volatile ms_probe_u8 *source,
                                    ms_probe_u8 *destination);
extern const ms_probe_u32 _start;

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

static int ms_scope_ram_address_valid(ms_probe_u32 address,
                                      ms_probe_u32 length)
{
    if ((address & 0x3U) != 0U)
    {
        return 0;
    }
    if (ms_scope_range_contains(MS_SCOPE_RAM_LOW_START,
                                MS_SCOPE_RAM_LOW_SIZE,
                                address, length) != 0)
    {
        return 1;
    }
    return ms_scope_range_contains(MS_SCOPE_RAM_HIGH_START,
                                   MS_SCOPE_RAM_HIGH_SIZE,
                                   address, length);
}

static int ms_scope_code_address_valid(ms_probe_u32 address)
{
    if (ms_scope_range_contains(MS_SCOPE_FLASH_START, MS_SCOPE_FLASH_SIZE,
                                address, 2U) != 0)
    {
        return 1;
    }
    if (ms_scope_range_contains(MS_SCOPE_RAM_LOW_START,
                                MS_SCOPE_RAM_LOW_SIZE,
                                address, 2U) != 0)
    {
        return 1;
    }
    return ms_scope_range_contains(MS_SCOPE_RAM_HIGH_START,
                                   MS_SCOPE_RAM_HIGH_SIZE,
                                   address, 2U);
}

static void ms_scope_clear_uart_errors(void)
{
    USART0_PTR->STR.bits.OVFEIC = 1U;
    USART0_PTR->STR.bits.PAREIC = 1U;
    USART0_PTR->STR.bits.FREIC = 1U;
    USART0_PTR->STR.bits.TEIC = 1U;
    USART0_PTR->STR.bits.REIC = 1U;
    USART0_PTR->STR.bits.OVFEIC = 0U;
    USART0_PTR->STR.bits.PAREIC = 0U;
    USART0_PTR->STR.bits.FREIC = 0U;
    USART0_PTR->STR.bits.TEIC = 0U;
    USART0_PTR->STR.bits.REIC = 0U;
}

static void ms_scope_clear_timer_flag(void)
{
    T23_PTR->CCP_SRIC.bits.TXIC = 1U;
    __asm__ volatile("SYNC");
    T23_PTR->CCP_SRIC.bits.TXIC = 0U;
}

static int ms_scope_stop_dma(void)
{
    ms_probe_u32 index;

    for (index = 0U; index < MS_SCOPE_DMA_CHANNEL_COUNT; index++)
    {
        DMA0_PTR->CTLR[index].bits.DMAHALT = 1U;
        DMA1_PTR->CTLR[index].bits.DMAHALT = 1U;
    }
    __asm__ volatile("SYNC");
    for (index = 0U; index < MS_SCOPE_DMA_CHANNEL_COUNT; index++)
    {
        DMA0_PTR->CTLR[index].bits.DMAEN = 0U;
        DMA1_PTR->CTLR[index].bits.DMAEN = 0U;
    }
    DMA0_PTR->LIER.reg = 0U;
    DMA1_PTR->LIER.reg = 0U;
    __asm__ volatile("SYNC");

    for (index = 0U; index < MS_SCOPE_DMA_CHANNEL_COUNT; index++)
    {
        if ((DMA0_PTR->CTLR[index].bits.DMAEN != 0U) ||
            (DMA1_PTR->CTLR[index].bits.DMAEN != 0U))
        {
            return 0;
        }
    }
    return 1;
}

static int ms_scope_clock_fault_active(
    const struct ms_scope_kf32_port *port)
{
    if (port->fault_cause != MS_SCOPE_NMI_VECTOR)
    {
        return 0;
    }
    return (OSC_PTR->INT.bits.CKFIF != 0U) ? 1 : 0;
}

static int ms_scope_iwdt_active(void)
{
    return (IWDT_PTR->CTL.bits.IWDTEN != 0U) ? 1 : 0;
}

static void ms_scope_service_watchdog(void *user)
{
    ms_probe_u32 pm_write_enabled;
    ms_probe_u32 backup_reset_released;
    ms_probe_u32 backup_write_enabled;

    (void)user;
    if (ms_scope_iwdt_active() == 0)
    {
        return;
    }

    pm_write_enabled = OSC_PTR->CTRL0.bits.PMWREN;
    backup_reset_released = PM_PTR->CTRL0.bits.BKPREGCLR;
    backup_write_enabled = PM_PTR->CTRL0.bits.BKPWR;
    OSC_PTR->CTRL0.bits.PMWREN = 1U;
    PM_PTR->CTRL0.bits.BKPREGCLR = 1U;
    PM_PTR->CTRL0.bits.BKPWR = 1U;
    __asm__ volatile("SYNC");
    IWDT_PTR->FD.bits.IWDTFD = 0x55AA55AAU;
    PM_PTR->CTRL0.bits.BKPWR = backup_write_enabled;
    PM_PTR->CTRL0.bits.BKPREGCLR = backup_reset_released;
    OSC_PTR->CTRL0.bits.PMWREN = pm_write_enabled;
}

static int ms_scope_enter_poll_mode(void *user)
{
    struct ms_scope_kf32_port *port;

    port = (struct ms_scope_kf32_port *)user;
    USART0_PTR->IER.reg = 0U;
    INTR_PTR->EIE1.bits.USART0IE = 0U;
    USART0_PTR->CTLR.bits.USARTEN = 1U;
    USART0_PTR->CTLR.bits.RXEN = 1U;
    USART0_PTR->CTLR.bits.TXEN = 1U;
    if (ms_scope_clock_fault_active(port) != 0)
    {
        USART0_PTR->BRGR.reg = MS_SCOPE_INTHF_UART_BRGR;
    }
    ms_scope_clear_uart_errors();
    T23_PTR->CTRL1.bits.TXIE = 0U;
    INTR_PTR->EIE3.bits.T23IE = 0U;
    if (ms_scope_clock_fault_active(port) != 0)
    {
        T23_PTR->CTRL1.bits.TXEN = 0U;
        T23_PTR->PRSC.bits.TXCKS = MS_SCOPE_INTHF_TIMER_PRESCALER;
        T23_PTR->CNT.bits.TXCNT = 0U;
        T23_PTR->PPX.bits.PPX = MS_SCOPE_TIMER_PERIOD;
        T23_PTR->CTRL1.bits.TXEN = 1U;
    }
    ms_scope_clear_timer_flag();
    port->elapsed_ms = 0U;
    return 0;
}

static int ms_scope_poll_read(void *user, ms_probe_u8 *data,
                              ms_probe_u16 capacity)
{
    ms_probe_u16 length;

    (void)user;
    length = 0U;
    while (length < capacity)
    {
        if (USART0_PTR->STR.bits.RDRIF == 0U)
        {
            if ((USART0_PTR->STR.bits.OVFEIF != 0U) ||
                (USART0_PTR->STR.bits.PAREIF != 0U) ||
                (USART0_PTR->STR.bits.FREIF != 0U) ||
                (USART0_PTR->STR.bits.REIF != 0U))
            {
                ms_scope_clear_uart_errors();
            }
            break;
        }
        data[length] = (ms_probe_u8)USART0_PTR->BUFR.RBUFR.RBUF;
        length++;
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
           (USART0_PTR->STR.bits.TFEIF1 != 0U))
    {
        USART0_PTR->BUFR.TBUFR.TBUF = data[written];
        written++;
    }
    return (int)written;
}

static int ms_scope_try_copy(ms_probe_u32 address, ms_probe_u8 *data,
                             ms_probe_u32 length)
{
    ms_probe_u32 index;

    for (index = 0U; index < length; index++)
    {
        if (ms_scope_kf32_try_read_u8(
                (const volatile ms_probe_u8 *)(unsigned long)
                    (address + index),
                &data[index]) != 0)
        {
            return -1;
        }
    }
    return 0;
}

static int ms_scope_read_memory(void *user, ms_probe_u32 address,
                                ms_probe_u8 *data, ms_probe_u16 length)
{
    (void)user;
    return ms_scope_try_copy(address, data, (ms_probe_u32)length);
}

static ms_probe_u32 ms_scope_now_ms(void *user)
{
    struct ms_scope_kf32_port *port;

    port = (struct ms_scope_kf32_port *)user;
    if (T23_PTR->CTRL1.bits.TXIF != 0U)
    {
        ms_scope_clear_timer_flag();
        port->elapsed_ms++;
    }
    return port->elapsed_ms;
}

static void ms_scope_reset(void *user)
{
    ms_probe_u32 spin_count;

    (void)user;
    spin_count = 0U;
    while ((USART0_PTR->STR.bits.TXEIF == 0U) &&
           (spin_count < MS_SCOPE_RESET_TX_SPIN_LIMIT))
    {
        spin_count++;
    }
    SYSTEM_PTR->ARCTL.reg = MS_SCOPE_SYSTEM_RESET_VALUE;
    __asm__ volatile("SYNC");
    for (;;)
    {
        __asm__ volatile("NOP");
    }
}

static int ms_scope_core_frame(
    const struct ms_scope_kf32_entry_state *entry,
    struct ms_scope_kf32_core_frame *frame,
    ms_probe_u32 *fault_sp)
{
    ms_probe_u32 address;

    if (entry->cause == MS_SCOPE_STACKFAULT_VECTOR)
    {
        return 0;
    }
    address = ((entry->exc_return & MS_SCOPE_EXC_RETURN_USE_PSP) != 0U) ?
              entry->psp : entry->msp;
    if (ms_scope_ram_address_valid(address,
                                   MS_SCOPE_EXCEPTION_FRAME_SIZE) == 0)
    {
        return 0;
    }
    if (ms_scope_try_copy(address, (ms_probe_u8 *)frame,
                          MS_SCOPE_EXCEPTION_FRAME_SIZE) != 0)
    {
        return 0;
    }
    if (((frame->pc & 0x1U) != 0U) ||
        (ms_scope_code_address_valid(frame->pc) == 0))
    {
        return 0;
    }
    *fault_sp = address + MS_SCOPE_EXCEPTION_FRAME_SIZE;
    return 1;
}

static void ms_scope_capture_context(
    const struct ms_scope_kf32_entry_state *entry)
{
    struct ms_scope_kf32_core_frame frame;
    ms_probe_u8 *payload;
    ms_probe_u32 values[MS_SCOPE_KF32_CONTEXT_VALUE_COUNT];
    ms_probe_u32 fault_sp;
    ms_probe_u32 index;
    int valid;

    ms_scope_zero(ms_scope_fault_context, MS_SCOPE_KF32_CONTEXT_SIZE);
    ms_scope_zero((ms_probe_u8 *)values, (ms_probe_u32)sizeof(values));
    ms_scope_zero((ms_probe_u8 *)&frame, (ms_probe_u32)sizeof(frame));
    fault_sp = 0U;
    valid = ms_scope_core_frame(entry, &frame, &fault_sp);

    ms_scope_fault_context[0] = (ms_probe_u8)'M';
    ms_scope_fault_context[1] = (ms_probe_u8)'S';
    ms_scope_fault_context[2] = (ms_probe_u8)'C';
    ms_scope_fault_context[3] = (ms_probe_u8)'T';
    ms_scope_put_u16(&ms_scope_fault_context[4],
                     MS_SCOPE_KF32_CONTEXT_VERSION);
    ms_scope_fault_context[6] = MS_PROBE_ARCH_KUNGFU32;
    ms_scope_put_u32(&ms_scope_fault_context[8],
                     MS_SCOPE_KF32_CONTEXT_SIZE);
    ms_scope_put_u32(&ms_scope_fault_context[12], ms_scope_fault_epoch);
    ms_scope_put_u32(&ms_scope_fault_context[16], entry->cause);

    if (valid != 0)
    {
        values[0] = frame.r0;
        values[1] = frame.r1;
        values[2] = frame.r2;
        values[3] = frame.r3;
        values[4] = frame.r4;
        for (index = 0U; index < 8U; index++)
        {
            values[5U + index] = entry->r5_r12[index];
        }
        values[13] = fault_sp;
        values[14] = frame.lr;
        values[15] = frame.pc;
        values[16] = frame.xpsr;
    }
    values[17] = entry->msp;
    values[18] = entry->psp;
    values[19] = entry->mctl;
    values[20] = (entry->intr_ctrl0 >> 12) & 0x0fU;
    values[21] = (entry->intr_ctrl0 >> 6) & 0x01U;
    values[22] = entry->exc_return;
    values[23] = entry->intr_ctrl0;
    values[24] = INTR_PTR->EIF0.reg;
    values[25] = MPU_PTR->CTLESR.reg;
    values[26] = SYSTEM_PTR->VECTOFF.reg;

    payload = &ms_scope_fault_context[MS_PROBE_CONTEXT_HEADER_SIZE];
    for (index = 0U; index < MS_SCOPE_KF32_CONTEXT_VALUE_COUNT; index++)
    {
        ms_scope_put_u32(&payload[index * 4U], values[index]);
    }
    ms_scope_put_u32(&ms_scope_fault_context[20],
                     ms_probe_crc32(payload,
                                    MS_SCOPE_KF32_CONTEXT_PAYLOAD_SIZE));
    if (valid != 0)
    {
        ms_scope_fault_context[7] = MS_SCOPE_CONTEXT_FLAG_VALID;
    }
    ms_probe_set_context(&ms_scope_probe, ms_scope_fault_context,
                         MS_SCOPE_KF32_CONTEXT_SIZE,
                         MS_SCOPE_KF32_CONTEXT_VERSION, valid);
}

static MS_SCOPE_NORETURN void ms_scope_nested_fault(void)
{
    for (;;)
    {
        __asm__ volatile("NOP");
    }
}

static void ms_scope_install_vectors(void)
{
    const volatile ms_probe_u32 *source;
    ms_probe_u32 source_address;
    ms_probe_u32 index;
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    source_address = SYSTEM_PTR->VECTOFF.reg;
    if (source_address == 0U)
    {
        source = &_start;
    }
    else
    {
        source = (const volatile ms_probe_u32 *)(unsigned long)source_address;
    }
    for (index = 0U; index < MS_SCOPE_VECTOR_COUNT; index++)
    {
        ms_scope_vector_table[index] = source[index];
    }
    ms_scope_vector_table[MS_SCOPE_HARDFAULT_VECTOR] =
        (ms_probe_u32)(unsigned long)&ms_scope_kf32_hardfault_entry;
    ms_scope_vector_table[MS_SCOPE_NMI_VECTOR] =
        (ms_probe_u32)(unsigned long)&ms_scope_kf32_nmi_entry;
    ms_scope_vector_table[MS_SCOPE_STACKFAULT_VECTOR] =
        (ms_probe_u32)(unsigned long)&ms_scope_kf32_stackfault_entry;
    ms_scope_vector_table[MS_SCOPE_ARIFAULT_VECTOR] =
        (ms_probe_u32)(unsigned long)&ms_scope_kf32_arifault_entry;
    ms_scope_vector_table[MS_SCOPE_SRAM_ECC_VECTOR] =
        (ms_probe_u32)(unsigned long)&ms_scope_kf32_sram_ecc_entry;
    ms_scope_vector_table[MS_SCOPE_DPRAM_ECC_VECTOR] =
        (ms_probe_u32)(unsigned long)&ms_scope_kf32_dpram_ecc_entry;
    ms_scope_vector_table[MS_SCOPE_CACHE_ECC_VECTOR] =
        (ms_probe_u32)(unsigned long)&ms_scope_kf32_cache_ecc_entry;
    ms_scope_vector_table[MS_SCOPE_FLASH_ECC_VECTOR] =
        (ms_probe_u32)(unsigned long)&ms_scope_kf32_flash_ecc_entry;
    ms_scope_vector_table[MS_SCOPE_BUSFAULT_VECTOR] =
        (ms_probe_u32)(unsigned long)&ms_scope_kf32_busfault_entry;
    __asm__ volatile("SYNC");
    SYSTEM_PTR->VECTOFF.reg =
        (ms_probe_u32)(unsigned long)ms_scope_vector_table;
    __asm__ volatile("SYNC");
    rt_hw_interrupt_enable(level);
}

int ms_scope_kf32_probe_init(void)
{
    struct ms_probe_config config;

    ms_scope_zero((ms_probe_u8 *)&ms_scope_port,
                  (ms_probe_u32)sizeof(ms_scope_port));
    ms_scope_zero(ms_scope_fault_context, MS_SCOPE_KF32_CONTEXT_SIZE);
    ms_scope_zero((ms_probe_u8 *)&ms_scope_kf32_entry_state,
                  (ms_probe_u32)sizeof(ms_scope_kf32_entry_state));
    ms_scope_zero((ms_probe_u8 *)&config,
                  (ms_probe_u32)sizeof(config));
    ms_scope_fault_active = 0U;
    ms_scope_kf32_read_active = 0U;
    ms_scope_fault_epoch = 0U;
    ms_scope_initialized = 0;

    config.ops.enter_poll_mode = ms_scope_enter_poll_mode;
    config.ops.poll_read = ms_scope_poll_read;
    config.ops.poll_write = ms_scope_poll_write;
    config.ops.read_memory = ms_scope_read_memory;
    config.ops.now_ms = ms_scope_now_ms;
    config.ops.service_watchdog = ms_scope_service_watchdog;
    config.ops.reset = ms_scope_reset;
    config.user = &ms_scope_port;
    config.regions = ms_scope_regions;
    config.region_count = (ms_probe_u8)(sizeof(ms_scope_regions) /
                                        sizeof(ms_scope_regions[0]));
    config.max_payload = (ms_probe_u16)RT_SCOPE_MAX_PAYLOAD;
    config.max_read_length = (ms_probe_u32)RT_SCOPE_MAX_READ_LENGTH;
    config.frame_timeout_ms = (ms_probe_u32)RT_SCOPE_FRAME_TIMEOUT_MS;
    config.device_id = (ms_probe_u32)RT_SCOPE_DEVICE_ID;
    config.capabilities = MS_PROBE_CAP_RTTHREAD_PRESENT;
    config.architecture = MS_PROBE_ARCH_KUNGFU32;
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

MS_SCOPE_NORETURN void ms_scope_kf32_fault_main(
    struct ms_scope_kf32_entry_state *entry)
{
    if (ms_scope_initialized == 0)
    {
        ms_scope_nested_fault();
    }
    ms_scope_port.fault_cause = entry->cause;
    if (ms_scope_stop_dma() != 0)
    {
        ms_scope_probe.config.capabilities |= MS_PROBE_CAP_MEMORY_STABLE;
    }
    else
    {
        ms_scope_probe.config.capabilities &=
            (ms_probe_u16)(~MS_PROBE_CAP_MEMORY_STABLE);
    }
    if (ms_scope_iwdt_active() != 0)
    {
        ms_scope_probe.config.capabilities |= MS_PROBE_CAP_WATCHDOG_REQUIRED;
    }
    else
    {
        ms_scope_probe.config.capabilities &=
            (ms_probe_u16)(~MS_PROBE_CAP_WATCHDOG_REQUIRED);
    }
    ms_scope_fault_epoch++;
    ms_scope_capture_context(entry);
    ms_probe_activate(&ms_scope_probe, ms_scope_fault_epoch);
    ms_probe_run_fault(&ms_scope_probe);
    ms_scope_nested_fault();
}

#endif
