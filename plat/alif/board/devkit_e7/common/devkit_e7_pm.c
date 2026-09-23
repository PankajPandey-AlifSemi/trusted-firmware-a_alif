/*
 * Copyright (c) 2025, Alif Semiconductor. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <stdbool.h>

#include <platform_def.h>

#include <arch.h>
#include <arch_helpers.h>
#include <common/debug.h>
#include <drivers/console.h>
#include <drivers/delay_timer.h>
#include <lib/mmio.h>
#include <lib/psci/psci.h>
#include <plat/alif/common/plat_alif.h>

/*
 * Prototype is in lib/psci/psci_private.h. CPU0 SYSTEM_OFF skips the
 * generic CPU_OFF path, so this file calls it directly. Level 0 cleans
 * L1. A higher level also cleans L2.
 */
void psci_do_pwrdown_cache_maintenance(unsigned int pwr_level);

#define HOST_BASE_SYS_CTRL		UL(0x1A010000)
#define HOST_CPU_BOOT_MSK		UL(0x300)
#define HOST_CPU_CLUS_PWR_REQ		UL(0x304)
#define HOST_CPU_WAKEUP			UL(0x308)
/*
 * BSYS_PWR_REQ / BSYS_PWR_ST (SSE-710 Host Base System Control).
 * [0] WAKEUP_EN, [1] REFCLK_REQ, [2] DBGTOP_PWR_REQ, [5:3] SYSTOP_PWR_REQ.
 * SYSTEM_OFF does not write BSYS_PWR_REQ. Clearing it drops SYSTOP,
 * DBGTOP and REFCLK while CPU0 is still running from XIP. M55 applies
 * chip-level power after A32_GOING_OFF.
 */
#define HOST_BSYS_PWR_REQ		UL(0x400)
#define HOST_BSYS_PWR_ST		UL(0x404)

#define HOST_CPU_CORE_PPU_0_BASE	UL(0x1BC10000)
#define HOST_CPU_CORE_PPU_STRIDE	UL(0x10000)
#define HOST_CPU_CORE_PPU_BASE(cpu)	\
	(HOST_CPU_CORE_PPU_0_BASE + ((cpu) * HOST_CPU_CORE_PPU_STRIDE))
#define PPU_PWPR			UL(0x000)
#define PPU_PWSR			UL(0x008)
#define PPU_PWSR_POLICY_MASK		U(0xF)
/* PWR_DYN_EN | PWR_POLICY=OFF. Default ON policy is 0x108. */
#define PPU_PWPR_OFF_DYN		U(0x100)

#define SECONDARY_PPU_OFF_TIMEOUT_US	UL(100000)

extern unsigned int secondary_cpu_flags[PLATFORM_CORE_COUNT];
void __dead2 ensemble_secondary_ppu_off(void);

static unsigned int ensemble_mpidr_to_cpu(u_register_t mpidr)
{
	return (unsigned int)(mpidr & MPIDR_CPU_MASK);
}

static unsigned int ensemble_secondary_index(unsigned int cpu)
{
	assert(cpu > 0U);
	assert(cpu < PLATFORM_CORE_COUNT);
	return cpu - 1U;
}

static void ensemble_publish_secondary_flags(void)
{
	flush_dcache_range((uintptr_t)&secondary_cpu_flags,
			   sizeof(secondary_cpu_flags));
	dsb();
}

static void ensemble_set_cpu_flag(unsigned int cpu, unsigned int flag)
{
	secondary_cpu_flags[ensemble_secondary_index(cpu)] = flag;
	ensemble_publish_secondary_flags();
}

static void ensemble_set_cpu_boot_wakeup(unsigned int cpu, bool enable)
{
	unsigned int mask = 1U << cpu;
	unsigned int val;

	val = mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_BOOT_MSK);
	val = enable ? (val | mask) : (val & ~mask);
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_BOOT_MSK, val);

	val = mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP);
	val = enable ? (val | mask) : (val & ~mask);
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP, val);
	dsb();
}

static bool ensemble_core_ppu_is_off(unsigned int cpu)
{
	uint32_t pwsr = mmio_read_32(HOST_CPU_CORE_PPU_BASE(cpu) + PPU_PWSR);

	return (pwsr & PPU_PWSR_POLICY_MASK) == 0U;
}

static int ensemble_pwr_domain_on(u_register_t mpidr)
{
	unsigned int cpu = ensemble_mpidr_to_cpu(mpidr);

	assert(cpu > 0U);
	assert(cpu < PLATFORM_CORE_COUNT);

	/*
	 * Post the go-flag so the parked core takes the SP_MIN warm path.
	 * BOOT_MSK plus HOST_CPU_WAKEUP is what SSE-710 documents for the
	 * first core release after CLUSTOP leaves OFF, MEM_RET, or
	 * WARM_RST. WAKEUPREQ is honored only when this core PPU is OFF
	 * or OFF_EMU. A later wake from OFF still uses this pair; it is
	 * not switched to a GIC interrupt here.
	 */
	ensemble_set_cpu_flag(cpu, SECONDARY_CPU_GO_FLAG);
	ensemble_set_cpu_boot_wakeup(cpu, true);

	return PSCI_E_SUCCESS;
}

