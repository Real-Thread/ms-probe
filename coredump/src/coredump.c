/*
 * Copyright (c) 2006-2023 Shanghai Real-Thread Electronic Technology Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial
 *
 * This file is part of ms-probe, available under GPL-2.0 or a commercial
 * license. See LICENSE and LICENSES/LicenseRef-Commercial.txt.
 */

#include "rtthread.h"
#include "cpuport.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coredump.h"
#include "elf_define.h"


typedef uint16_t elf_half;
typedef uint32_t elf_word;

#if ECOREDUMP_ELF_CLASS == ELFCLASS32
typedef uint32_t          elf_addr;
typedef uint32_t          elf_off;
typedef struct elf32_phdr elf_phdr_t;
#else
typedef uint64_t          elf_addr;
typedef uint64_t          elf_off;
typedef struct elf64_phdr elf_phdr_t;
#endif

char *coredump_buffer = 0;
uint32_t coredump_buffer_index = 0;


void coredump_writebuffer(uint8_t *base, int length)
{
    memcpy((void *)((uint32_t)coredump_buffer + coredump_buffer_index), base, length);
    coredump_buffer_index +=length;
}

/* Elf file header type */
typedef struct
{
    unsigned char e_ident[EI_NIDENT];
    elf_half      e_type;
    elf_half      e_machine;
    elf_word      e_version;
    elf_addr      e_entry;
    elf_off       e_phoff;
    elf_off       e_shoff;
    elf_word      e_flags;
    elf_half      e_ehsize;
    elf_half      e_phentsize;
    elf_half      e_phnum;
    elf_half      e_shentsize;
    elf_half      e_shnum;
    elf_half      e_shstrndx;
} elf_ehdr_t;

/* Segment header type for 32bit arch */
struct elf32_phdr
{
    elf_word p_type;
    elf_off  p_offset;
    elf_addr p_vaddr;
    elf_addr p_paddr;
    elf_off  p_filesz;
    elf_off  p_memsz;
    elf_word p_flags;
    elf_off  p_align;
};

/* Segment header type for 64bit arch */
struct elf64_phdr
{
    elf_word p_type;   /* Identifies program segment type */
    elf_word p_flags;  /* Segment flags */
    elf_off  p_offset; /* Segment file offset */
    elf_addr p_vaddr;  /* Segment virtual address */
    elf_addr p_paddr;  /* Segment physical address */
    elf_off  p_filesz; /* Segment size in file */
    elf_off  p_memsz;  /* Segment size in memory */
    elf_off  p_align;  /* Segment alignment, file & memory */
};

typedef struct
{
    elf_word namesz; /* Size of entry's owner string */
    elf_word descsz; /* Size of the note descriptor */
    elf_word type;   /* Interpretation of the descriptor */
    char     name[1];
} elf_note_base_t;

typedef struct
{
    elf_word namesz;                            /* Size of entry's owner string */
    elf_word descsz;                            /* Size of the note descriptor */
    elf_word type;                              /* Interpretation of the descriptor */
    char     name[RT_ALIGN(sizeof("CORE"), 4)]; /* The only support name is "CORE" */
    uint8_t  desc[ECOREDUMP_PRSTATUS_SIZE];     /* descriptor of this note entry type */
} elf_note_prstatus_t;

typedef struct
{
    elf_word namesz;                             /* Size of entry's owner string */
    elf_word descsz;                             /* Size of the note descriptor */
    elf_word type;                               /* Interpretation of the descriptor */
    char     name[RT_ALIGN(sizeof("LINUX"), 4)]; /* The only support name is "LINUX" */
    uint8_t  desc[ECOREDUMP_FPREGSET_SIZE];      /* descriptor of this note entry type */
} elf_note_fpreg_entry_t;

typedef struct
{
    elf_off             file_off;
    uint32_t            with_fp;
    uint32_t            stack_size;
    cd_writeout_func_t writeout_func;
    union
    {
        elf_ehdr_t             elf_header;
        elf_phdr_t             program_header;
        elf_note_prstatus_t    prstatus;
        elf_note_fpreg_entry_t note_fp;
    } tmp;
    core_regset_type tmp_core_rset;
    fp_regset_type   tmp_fp_rset;
} coredump_instance_t;

