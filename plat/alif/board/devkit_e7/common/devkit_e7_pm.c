/*
 * Copyright (c) 2025, Alif Semiconductor. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <platform_def.h>

#include <arch.h>
#include <arch_helpers.h>
#include <common/debug.h>
#include <drivers/console.h>
#include <drivers/delay_timer.h>
#include <lib/mmio.h>
#include <lib/psci/psci.h>
#include <plat/alif/common/alif_mhu.h>
#include <plat/alif/common/plat_alif.h>
#include <plat/common/platform.h>

#define HOST_BASE_SYS_CTRL		0x1A010000
#define HOST_CPU_BOOT_MSK		0x300
#define HOST_CPU_CLUS_PWR_REQ		0x304
#define HOST_CPU_WAKEUP			0x308
#define HOST_CHS_PWR_REQ		0x400
#define HOST_CHS_PWR_ST			0x404
#define SECONDARY_CPU_GO_FLAG		0x000ADD
/* Multiplier to Calculate the delay */
#define MULT 100
/*
 * Parked secondaries watch this instead of 0xADD. It sends them through
 * the same PPU handshake as a32_pon_poff_test.c so PD-9 can drop.
 */
#define SECONDARY_CLUSTER_OFF_FLAG	0x0000FF
/* Parked CPU1 writes this after leaving WFI so CPU0 can see the wake. */
#define SECONDARY_PPU_OFF_ACK		0x0000F1

/* Per-core PPU (RTL a32_ppu_access_checks / bk4 PON-POFF). */
#define HOST_CPU_CORE_PPU_0_BASE	0x1BC10000
#define HOST_CPU_CORE_PPU_1_BASE	0x1BC20000
#define PPU_PWPR			0x000
#define PPU_PWSR			0x008
/* PWR_DYN_EN | PWR_POLICY=OFF. Default ON policy is 0x108. */
#define PPU_PWPR_OFF_DYN		0x100

extern unsigned int secondary_cpu_flags[PLATFORM_CORE_COUNT];

void __dead2 plat_secondary_cold_boot_setup(void);
void __dead2 ensemble_secondary_ppu_off(void);

static unsigned int ensemble_mpidr_to_cpu(u_register_t mpidr)
{
	return (unsigned int)(mpidr & MPIDR_CPU_MASK);
}

static int ensemble_pwr_domain_on(u_register_t mpidr)
{
	unsigned int cpu = ensemble_mpidr_to_cpu(mpidr);
	unsigned int val;

	assert(cpu > 0U);
	assert(cpu < PLATFORM_CORE_COUNT);

	/* Set the flag so that core jumps to sp_min_warm_boot */
	secondary_cpu_flags[cpu - 1U] = SECONDARY_CPU_GO_FLAG;

	flush_dcache_range((uintptr_t)&secondary_cpu_flags,
			   sizeof(secondary_cpu_flags));

	val = mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_BOOT_MSK);
	val |= (1U << cpu);
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_BOOT_MSK, val);

	val = mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP);
	val |= (1U << cpu);
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP, val);

	dsb();
	return PSCI_E_SUCCESS;
}

static void ensemble_pwr_domain_on_finish(const psci_power_state_t *target_state)
{
	unsigned int cpu = ensemble_mpidr_to_cpu(read_mpidr_el1());

	(void)target_state;

	/*
	 * Drop the go-flag now that this core has left the park loop.
	 * Leaving 0xADD set would make a later CPU_OFF immediately re-enter
	 * the warm path and return to Linux (psci: failed to shutdown).
	 */
	if ((cpu > 0U) && (cpu < PLATFORM_CORE_COUNT)) {
		secondary_cpu_flags[cpu - 1U] = 0U;
		flush_dcache_range((uintptr_t)&secondary_cpu_flags,
				   sizeof(secondary_cpu_flags));
	}

	/* Enable the gic cpu interface */
	plat_alif_gic_pcpu_init();

	/* Program the gic per-cpu distributor or re-distributor interface */
	plat_alif_gic_cpuif_enable();
}

