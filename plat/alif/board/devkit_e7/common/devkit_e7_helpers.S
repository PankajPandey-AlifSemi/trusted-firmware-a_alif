/*
 * Copyright (c) 2025, Alif Semiconductor. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch.h>
#include <asm_macros.S>
#include <lib/cpus/aarch32/cortex_a32.h>
#include <platform_def.h>

	.globl	plat_secondary_cold_boot_setup
	.globl	plat_get_my_entrypoint
	.globl	plat_is_my_cpu_primary
	.globl	plat_alif_calc_core_pos
	.globl	plat_my_core_pos
	.globl	plat_crash_console_init
	.globl	plat_crash_console_putc
	.globl	plat_crash_console_flush
	.globl	wfi_loop
	.globl	secondary_cpu_flags

	/*
	 * r1 = &secondary_cpu_flags[cpu - 1]
	 * Clobbers: r0
	 */
	.macro secondary_flag_ptr
	ldcopr	r0, MPIDR
	and	r0, r0, #MPIDR_CPU_MASK
	sub	r0, r0, #1
	lsl	r0, r0, #2
	ldr	r1, =secondary_cpu_flags
	add	r1, r1, r0
	.endm

	/* --------------------------------------------------------------------
	 * void plat_secondary_cold_boot_setup(void);
	 *
	 * Park secondary CPUs until PSCI CPU_ON posts SECONDARY_CPU_GO_FLAG,
	 * then jump to the SP_MIN warm entrypoint. Re-poll after each WFI
	 * so a wake from HOST_CPU_WAKEUP is observed without depending on SEV.
	 * --------------------------------------------------------------------
	 */
func plat_secondary_cold_boot_setup
	ldcopr16	r0, r1, CORTEX_A32_CPUECTLR_EL1
	bic	r0, r0, #0x40		/* CPUECTLR.SMPEN */
	stcopr16	r0, r1, CORTEX_A32_CPUECTLR_EL1

poll_flag:
	secondary_flag_ptr
	/*
	 * SMPEN is clear, so this core is outside coherency. Invalidate
	 * the flag line or CPU_ON stores are missed.
	 */
	mcr	p15, 0, r1, c7, c6, 1	/* DCIMVAC */
	dsb
	ldr	r1, [r1]
	ldr	r0, =SECONDARY_CPU_GO_FLAG
	cmp	r1, r0
	beq	go_warm
	/*
	 * WFI, not WFE: SEV will not wake this. CPU_ON must set
	 * HOST_CPU_WAKEUP (and BOOT_MSK) to leave this WFI.
	 */
	wfi
	b	poll_flag

go_warm:
	/* Clear the go-flag before warm entry so CPU_OFF cannot re-take it. */
	secondary_flag_ptr
	mov	r0, #0
	str	r0, [r1]
	dsb
	ldr	r9, =sp_min_warm_entrypoint
	mov	pc, r9
endfunc plat_secondary_cold_boot_setup

	/* --------------------------------------------------------------------
	 * void wfi_loop(void);
	 *
	 * Alias of the secondary park loop (cold-boot hold path).
	 * --------------------------------------------------------------------
	 */
func wfi_loop
	b	plat_secondary_cold_boot_setup
endfunc wfi_loop

	.pushsection .data
	.align 2
secondary_cpu_flags:
	.rept	PLATFORM_CORE_COUNT
	.word	0
	.endr
	.popsection

func plat_get_my_entrypoint
	/* Cold reset */
	mov	r0, #0
	bx	lr
endfunc plat_get_my_entrypoint

	/* -----------------------------------------------------
	 * unsigned int plat_is_my_cpu_primary (void);
	 *
	 * Find out whether the current CPU is the primary
	 * CPU.
	 * -----------------------------------------------------
	 */
func plat_is_my_cpu_primary
	ldcopr	r0, MPIDR
	ldr	r1, =MPIDR_AFFINITY_MASK
	and	r0, r1
	cmp	r0, #0
	moveq	r0, #1
	movne	r0, #0
	bx	lr
endfunc plat_is_my_cpu_primary

func plat_my_core_pos
	ldcopr	r0, MPIDR
	b	plat_alif_calc_core_pos
endfunc plat_my_core_pos

	/* ---------------------------------------------------------------------
	 * unsigned int plat_alif_calc_core_pos(u_register_t mpidr)
	 *
	 * Function to calculate the core position on Corstone700.
	 *
	 * (ClusterId * MAX_CPUS_PER_CLUSTER * MAX_PE_PER_CPU) +
	 * (CPUId * MAX_PE_PER_CPU) +
	 * ThreadId
	 *
	 * which can be simplified as:
	 *
	 * ((ClusterId * MAX_CPUS_PER_CLUSTER + CPUId) * MAX_PE_PER_CPU)
	 * + ThreadId
	 * ---------------------------------------------------------------------
	 */
func plat_alif_calc_core_pos
	mov	r3, r0

	/* Extract individual affinity fields from MPIDR */
	ubfx	r0, r3, #MPIDR_AFF0_SHIFT, #MPIDR_AFFINITY_BITS
	ubfx	r1, r3, #MPIDR_AFF1_SHIFT, #MPIDR_AFFINITY_BITS
	ubfx	r2, r3, #MPIDR_AFF2_SHIFT, #MPIDR_AFFINITY_BITS

	/* Compute linear position */
	mov	r3, #DEVKIT_E7_MAX_CPUS_PER_CLUSTER
	mla	r1, r2, r3, r1
	mov	r3, #DEVKIT_E7_MAX_PE_PER_CPU
	mla	r0, r1, r3, r0

	bx	lr
endfunc plat_alif_calc_core_pos

	/* ---------------------------------------------
	 * int plat_crash_console_init(void)
	 * Function to initialize the crash console
	 * without a C Runtime to print crash report.
	 * Clobber list : r0 - r3
	 * ---------------------------------------------
	 */
func plat_crash_console_init
	mov 	r0, #0
	bx	lr
endfunc plat_crash_console_init

	/* ---------------------------------------------
	 * int plat_crash_console_putc(int c)
	 * Function to print a character on the crash
	 * console without a C Runtime.
	 * Clobber list : r1 - r2
	 * ---------------------------------------------
	 */
func plat_crash_console_putc
	mov 	r0, #0
	bx	lr
endfunc plat_crash_console_putc

	/* ---------------------------------------------
	 * int plat_crash_console_flush()
	 * Function to force a write of all buffered
	 * data that hasn't been output.
	 * Out : return -1 on error else return 0.
	 * Clobber list : r0
	 * ---------------------------------------------
	 */
func plat_crash_console_flush
	mov 	r0, #0
	bx	lr
endfunc plat_crash_console_flush