static core_regset_type current_core_regset;
static fp_regset_type   current_fp_regset;

static coredump_instance_t m_ctx;

static void fill_core_file_header(elf_ehdr_t *elf_header, uint16_t num)
{
    memset(elf_header, 0, sizeof(elf_ehdr_t));

    elf_header->e_ident[EI_MAG0]    = ELFMAG0;
    elf_header->e_ident[EI_MAG1]    = ELFMAG1;
    elf_header->e_ident[EI_MAG2]    = ELFMAG2;
    elf_header->e_ident[EI_MAG3]    = ELFMAG3;
    elf_header->e_ident[EI_CLASS]   = ECOREDUMP_ELF_CLASS;
    elf_header->e_ident[EI_DATA]    = ECOREDUMP_ELF_ENDIAN;
    elf_header->e_ident[EI_VERSION] = 1;
    elf_header->e_ident[EI_OSABI]   = ECOREDUMP_OSABI;
    elf_header->e_type              = ET_CORE;
    elf_header->e_machine           = ECOREDUMP_MACHINE;
    elf_header->e_version           = 1;
    elf_header->e_entry             = 0;
    elf_header->e_phoff             = sizeof(elf_ehdr_t);
    elf_header->e_shoff             = 0;
    elf_header->e_flags             = 0;
    elf_header->e_ehsize            = sizeof(elf_ehdr_t);
    elf_header->e_phentsize         = sizeof(elf_phdr_t);
    elf_header->e_phnum             = num;
    elf_header->e_shentsize         = 0;
    elf_header->e_shnum             = 0;
    elf_header->e_shstrndx          = 0;
}

static void fill_program_header(elf_phdr_t *program_header,
                                elf_word    type,
                                elf_addr    vaddr,
                                elf_off     filesz,
                                elf_off     memsz,
                                elf_word    flag)
{
    program_header->p_type   = type;
    program_header->p_offset = m_ctx.file_off;
    program_header->p_vaddr  = vaddr;
    program_header->p_paddr  = 0;
    program_header->p_filesz = filesz;
    program_header->p_memsz  = memsz;
    program_header->p_flags  = flag;
    program_header->p_align  = 1;

    m_ctx.file_off += filesz;
}

static void fill_note_base(elf_note_base_t *note, const char *note_name, elf_word type, elf_word descsz)
{
    note->namesz = strlen(note_name) + 1;
    note->descsz = descsz;
    note->type   = type;
    memcpy(note->name, note_name, note->namesz);
    if (note->namesz & 3)
        memset(&note->name[note->namesz], 0, note->namesz & 3);
}

/*  fill prstatus struct in note segment */
static void fill_note_prstatus(elf_note_prstatus_t *prstatus, core_regset_type *regset)
{
    fill_note_base((elf_note_base_t *)prstatus, "CORE", NT_PRSTATUS, sizeof(prstatus->desc));
    fill_note_prstatus_desc(&prstatus->desc[0], regset);
}

/*  fill FP registers struct in note segment */
static void fill_note_vfp_regset(elf_note_fpreg_entry_t *prstatus, fp_regset_type *regset)
{
    fill_note_base((elf_note_base_t *)prstatus, "LINUX", NT_ARM_VFP, sizeof(prstatus->desc));
    fill_note_fpregset_desc(&prstatus->desc[0], regset);
}

void coredump_init(int with_fp, cd_writeout_func_t func)
{
    memset(&m_ctx, 0, sizeof(coredump_instance_t));
    m_ctx.stack_size    = 0;
    m_ctx.writeout_func = coredump_writebuffer;
    m_ctx.with_fp       = 0;
}

int32_t coredump_size(void)
{
    struct thread_info_ops ops;
    coredump_set_ops(&ops);
    return corefile_size(&ops);
}

static void fill_one_threads_regset(core_regset_type *core_regset, fp_regset_type *fp_regset)
{
    fill_note_prstatus(&m_ctx.tmp.prstatus, core_regset);
    m_ctx.writeout_func((uint8_t *)&m_ctx.tmp.prstatus, sizeof(elf_note_prstatus_t));

    if (m_ctx.with_fp)
    {
        fill_note_vfp_regset(&m_ctx.tmp.note_fp, fp_regset);
        m_ctx.writeout_func((uint8_t *)&m_ctx.tmp.note_fp, sizeof(elf_note_fpreg_entry_t));
    }
}

