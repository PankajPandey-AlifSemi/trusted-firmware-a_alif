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
#include <lib/mmio.h>
#include <lib/xlat_tables/xlat_tables_compat.h>
#include <services/arm_arch_svc.h>
#include <plat/alif/common/plat_alif.h>
#include <plat/common/platform.h>

uintptr_t plat_get_ns_image_entrypoint(void)
{
	return PRELOADED_BL33_BASE;
}

/*
 * Required by bakery_lock_normal.c when USE_COHERENT_MEM=0.
 * Alif still uses the legacy xlat_tables library, which does not provide this
 * helper (xlat_tables_v2 does).
 */
bool is_dcache_enabled(void)
{
	return (read_sctlr() & SCTLR_C_BIT) != 0U;
}

/*******************************************************************************
 * Returns ALIF platform specific memory map regions.
 ******************************************************************************/
const mmap_region_t *plat_alif_get_mmap(void)
{
	return plat_alif_mmap;
}

/*******************************************************************************
 * Gets SPSR for BL33 entry
 ******************************************************************************/
uint32_t alif_get_spsr_for_bl33_entry(void)
{
	uint32_t spsr = SPSR_MODE32(MODE32_svc, plat_get_ns_image_entrypoint() & 0x1,
			SPSR_E_LITTLE, DISABLE_ALL_EXCEPTIONS);
	return spsr;
}
