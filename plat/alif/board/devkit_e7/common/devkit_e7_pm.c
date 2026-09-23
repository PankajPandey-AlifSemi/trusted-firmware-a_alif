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
#include <lib/cpus/aarch32/cortex_a32.h>
#include <lib/mmio.h>
#include <lib/psci/psci.h>
#include <plat/alif/common/alif_mhu.h>
#include <plat/alif/common/plat_alif.h>

#define HOST_BASE_SYS_CTRL		UL(0x1A010000)
#define HOST_CPU_BOOT_MSK		UL(0x300)
#define HOST_CPU_CLUS_PWR_REQ		UL(0x304)
#define HOST_CPU_WAKEUP			UL(0x308)
#define HOST_CHS_PWR_REQ		UL(0x400)
#define HOST_CHS_PWR_ST			UL(0x404)

#define HOST_CPU_CORE_PPU_0_BASE	UL(0x1BC10000)
#define HOST_CPU_CORE_PPU_STRIDE	UL(0x10000)
#define HOST_CPU_CORE_PPU_BASE(cpu)	\
	(HOST_CPU_CORE_PPU_0_BASE + ((cpu) * HOST_CPU_CORE_PPU_STRIDE))
#define PPU_PWPR			UL(0x000)
#define PPU_PWSR			UL(0x008)
#define PPU_PWSR_POLICY_MASK		U(0xF)
/* PWR_DYN_EN | PWR_POLICY=OFF. Default ON policy is 0x108. */
#define PPU_PWPR_OFF_DYN		U(0x100)
#define DBGOSDLR_DLK_BIT		U(0x1)

#define SECONDARY_PPU_OFF_TIMEOUT_US	UL(100000)
#define SECONDARY_PPU_FORCE_TIMEOUT_US	UL(10000)

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

	/* Post the go-flag so the parked core takes the SP_MIN warm path. */
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

	/* Stay parked until the next CPU_ON posts the go-flag. */
	ensemble_set_cpu_flag(cpu, 0U);
	ensemble_set_cpu_boot_wakeup(cpu, false);
}

static void __dead2
ensemble_pwr_domain_pwr_down_wfi(const psci_power_state_t *target_state)
{
	(void)target_state;

	/*
	 * CPU_OFF must drop this core's PPU, not sit in a park WFI.
	 * CPU_ON brings the core back with BOOT_MSK + WAKEUP (reset into
	 * the park loop, then the go-flag).
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

static void ensemble_disable_smp(void)
{
	uint64_t ectlr;

	__asm__ volatile(
		"mrrc	p15, 1, %Q0, %R0, c15"
		: "=r" (ectlr));
	ectlr &= ~CORTEX_A32_CPUECTLR_SMPEN_BIT;
	__asm__ volatile(
		"mcrr	p15, 1, %Q0, %R0, c15\n"
		"isb\n"
		"dsb	sy"
		:
		: "r" (ectlr)
		: "memory");
}

/*
 * CPU-side sequence before WFI so the core PPU can cut power:
 * clean+invalidate caches while SCTLR.C is set, clear SCTLR.C,
 * set DBGOSDLR.DLK, then leave intra-cluster coherency.
 * I-cache and MMU stay on so BL32 XIP remains executable.
 */
static void ensemble_cpu_prepare_ppu_off(void)
{
	dcsw_op_all(DC_OP_CISW);
	dsb();
	isb();

	write_sctlr(read_sctlr() & ~SCTLR_C_BIT);
	isb();
	dsb();

	write_dbgosdlr(read_dbgosdlr() | DBGOSDLR_DLK_BIT);
	ensemble_disable_smp();
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

	if ((cpu > 0U) && (cpu < PLATFORM_CORE_COUNT)) {
		ensemble_set_cpu_flag(cpu, SECONDARY_PPU_OFF_ACK);
	}

	disable_irq();
	disable_fiq();
	plat_alif_gic_cpuif_disable();

	/*
	 * A set HOST_CPU_WAKEUP bit holds this core's PPU ON.
	 * Only the local core's PWPR write is honoured.
	 */
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP, 0U);
	if ((cpu > 0U) && (cpu < PLATFORM_CORE_COUNT)) {
		unsigned int val = mmio_read_32(HOST_BASE_SYS_CTRL +
						HOST_CPU_BOOT_MSK);

		val &= ~(1U << cpu);
		mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_BOOT_MSK, val);
	}

	mmio_write_32(HOST_CPU_CORE_PPU_BASE(cpu) + PPU_PWPR, PPU_PWPR_OFF_DYN);
	dsb();

	ensemble_cpu_prepare_ppu_off();
	ensemble_wfi_forever();
}

