/*
 * Copyright (c) 2025, Alif Semiconductor. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PLATFORM_DEF_H
#define PLATFORM_DEF_H

#include <lib/utils_def.h>
#include <lib/xlat_tables/xlat_tables_defs.h>
#include <plat/common/common_def.h>

/* Core/Cluster/Thread counts for Devkit e7 */
#define DEVKIT_E7_CLUSTER_COUNT		U(1)
#define DEVKIT_E7_MAX_CPUS_PER_CLUSTER	U(2)
#define DEVKIT_E7_MAX_PE_PER_CPU	U(1)

#define PLAT_ALIF_CLUSTER_COUNT		DEVKIT_E7_CLUSTER_COUNT

#define PLATFORM_CORE_COUNT		(PLAT_ALIF_CLUSTER_COUNT *       \
					DEVKIT_E7_MAX_CPUS_PER_CLUSTER *   \
					DEVKIT_E7_MAX_PE_PER_CPU)

/*
 * Per-secondary mailbox words in secondary_cpu_flags[cpu - 1].
 * CPU_ON posts GO so the parked core takes the SP_MIN warm path.
 * SYSTEM_OFF posts CLUSTER_OFF so a parked core runs the local PPU-off
 * sequence and writes ACK for the primary to observe.
 */
#define SECONDARY_CPU_GO_FLAG		0x000ADD
#define SECONDARY_CLUSTER_OFF_FLAG	0x0000FF
#define SECONDARY_PPU_OFF_ACK		0x0000F1

#define BL32_BASE			ALIF_TRUSTED_SRAM_BASE
#define BL32_LIMIT			UL(BL32_BASE + ALIF_TRUSTED_SRAM_SIZE)

#define BL32_IN_XIP_MEM			1
#define BL32_RO_BASE			ALIF_BL32_XIP_BASE
#define BL32_RO_LIMIT			UL(BL32_RO_BASE + ALIF_BL32_XIP_SIZE)
#define BL32_RW_BASE			BL32_BASE
#define BL32_RW_LIMIT			BL32_LIMIT
/*
 * Some data must be aligned on the biggest cache line size in the platform.
 * This is known only to the platform as it might have a combination of
 * integrated and external caches.
 */
#define CACHE_WRITEBACK_GRANULE		(U(1) << ARM_CACHE_WRITEBACK_SHIFT)
#define ARM_CACHE_WRITEBACK_SHIFT	6

/* Timer/watchdog related constants */
#define ARM_SYS_CNTCTL_BASE		UL(0x1A200000)
#define ARM_SYS_CNTREAD_BASE		UL(0x1A210000)
#define ARM_SYS_TIMCTL_BASE		UL(0x1A220000)

#define OSPI0_BASE_ADDR                (0x83000000)
#define OSPI0_SIZE                     (0x1000)
#define OSPI0_MAP_DEVICE               MAP_REGION_FLAT(                \
                                               OSPI0_BASE_ADDR,        \
                                               OSPI0_SIZE,             \
                                               MT_DEVICE | MT_RW | MT_SECURE)

#define LPGPIO_MAP_DEVICE              MAP_REGION_FLAT(                \
                                               0x42002000,             \
                                               0x1000,                 \
                                               MT_DEVICE | MT_RW | MT_SECURE)


#define AES0_BASE_ADDR                 (0x83001000)
#define AES0_SIZE                      (0x1000)
#define AES0_MAP_DEVICE                        MAP_REGION_FLAT(                \
                                               AES0_BASE_ADDR,         \
                                               AES0_SIZE,              \
                                               MT_DEVICE | MT_RW | MT_SECURE)

#define OSPI1_BASE_ADDR			(0x83002000)
#define OSPI1_SIZE			(0x1000)
#define OSPI1_MAP_DEVICE		MAP_REGION_FLAT(		\
						OSPI1_BASE_ADDR,	\
						OSPI1_SIZE,		\
						MT_DEVICE | MT_RW | MT_SECURE)

#define AES1_BASE_ADDR			(0x83003000)
#define AES1_SIZE			(0x1000)
#define AES1_MAP_DEVICE			MAP_REGION_FLAT(		\
						AES1_BASE_ADDR,		\
						AES1_SIZE,		\
						MT_DEVICE | MT_RW | MT_SECURE)