static void ensemble_pwr_domain_off(const psci_power_state_t *target_state)
{
	unsigned int cpu = ensemble_mpidr_to_cpu(read_mpidr_el1());
	unsigned int val;

	(void)target_state;

	/*
	 * PSCI CPU_OFF is only used for secondary hotplug. SYSTEM_OFF of the
	 * last remaining CPU (always CPU 0) must not come through this path:
	 * secondary_cpu_flags is indexed as [cpu - 1].
	 */
	assert(cpu > 0U);
	assert(cpu < PLATFORM_CORE_COUNT);

	plat_alif_gic_cpuif_disable();

	/* Clear go-flag so the core stays parked until the next CPU_ON */
	secondary_cpu_flags[cpu - 1U] = 0U;
	flush_dcache_range((uintptr_t)&secondary_cpu_flags,
			   sizeof(secondary_cpu_flags));

	val = mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP);
	val &= ~(1U << cpu);
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP, val);

	val = mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_BOOT_MSK);
	val &= ~(1U << cpu);
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_BOOT_MSK, val);

	dsb();
}

static void __dead2
ensemble_pwr_domain_pwr_down_wfi(const psci_power_state_t *target_state)
{
	(void)target_state;

	/*
	 * RTL cpu_test_hook() for A32_1: HOST_CPU_WAKEUP=0, then
	 * power_down_cpu_core() / WFI. Do that on PSCI CPU_OFF so
	 * echo 0 > cpu1/online actually drops a32_1 PPU, not a park WFI.
	 * CPU_ON still brings it back with BOOT_MSK + WAKEUP (cold reset
	 * into the 0xADD park loop).
         */
	ensemble_secondary_ppu_off();
}

static int ensemble_validate_ns_entrypoint(uintptr_t entrypoint)
{
	if (entrypoint == 0U) {
		return PSCI_E_INVALID_ADDRESS;
	}

	/* Reject secure BL32 XIP and RW regions */
	if ((entrypoint >= BL32_RO_BASE) && (entrypoint < BL32_RO_LIMIT)) {
		return PSCI_E_INVALID_ADDRESS;
	}

	if ((entrypoint >= BL32_RW_BASE) && (entrypoint < BL32_RW_LIMIT)) {
		return PSCI_E_INVALID_ADDRESS;
	}

	return PSCI_E_SUCCESS;
}

/*
 * Leave intra-cluster coherency. Matches power_down_cpu_core() in
 * a32_pon_poff_test.c: BIC CPUECTLR.SMPEN then DSB/ISB.
 */
static void ensemble_disable_smp(void)
{
	__asm__ volatile(
		"mrrc	p15, 1, r0, r1, c15\n"
		"bic	r0, r0, %0\n"
		"mcrr	p15, 1, r0, r1, c15\n"
		"isb\n"
		"dsb	sy\n"
		:
		: "I" (0x40)
		: "r0", "r1", "memory");
}

/*
 * CPU-side PPU handshake from power_down_cpu_core() in
 * a32_pon_poff_test.c:
 *   1. clean+invalidate caches while SCTLR.C is still set
 *   2. clear SCTLR.C
 *   3. clear CPUECTLR.SMPEN and set DBGOSDLR.DLK
 *
 * After this, WFI lets the core PPU cut power.
 */