static void addr_align(uint32_t *addr, uint32_t *memlen)
{
    *memlen += ((*addr) & (~CORE_MEM_LINE_MASK));
    *addr &= CORE_MEM_LINE_MASK;
}

void gen_coredump(struct thread_info_ops *ops)
{
    int      note_size;
    uint32_t addr, memlen;

    int segment_count = ops->get_memarea_count(ops) + 1;
    fill_core_file_header(&m_ctx.tmp.elf_header, segment_count);
    m_ctx.writeout_func((uint8_t *)&m_ctx.tmp.elf_header, sizeof(elf_ehdr_t));

    m_ctx.file_off = sizeof(elf_ehdr_t) + segment_count * sizeof(elf_phdr_t);

    if (m_ctx.with_fp)
        note_size = (sizeof(elf_note_prstatus_t) + sizeof(elf_note_fpreg_entry_t)) * ops->get_threads_count(ops);
    else
        note_size = sizeof(elf_note_prstatus_t) * ops->get_threads_count(ops);

    fill_program_header(&m_ctx.tmp.program_header, PT_NOTE, 0, note_size, 0, PF_R);
    m_ctx.writeout_func((uint8_t *)&m_ctx.tmp.program_header, sizeof(elf_phdr_t));

    for (int i = 0; i < ops->get_memarea_count(ops); i++)
    {
        ops->get_memarea(ops, i, &addr, &memlen);
        addr_align(&addr, &memlen);
        fill_program_header(&m_ctx.tmp.program_header, PT_LOAD, addr, memlen, memlen, PF_R | PF_W);
        m_ctx.writeout_func((uint8_t *)&m_ctx.tmp.program_header, sizeof(elf_phdr_t));
    }

    ops->get_thread_regset(ops, ops->get_current_thread_idx(ops), &m_ctx.tmp_core_rset, &m_ctx.tmp_fp_rset);
    fill_one_threads_regset(&m_ctx.tmp_core_rset, &m_ctx.tmp_fp_rset);

    for (int i = 0; i < ops->get_threads_count(ops); i++)
    {
        if (ops->get_current_thread_idx(ops) != i)
        {
            ops->get_thread_regset(ops, i, &m_ctx.tmp_core_rset, &m_ctx.tmp_fp_rset);
            fill_one_threads_regset(&m_ctx.tmp_core_rset, &m_ctx.tmp_fp_rset);
        }
    }

    for (int i = 0; i < ops->get_memarea_count(ops); i++)
    {
        ops->get_memarea(ops, i, &addr, &memlen);
        addr_align(&addr, &memlen);
        m_ctx.writeout_func((uint8_t *)addr, memlen);
    }
}

int32_t corefile_size(struct thread_info_ops *ops)
{
    int      elf_size = 0;
    int      segment_count;
    uint32_t addr, memlen;

    segment_count = ops->get_memarea_count(ops) + 1;
    elf_size      = sizeof(elf_ehdr_t) + segment_count * sizeof(elf_phdr_t);

    if (m_ctx.with_fp)
        elf_size += (sizeof(elf_note_prstatus_t) + sizeof(elf_note_fpreg_entry_t)) * ops->get_threads_count(ops);
    else
        elf_size += sizeof(elf_note_prstatus_t) * ops->get_threads_count(ops);

    for (int i = 0; i < ops->get_memarea_count(ops); i++)
    {
        ops->get_memarea(ops, i, &addr, &memlen);
        addr_align(&addr, &memlen);
        elf_size += memlen;
    }

    return elf_size;
}

static int32_t coredump_thr_cnts(struct thread_info_ops *ops)
{
    return 1;
}

static int32_t coredump_cur_idx(struct thread_info_ops *ops)
{
    return 0;
}

static void coredump_thr_rset(struct thread_info_ops *ops, int32_t idx, core_regset_type *core_regset, fp_regset_type *fp_regset)
{
    memcpy(core_regset, &current_core_regset, sizeof(core_regset_type));
    memcpy(fp_regset, &current_fp_regset, sizeof(fp_regset_type));
}

static int32_t coredump_mem_cnts(struct thread_info_ops *ops)
{
    return 1;
}