static void ensemble_request_secondary_ppu_off(void)
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
		 * Parked cores wait in WFI. SEV does not wake WFI; use the
		 * same BOOT_MSK + HOST_CPU_WAKEUP pulse as CPU_ON.
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
			return;
		}
	} while (timeout_elapsed(timeout) == 0U);

	WARN("PSCI: secondary PPU-off handshake timed out\n");
}

/*
 * Best-effort fallback: a remote PWPR write is ignored until the target
 * has run the local handshake. Also drop CLUSTOP / MEM_RET requests so
 * the cluster PPU is allowed to power down.
 */
static void ensemble_force_secondary_ppu_off(void)
{
	unsigned int cpu;
	uint64_t timeout;

	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_CLUS_PWR_REQ, 0U);

	for (cpu = 1U; cpu < PLATFORM_CORE_COUNT; cpu++) {
		if (!ensemble_core_ppu_is_off(cpu)) {
			mmio_write_32(HOST_CPU_CORE_PPU_BASE(cpu) + PPU_PWPR,
				      PPU_PWPR_OFF_DYN);
		}
	}
	dsb();

	timeout = timeout_init_us(SECONDARY_PPU_FORCE_TIMEOUT_US);
	do {
		for (cpu = 1U; cpu < PLATFORM_CORE_COUNT; cpu++) {
			if (!ensemble_core_ppu_is_off(cpu)) {
				break;
			}
		}
		if (cpu == PLATFORM_CORE_COUNT) {
			return;
		}
	} while (timeout_elapsed(timeout) == 0U);

	WARN("PSCI: secondary PPU still ON after fallback PWPR write\n");
}

static void __dead2 ensemble_system_off(void)
{
	INFO("PSCI: system off APSS cluster\n");

	/*
	 * Notify M55-HE before GIC / chassis teardown so the mailbox is
	 * still live. A timeout does not abort SYSTEM_OFF.
	 */
	if (alif_mhu_notify_going_off() != 0) {
		WARN("PSCI: M55-HE did not ACK A32_GOING_OFF; continuing\n");
	}
	alif_mhu_shutdown();

	/*
	 * A parked secondary is in WFI. Wake it the CPU_ON way with
	 * CLUSTER_OFF so it runs the local PPU handshake, then try a
	 * remote PWPR write if PWSR is still ON.
	 */
	ensemble_request_secondary_ppu_off();
	ensemble_force_secondary_ppu_off();

	disable_irq();
	disable_fiq();

	plat_alif_gic_cpuif_disable();
	/* GICD_CTLR: turn the distributor off so SPIs cannot wake us. */
	mmio_write_32(PLAT_ALIF_GICD_BASE, 0U);
	dsb();

	/*
	 * Clear per-core wakeup so neither core is held ON. Do not clear
	 * HOST_CPU_BOOT_MSK for CPU 0: zeroing the boot mask can reset
	 * the core before WFI.
	 */
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP, 0U);

	/*
	 * CHS_PWR_REQ:
	 *   [0]     WAKEUP_EN
	 *   [1]     REFCLK_REQ
	 *   [2]     DBGTOP_PWR_REQ
	 *   [5:3]   SYSTOP_PWR_REQ (APSS cluster / SE PD-9)
	 * Writing 0 drops SYSTOP so the PPU may power-off the cluster.
	 */
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CHS_PWR_REQ, 0U);
	dsb();
	isb();

	VERBOSE("PSCI: CHS_PWR_REQ=0x%x ST=0x%x WAKEUP=0x%x\n",
		mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CHS_PWR_REQ),
		mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CHS_PWR_ST),
		mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP));
	console_flush();

	ensemble_cpu_prepare_ppu_off();
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