static void ensemble_pwr_domain_on_finish(const psci_power_state_t *target_state)
{
	unsigned int cpu = ensemble_mpidr_to_cpu(read_mpidr_el1());

	(void)target_state;

	/*
	 * Drop the go-flag now that this core has left the park loop.
	 * Leaving SECONDARY_CPU_GO_FLAG set would make a later CPU_OFF
	 * immediately re-enter the warm path.
	 */
	if ((cpu > 0U) && (cpu < PLATFORM_CORE_COUNT)) {
		ensemble_set_cpu_flag(cpu, 0U);
	}

	plat_alif_gic_pcpu_init();
	plat_alif_gic_cpuif_enable();
}

static void ensemble_pwr_domain_off(const psci_power_state_t *target_state)
{
	unsigned int cpu = ensemble_mpidr_to_cpu(read_mpidr_el1());

	(void)target_state;

	/*
	 * PSCI CPU_OFF is secondary-only. SYSTEM_OFF of the last remaining
	 * CPU (CPU 0) must not come through this path: the mailbox is
	 * indexed as [cpu - 1].
	 */
	assert(cpu > 0U);
	assert(cpu < PLATFORM_CORE_COUNT);

	plat_alif_gic_cpuif_disable();

	/*
	 * pwr_down_wfi programs this core's PPU to dynamic OFF. Leave the
	 * go-flag clear so the next CPU_ON is what posts it.
	 */
	ensemble_set_cpu_flag(cpu, 0U);
	ensemble_set_cpu_boot_wakeup(cpu, false);
}

static void __dead2
ensemble_pwr_domain_pwr_down_wfi(const psci_power_state_t *target_state)
{
	(void)target_state;

	/*
	 * CPU_OFF must drop this core's PPU. Cache maintenance already
	 * ran in the generic path. CPU_ON releases the core back through
	 * reset into the park loop.
	 */
	ensemble_secondary_ppu_off();
}

static int ensemble_validate_ns_entrypoint(uintptr_t entrypoint)
{
	if (entrypoint == 0U) {
		return PSCI_E_INVALID_ADDRESS;
	}

	if ((entrypoint >= BL32_RO_BASE) && (entrypoint < BL32_RO_LIMIT)) {
		return PSCI_E_INVALID_ADDRESS;
	}

	if ((entrypoint >= BL32_RW_BASE) && (entrypoint < BL32_RW_LIMIT)) {
		return PSCI_E_INVALID_ADDRESS;
	}

	return PSCI_E_SUCCESS;
}

/*
 * Program this core's PPU to dynamic OFF (PWR_DYN_EN, policy OFF).
 * The PPU stays ON until this core is in WFI.
 *
 * DBGPWRDUP is an input to the Cortex-A32. The A32 TRM requires it low
 * before core power is removed. On SSE-710 it is a HOSTCPUPWR signal,
 * not a Host Base System Control register, so software cannot clear it.
 * DBGOSDLR.DLK does not drive that pin. The CORE PPU removes core power
 * after PWPR is OFF and this core is in WFI; OFF is the mode in which
 * SSE-710 rejects debug access to the core.
 */
static void ensemble_core_ppu_dyn_off(unsigned int cpu)
{
	mmio_write_32(HOST_CPU_CORE_PPU_BASE(cpu) + PPU_PWPR, PPU_PWPR_OFF_DYN);
}

static void __dead2 ensemble_wfi_forever(void)
{
	for (;;) {
		dsb();
		isb();
		wfi();
	}
}

void __dead2 ensemble_secondary_ppu_off(void)
{
	unsigned int cpu = ensemble_mpidr_to_cpu(read_mpidr_el1());

	disable_irq();
	disable_fiq();
	plat_alif_gic_cpuif_disable();

	/*
	 * Clear only this core's BOOT_MSK and WAKEUP bits. CPU_OFF
	 * already ran psci_do_pwrdown_cache_maintenance() before
	 * pwr_domain_off, so this core has left coherency. A core that
	 * enters from the park loop comes out of reset with the cache
	 * off and SMPEN clear. Neither path repeats that maintenance.
	 */
	if ((cpu > 0U) && (cpu < PLATFORM_CORE_COUNT)) {
		ensemble_set_cpu_boot_wakeup(cpu, false);
	}

	ensemble_core_ppu_dyn_off(cpu);
	dsb();
	isb();
	ensemble_wfi_forever();
}