static int32_t coredump_memarea(struct thread_info_ops *ops, int32_t idx, uint32_t *addr, uint32_t *memlen)
{
    *addr   = current_core_regset.sp;
    *memlen = m_ctx.stack_size;
    return 0;
}

void coredump_set_ops(struct thread_info_ops *ops)
{
    ops->get_threads_count      = coredump_thr_cnts;            /* Always return 1 */
    ops->get_current_thread_idx = coredump_cur_idx;             /* Always return 0 */
    ops->get_thread_regset      = coredump_thr_rset;            /*  */
    ops->get_memarea_count      = coredump_mem_cnts;            /* Always return 1 */
    ops->get_memarea            = coredump_memarea;             /* Record thread stack */
    ops->priv                   = NULL;
}

core_regset_type *get_cur_core_regset_address()
{
    return &current_core_regset;
}

fp_regset_type *get_cur_fp_regset_address()
{
    return &current_fp_regset;
}

static void get_exception_gpregset(void)
{
    struct exception_info *e_info;

    /* Get the exception context by reading the psp */
    __asm volatile(
                    "mrs %0, psp    \n"
                    :"+r"(e_info)
                    :: "memory");

    current_core_regset.r4 = e_info->stack_frame.r4;
    current_core_regset.r5 = e_info->stack_frame.r5;
    current_core_regset.r6 = e_info->stack_frame.r6;
    current_core_regset.r7 = e_info->stack_frame.r7;
    current_core_regset.r8 = e_info->stack_frame.r8;
    current_core_regset.r9 = e_info->stack_frame.r9;
    current_core_regset.r10 = e_info->stack_frame.r10;
    current_core_regset.r11 = e_info->stack_frame.r11;

    current_core_regset.r0 = e_info->stack_frame.exception_stack_frame.r0;
    current_core_regset.r1 = e_info->stack_frame.exception_stack_frame.r1;
    current_core_regset.r2 = e_info->stack_frame.exception_stack_frame.r2;
    current_core_regset.r3 = e_info->stack_frame.exception_stack_frame.r3;
    current_core_regset.r12 = e_info->stack_frame.exception_stack_frame.r12;
    current_core_regset.sp = (uint32_t)e_info;
    current_core_regset.lr = e_info->stack_frame.exception_stack_frame.lr;
    current_core_regset.pc = e_info->stack_frame.exception_stack_frame.pc;
    current_core_regset.xpsr = e_info->stack_frame.exception_stack_frame.psr;
}

/* TODO: Temp buffer. */
rt_uint8_t _coredump_buf[COREDUMP_SIZE];

/**
 * @brief Weakly defined core dump backend function for handling core dump data.
 *
 * This function is weakly defined (using the `weak` attribute), allowing users
 * to override it in other files to implement custom core dump handling logic.
 * By default, this function is an empty implementation and does nothing.
 *
 * @param src_buf Pointer to the core dump data buffer.
 * @param len     Length of the core dump data in bytes.
 */
__attribute__((weak)) void coredump_backend(rt_uint8_t *src_buf, rt_uint32_t len) { }

void coredump_handle(void)
{
    coredump_buffer = &_coredump_buf[0];
    struct thread_info_ops ops;

    rt_kprintf("**********Coredump start**********\n");

    /* erase_flash_data(COREDUMP_ADDR, COREDUMP_SIZE); */

    coredump_init(0, 0);

    /* coredump_buffer = rt_malloc(COREDUMP_SIZE); */

    memset(coredump_buffer, 0, COREDUMP_SIZE);
    memset(&coredump_buffer_index, 0, 4);

    m_ctx.stack_size = rt_thread_self()->stack_size;

    get_exception_gpregset();

    coredump_set_ops(&ops);
    gen_coredump(&ops);

    /* write_flash_data(COREDUMP_ADDR, (void *)coredump_buffer, coredump_buffer_index); */

    rt_kprintf("Core file size: %d(0x%X) Byte, record buffer start addr: 0x%X\n",
                coredump_buffer_index,
                coredump_buffer_index,
                &_coredump_buf[0]);
    coredump_backend(_coredump_buf, coredump_buffer_index);
    rt_kprintf("***********Coredump end***********\n");

    /* rt_free(coredump_buffer); */
}

