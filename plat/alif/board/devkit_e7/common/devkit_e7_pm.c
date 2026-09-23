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
#include <lib/mmio.h>
#include <lib/psci/psci.h>
#include <plat/alif/common/plat_alif.h>

#define HOST_BASE_SYS_CTRL		UL(0x1A010000)
#define HOST_CPU_BOOT_MSK		UL(0x300)
#define HOST_CPU_WAKEUP			UL(0x308)

extern unsigned int secondary_cpu_flags[PLATFORM_CORE_COUNT];
void __dead2 plat_secondary_cold_boot_setup(void);

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

	plat_secondary_cold_boot_setup();
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

/*******************************************************************************
 * Export the platform handlers via plat_alif_psci_pm_ops. The ALIF Standard
 * platform layer will take care of registering the handlers with PSCI.
 ******************************************************************************/
plat_psci_ops_t plat_alif_psci_pm_ops = {
	.pwr_domain_on = ensemble_pwr_domain_on,
	.pwr_domain_on_finish = ensemble_pwr_domain_on_finish,
	.pwr_domain_off = ensemble_pwr_domain_off,
	.pwr_domain_pwr_down_wfi = ensemble_pwr_domain_pwr_down_wfi,
	.validate_ns_entrypoint = ensemble_validate_ns_entrypoint
};

const plat_psci_ops_t *plat_alif_psci_override_pm_ops(plat_psci_ops_t *ops)
{
	return ops;
}