static void ensemble_cpu_prepare_ppu_off(void)
{
	dcsw_op_all(DC_OP_CISW);
	dsb();
	isb();

	write_sctlr(read_sctlr() & ~SCTLR_C_BIT);
	isb();
	dsb();

	write_dbgosdlr(read_dbgosdlr() | 0x1U);
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

/*
 * Last step of a parked secondary during SYSTEM_OFF. Hotplug CPU_OFF still
 * uses the 0xADD park loop; only ensemble_system_off posts 0xFF.
 */
void __dead2 ensemble_secondary_ppu_off(void)
{
	unsigned int cpu = ensemble_mpidr_to_cpu(read_mpidr_el1());
	unsigned int val;

	if ((cpu > 0U) && (cpu < PLATFORM_CORE_COUNT)) {
		secondary_cpu_flags[cpu - 1U] = SECONDARY_PPU_OFF_ACK;
		flush_dcache_range((uintptr_t)&secondary_cpu_flags,
				   sizeof(secondary_cpu_flags));
		dsb();
	}

	/* No INFO() here: SMPEN is already clear in the park loop. */

	disable_irq();
	disable_fiq();
	plat_alif_gic_cpuif_disable();

	/*
	 * RTL cpu_test_hook(): HOST_CPU_WAKEUP.W = 0 before WFI.
	 * A set wakeup bit holds this core's PPU ON.
	 */
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP, 0U);
	if ((cpu > 0U) && (cpu < PLATFORM_CORE_COUNT)) {
		val = mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_BOOT_MSK);
		val &= ~(1U << cpu);
		mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_BOOT_MSK, val);
	}

	/* Local PWPR write — a write from the other core is ignored (PWSR stays 0x108). */
	mmio_write_32(HOST_CPU_CORE_PPU_0_BASE + (cpu * 0x10000U) + PPU_PWPR,
		      PPU_PWPR_OFF_DYN);
	dsb();

	ensemble_cpu_prepare_ppu_off();
	ensemble_wfi_forever();
}

/**
 * @brief Microsecond delay
 */
static void mx_delay_us(uint32_t us)
{
    volatile uint32_t count = us * MULT;
    while (count--) {
        __asm__ volatile("nop");
    }
}

static void ensemble_request_secondary_ppu_off(void)
{
	unsigned int i;
	unsigned int val;
	unsigned int flag = 0U;
	uint32_t pwsr = 0U;

	pwsr = mmio_read_32(HOST_CPU_CORE_PPU_1_BASE + PPU_PWSR);
	if ((pwsr & 0xFU) == 0U) {
		INFO("PSCI: CORE1 already OFF (PWSR=0x%x), skip wake\n", pwsr);
		return;
	}

	for (i = 0U; i < (PLATFORM_CORE_COUNT - 1U); i++) {
		secondary_cpu_flags[i] = SECONDARY_CLUSTER_OFF_FLAG;
	}
	flush_dcache_range((uintptr_t)&secondary_cpu_flags,
			   sizeof(secondary_cpu_flags));
	dsb();

	/*
	 * Parked cores wait in WFI. SEV does not wake WFI — that is why
	 * the previous 0xFF+SEV path left CORE1 PWSR at 0x108.
	 * PSCI CPU_ON (echo 1 > cpu1/online) releases them with BOOT_MSK
	 * and HOST_CPU_WAKEUP; use the same wake, with flag 0xFF.
	 */
	val = mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_BOOT_MSK);
	val |= (1U << 1);
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_BOOT_MSK, val);

	val = mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP);
	val |= (1U << 1);
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP, val);
	dsb();
	sev();

	INFO("PSCI: woke CPU1 for PPU-off BOOT_MSK=0x%x WAKEUP=0x%x\n",
	     mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_BOOT_MSK),
	     mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP));

	for (i = 0U; i < 1000U; i++) {
		flush_dcache_range((uintptr_t)&secondary_cpu_flags,
				   sizeof(secondary_cpu_flags));
		flag = secondary_cpu_flags[0];
		pwsr = mmio_read_32(HOST_CPU_CORE_PPU_1_BASE + PPU_PWSR);
		if ((pwsr & 0xFU) == 0U) {
			break;
		}
		mx_delay_us(100);
	}

	INFO("PSCI: CPU1 handshake flag=0x%x CORE1_PWSR=0x%x (%u x100us)\n",
	     flag, pwsr, i);
}