static bool ensemble_request_secondary_ppu_off(void)
{
	unsigned int cpu;
	uint64_t timeout;

	for (cpu = 1U; cpu < PLATFORM_CORE_COUNT; cpu++) {
		if (ensemble_core_ppu_is_off(cpu)) {
			VERBOSE("PSCI: CPU%u PPU already OFF\n", cpu);
			continue;
		}

		ensemble_set_cpu_flag(cpu, SECONDARY_CLUSTER_OFF_FLAG);
		/*
		 * WAKEUPREQ is ignored unless this core's PPU is OFF or
		 * OFF_EMU, so the pulse cannot wake a secondary parked ON
		 * in WFI. A secondary still running Linux never reads
		 * CLUSTER_OFF. The primary waits on PPU_PWSR only.
		 */
		ensemble_set_cpu_boot_wakeup(cpu, true);
	}

	timeout = timeout_init_us(SECONDARY_PPU_OFF_TIMEOUT_US);
	do {
		flush_dcache_range((uintptr_t)&secondary_cpu_flags,
				   sizeof(secondary_cpu_flags));
		for (cpu = 1U; cpu < PLATFORM_CORE_COUNT; cpu++) {
			if (!ensemble_core_ppu_is_off(cpu)) {
				break;
			}
		}
		if (cpu == PLATFORM_CORE_COUNT) {
			return true;
		}
	} while (timeout_elapsed(timeout) == 0U);

	WARN("PSCI: secondary PPU-off handshake timed out\n");
	return false;
}

static void ensemble_clear_cpu_wakeup(unsigned int cpu)
{
	unsigned int mask = 1U << cpu;
	unsigned int val;

	val = mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP);
	val &= ~mask;
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP, val);
	dsb();
}

static void __dead2 ensemble_system_off(void)
{
	bool secondaries_off;

	INFO("PSCI: system off APSS cluster\n");

	/*
	 * Ask any secondary whose PPU is still ON to run its own OFF
	 * sequence. Do not write that core's PWPR from here. Linux is
	 * expected to hot-unplug secondaries before SYSTEM_OFF. A core
	 * still running never sees CLUSTER_OFF, and this wait then times
	 * out. psci_stop_other_cores() would make each core run its own
	 * power-down, but that needs an SGI/FIQ path in SP_MIN.
	 */
	secondaries_off = ensemble_request_secondary_ppu_off();
	if (secondaries_off) {
		/* CLUSTOP may drop only after every secondary PPU is OFF. */
		mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_CLUS_PWR_REQ, 0U);
	}

	disable_irq();
	disable_fiq();

	plat_alif_gic_cpuif_disable();
	/* GICD_CTLR: turn the distributor off so SPIs cannot wake us. */
	mmio_write_32(PLAT_ALIF_GICD_BASE, 0U);
	dsb();

	/*
	 * Clear CPU0's WAKEUP bit only. Do not clear HOST_CPU_BOOT_MSK
	 * for CPU 0: zeroing that boot mask can reset the core before WFI.
	 */
	ensemble_clear_cpu_wakeup(0U);
	dsb();
	isb();

	VERBOSE("PSCI: BSYS_PWR_REQ=0x%x ST=0x%x WAKEUP=0x%x\n",
		mmio_read_32(HOST_BASE_SYS_CTRL + HOST_BSYS_PWR_REQ),
		mmio_read_32(HOST_BASE_SYS_CTRL + HOST_BSYS_PWR_ST),
		mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP));
	console_flush();

	/*
	 * SYSTEM_OFF skips the generic CPU_OFF sequence. Program CPU0's
	 * PPU, then use TF-A's cache maintenance. Level 1 also cleans L2
	 * and is used only when every secondary PPU is OFF.
	 */
	ensemble_core_ppu_dyn_off(0U);
	dsb();
	psci_do_pwrdown_cache_maintenance(secondaries_off ? 1U : 0U);
	ensemble_wfi_forever();
}

/*******************************************************************************
 * Export the platform handlers via plat_alif_psci_pm_ops. The ALIF Standard
 * platform layer will take care of registering the handlers with PSCI.
 ******************************************************************************/
plat_psci_ops_t plat_alif_psci_pm_ops = {
	.pwr_domain_on = ensemble_pwr_domain_on,
	.pwr_domain_on_finish = ensemble_pwr_domain_on_finish,
	.pwr_domain_off = ensemble_pwr_domain_off,
	.pwr_domain_pwr_down_wfi = ensemble_pwr_domain_pwr_down_wfi,
	.validate_ns_entrypoint = ensemble_validate_ns_entrypoint,
	.system_off = ensemble_system_off
};

const plat_psci_ops_t *plat_alif_psci_override_pm_ops(plat_psci_ops_t *ops)
{
	return ops;
}