#define SE_MHU0_SEND_ADDR               (0x1B800000)
#define MHU0_SIZE                       (0x1000)
#define SE_MHU0_SEND_DEVICE             MAP_REGION_FLAT(		\
						SE_MHU0_SEND_ADDR,	\
						MHU0_SIZE,		\
						MT_DEVICE | MT_RW | MT_SECURE)

#define SE_MHU0_RECV_ADDR               (0x1B810000)
#define SE_MHU0_RECV_DEVICE             MAP_REGION_FLAT(		\
						SE_MHU0_RECV_ADDR,	\
						MHU0_SIZE,              \
						MT_DEVICE | MT_RW | MT_SECURE)

#define NPU_HG_ADDR			(0x49042000)
#define NPUHG_SIZE                      (0x2000)
#define NPU_HG_MAP_DEVICE		MAP_REGION_FLAT(		\
						NPU_HG_ADDR,		\
						NPUHG_SIZE,		\
						MT_DEVICE | MT_RW | MT_SECURE)
/* SRAM0 memory 0x02380000 - 0x02380FFF is used for MHU0 */
/* communication with SE.*/
#define MHU0_PAYLOAD_ADDR                       0x02380000
#define MHU0_PAYLOAD_MAP                MAP_REGION_FLAT(		\
						MHU0_PAYLOAD_ADDR,	\
						0x1000,			\
						MT_DEVICE | MT_RW | MT_SECURE)

#define CLKCTL_SLV_ADDR			(0x4902F000)
#define CLKCTL_SIZE			(0x1000)
#define CLKCTL_SLV_MAP_DEVICE		MAP_REGION_FLAT(		\
						CLKCTL_SLV_ADDR,		\
						CLKCTL_SIZE,		\
						MT_DEVICE | MT_RW | MT_SECURE)
/* GIC related constants */
#define PLAT_ALIF_GICD_BASE		UL(0x1C010000)
#define PLAT_ALIF_GICC_BASE		UL(0x1C02F000)

/*
 * The max number of regions like RO(code), coherent and data required by
 * different BL stages which need to be mapped in the MMU.
 */
#define ARM_BL_REGIONS			3
#define PLAT_ARM_MMAP_ENTRIES		18
#define MAX_XLAT_TABLES			13
#define MAX_MMAP_REGIONS		(PLAT_ARM_MMAP_ENTRIES +        \
					ARM_BL_REGIONS)

#define PLAT_PHY_ADDR_SPACE_SIZE	(1ULL << 32)
#define PLAT_VIRT_ADDR_SPACE_SIZE	(1ULL << 32)

/*
 * This macro defines the deepest retention state possible. A higher state
 * ID will represent an invalid or a power down state.
 */
#define PLAT_MAX_RET_STATE		1

/*
 * This macro defines the deepest power down states possible. Any state ID
 * higher than this is invalid.
 */
#define PLAT_MAX_OFF_STATE		2

#define PLAT_MAX_PWR_LVL		2

#define DEVKIT_E7_IRQ_TZ_WDOG		32
#define DEVKIT_E7_IRQ_SEC_SYS_TIMER	34

#define ARM_IRQ_SEC_PHY_TIMER		29

#define ARM_IRQ_SEC_SGI_0		8
#define ARM_IRQ_SEC_SGI_1		9
#define ARM_IRQ_SEC_SGI_2		10
#define ARM_IRQ_SEC_SGI_3		11
#define ARM_IRQ_SEC_SGI_4		12
#define ARM_IRQ_SEC_SGI_5		13
#define ARM_IRQ_SEC_SGI_6		14
#define ARM_IRQ_SEC_SGI_7		15

/*
 * Define a list of Group 0 interrupts.
 */