/*
 * Fallback only: CPU1 must write its own PWPR. A write from CPU0 is
 * ignored while the target has not done the local handshake (PWSR stays
 * 0x108). Do not clear BOOT_MSK here — that raced the wake pulse.
 */
static void ensemble_force_core1_ppu_off(void)
{
	uint32_t pwsr = 0U;
	unsigned int i;

	/* bit[0] MEM_RET_REQ, bit[1] CLUSTOP PWR_REQ. 0 = allow low power. */
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_CLUS_PWR_REQ, 0U);

	INFO("PSCI: CORE1 PPU PWSR before OFF=0x%x CLUS_PWR_REQ=0x%x\n",
	     mmio_read_32(HOST_CPU_CORE_PPU_1_BASE + PPU_PWSR),
	     mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_CLUS_PWR_REQ));

	mmio_write_32(HOST_CPU_CORE_PPU_1_BASE + PPU_PWPR, PPU_PWPR_OFF_DYN);
	dsb();

	for (i = 0U; i < 100U; i++) {
		pwsr = mmio_read_32(HOST_CPU_CORE_PPU_1_BASE + PPU_PWSR);
		if ((pwsr & 0xFU) == 0U) {
			break;
		}
		mx_delay_us(100);
	}

	INFO("PSCI: CORE1 PPU PWSR after OFF=0x%x (policy nibble %u)\n",
	     pwsr, pwsr & 0xFU);
}

static void __dead2 ensemble_system_off(void)
{
	INFO("PSCI: system off APSS cluster\n");

	/*
	 * Tell M55-HE that A32 is going off and wait a bounded ~500 ms for
	 * A32_GOING_OFF_ACK. MHU0 channel 0 doorbells, same as the Linux/
	 * Zephyr MHU0 samples. Do this before GIC/chassis teardown so the
	 * mailbox is still live. A timeout does not abort SYSTEM_OFF.
	 */
	if (alif_mhu_notify_going_off() != 0) {
		WARN("PSCI: M55-HE did not ACK A32_GOING_OFF; continuing\n");
	}
	alif_mhu_shutdown();

	/*
	 * CPU_OFF parks CPU1 in WFI. Wake it the CPU_ON way, post 0xFF
	 * so it runs the PON/POFF handshake, then fall back to a PWPR write.
	 */
	ensemble_request_secondary_ppu_off();
	ensemble_force_core1_ppu_off();

	disable_irq();
	disable_fiq();

	plat_alif_gic_cpuif_disable();
	/* GICD_CTLR: turn the distributor off so SPIs cannot wake the cluster. */
	mmio_write_32(PLAT_ALIF_GICD_BASE, 0U);
	dsb();

	/*
	 * Clear per-core wakeup so neither core is held ON. Do not clear
	 * HOST_CPU_BOOT_MSK for CPU 0: the working PON/POFF test never
	 * does, and zeroing the boot mask can reset the core before WFI.
	 */
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP, 0U);

	/*
	 * CHS_PWR_REQ @ 0x400 (bk4 power_down_cpu_core):
	 *   [0]     WAKEUP_EN
	 *   [1]     REFCLK_REQ
	 *   [2]     DBGTOP_PWR_REQ
	 *   [5:3]   SYSTOP_PWR_REQ  (APSS cluster / SE PD-9)
	 * Writing 0 drops SYSTOP so the PPU may power-off the cluster.
	 */
	mmio_write_32(HOST_BASE_SYS_CTRL + HOST_CHS_PWR_REQ, 0U);
	dsb();
	isb();

	INFO("PSCI: CHS_PWR_REQ=0x%x ST=0x%x WAKEUP=0x%x CORE1_PWSR=0x%x, WFI\n",
	     mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CHS_PWR_REQ),
	     mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CHS_PWR_ST),
	     mmio_read_32(HOST_BASE_SYS_CTRL + HOST_CPU_WAKEUP),
	     mmio_read_32(HOST_CPU_CORE_PPU_1_BASE + PPU_PWSR));
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
