/*
 * Copyright (c) 2025, Alif Semiconductor. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <platform_def.h>

#include <arch.h>
#include <arch_helpers.h>
#include <lib/mmio.h>
#include <lib/psci/psci.h>
#include <plat/alif/common/plat_alif.h>
#include <plat/common/platform.h>

#define HOST_BASE_SYS_CTRL		0x1A010000
#define HOST_CPU_BOOT_MSK		0x300
#define HOST_CPU_WAKEUP			0x308
#define SECONDARY_CPU_GO_FLAG		0x000ADD

extern unsigned int secondary_cpu_flags[PLATFORM_CORE_COUNT];

void __dead2 plat_secondary_cold_boot_setup(void);

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

	disable_mmu_icache_secure();

	/*
	 * Keep the secure MMU enabled: BL32 is XIP (flash) with RW in SRAM.
	 * Disabling the MMU here can fault and cause PSCI CPU_OFF to return
	 * to Linux, which panics with "psci: cpu N failed to shutdown".
	 */
	plat_secondary_cold_boot_setup();
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
};

const plat_psci_ops_t *plat_alif_psci_override_pm_ops(plat_psci_ops_t *ops)
{
	return ops;
}