#define PLAT_ALIF_G0_IRQ_PROPS(grp)   	\
	INTR_PROP_DESC(ARM_IRQ_SEC_PHY_TIMER, GIC_HIGHEST_SEC_PRIORITY, \
		(grp), GIC_INTR_CFG_LEVEL), \
	INTR_PROP_DESC(ARM_IRQ_SEC_SGI_1, GIC_HIGHEST_SEC_PRIORITY,	\
		(grp), GIC_INTR_CFG_EDGE), \
	INTR_PROP_DESC(ARM_IRQ_SEC_SGI_2, GIC_HIGHEST_SEC_PRIORITY,	\
		(grp), GIC_INTR_CFG_EDGE), \
	INTR_PROP_DESC(ARM_IRQ_SEC_SGI_3, GIC_HIGHEST_SEC_PRIORITY,	\
		(grp), GIC_INTR_CFG_EDGE), \
	INTR_PROP_DESC(ARM_IRQ_SEC_SGI_4, GIC_HIGHEST_SEC_PRIORITY,	\
		(grp), GIC_INTR_CFG_EDGE), \
	INTR_PROP_DESC(ARM_IRQ_SEC_SGI_5, GIC_HIGHEST_SEC_PRIORITY,	\
		(grp), GIC_INTR_CFG_EDGE), \
	INTR_PROP_DESC(ARM_IRQ_SEC_SGI_6, GIC_HIGHEST_SEC_PRIORITY,	\
		(grp), GIC_INTR_CFG_EDGE), \
	INTR_PROP_DESC(ARM_IRQ_SEC_SGI_7, GIC_HIGHEST_SEC_PRIORITY,	\
		(grp), GIC_INTR_CFG_EDGE), \
	INTR_PROP_DESC(DEVKIT_E7_IRQ_TZ_WDOG, GIC_HIGHEST_SEC_PRIORITY, \
		(grp), GIC_INTR_CFG_LEVEL), \
	INTR_PROP_DESC(DEVKIT_E7_IRQ_SEC_SYS_TIMER, GIC_HIGHEST_SEC_PRIORITY, \
		(grp), GIC_INTR_CFG_LEVEL)

#define PLATFORM_STACK_SIZE		UL(0x440)

/* platform console parameters */
#define PLAT_ALIF_BOOT_UART_BASE	UL(0x4901A000)
#define PLAT_ALIF_BOOT_UART_CLK_IN_HZ	100000000
#define PLAT_ALIF_CONSOLE_BAUDRATE	115200

#define PLAT_ALIF_RUN_UART_BASE		UL(0x4901A000)
#define PLAT_ALIF_RUN_UART_CLK_IN_HZ	100000000

#define SYS_COUNTER_FREQ_IN_TICKS       UL(100000000) /* 100MHz */

#define ARM_MAP_BL_RO			MAP_REGION_FLAT(		\
						BL_CODE_BASE,		\
						BL_CODE_END		\
							- BL_CODE_BASE,	\
						MT_CODE | MT_SECURE),	\
					MAP_REGION_FLAT(		\
						BL_RO_DATA_BASE,	\
						BL_RO_DATA_END		\
						- BL_RO_DATA_BASE,	\
						MT_RO_DATA | MT_SECURE)

#define DEVKIT_E7_DEVICE_BASE     	(0x1A000000)
#define DEVKIT_E7_DEVICE_SIZE		(0x6000000)
#define DEVKIT_E7_MAP_DEVICE		MAP_REGION_FLAT(                \
                                                DEVKIT_E7_DEVICE_BASE,\
                                                DEVKIT_E7_DEVICE_SIZE,\
                                                MT_DEVICE | MT_RW | MT_SECURE)

#define UART_SIZE			(0x1000)
#define UART_MAP_DEVICE                 MAP_REGION_FLAT(                \
                                                PLAT_ALIF_BOOT_UART_BASE,         \
                                                UART_SIZE,              \
                                                MT_DEVICE | MT_RW | MT_SECURE)
#define EXPMST0_CTRL_REG		(0x4902F000)
#define UART_CTRL_REG			(0x4902F008)
#define PINMUX_BASE			(0x1A603000)
#define LPGPIO_CTRL_BASE		(0x42007000)
#define OSPI0_BASE			(0x83000000)
#define OSPI1_BASE			(0x83002000)
#define AES0_BASE			(0x83001000)
#define AES1_BASE			(0x83003000)

#define OSPI_SIZE			0x4000

#define OSPI_MAP_DEVICE                 MAP_REGION_FLAT(                \
                                                OSPI0_BASE,         	\
                                                OSPI_SIZE,              \
                                                MT_DEVICE | MT_RW | MT_SECURE)

#define GPIO_MAP_DEVICE                 MAP_REGION_FLAT(                \
                                                0x42002000,         	\
                                                0x8000,              	\
                                                MT_DEVICE | MT_RW | MT_SECURE)

#endif /* PLATFORM_DEF_H */
