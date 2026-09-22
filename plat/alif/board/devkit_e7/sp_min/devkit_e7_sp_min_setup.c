/*
 * Copyright (c) 2025, Alif Semiconductor. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <plat/alif/common/plat_alif.h>
#include <plat/alif/common/alif_mhu.h>
#include <lib/mmio.h>
#include <common/debug.h>
#include <drivers/delay_timer.h>
#include <plat/alif/common/drivers/pinconf.h>
#include <plat/alif/common/drivers/ospi.h>
#include <plat/alif/common/drivers/ap_memory_hyperram.h>
#include <plat/alif/common/drivers/issi_hyperram.h>
#include <arch_helpers.h>
#include <lib/utils_def.h>

/* Base addresses */
#define LPGPIO_BASE                     0x42002000UL
#define PERIPH_CLK_ENA                  0x4902F03C

/* NPU-HG Registers */
#define NPUHG_CMD                       0x8U
#define NPUHG_RESET                     0xCU

#define POWER_Q_ENABLE                  BIT(3)
#define CLOCK_Q_ENABLE                  BIT(2)
#define PENDING_CSL                     BIT(1)
#define PENDING_CPL                     BIT(0)

/* Clock enable bits */
#define PERIPH_CLK_ENA_OSPI0_CKEN       BIT(0)
#define PERIPH_CLK_ENA_OSPI1_CKEN       BIT(1)

/* ISS HyperRAM configuration */
#define HYPRAM_CONFIG_REG0_VALUE        0x8f1d  /* 64-byte wrap length */

/* External declarations */
extern int init_nor_flash(void);
extern int init_mx_nor_flash_dtr(void);

/**
 * @brief Initialize platform clocks
 */
static void devkit_e7_clocks_init(void)
{
    uint32_t value;

    /* Enable peripheral and APB clocks */
    value = mmio_read_32(EXPMST0_CTRL_REG);
    mmio_write_32(EXPMST0_CTRL_REG, value | 0xC0000000);
    dsb();

    /* Enable UART clock and select source */
    value = mmio_read_32(UART_CTRL_REG);
    mmio_write_32(UART_CTRL_REG, value | 0x0000FFFF);
    dsb();

#if ALIF_SOC_E8
    /* Enable OSPI0 and OSPI1 clock (E8-only register) */
    value = mmio_read_32(PERIPH_CLK_ENA);
    mmio_write_32(PERIPH_CLK_ENA, value | PERIPH_CLK_ENA_OSPI0_CKEN | PERIPH_CLK_ENA_OSPI1_CKEN);
    dsb();
#endif /* ALIF_SOC_E8 */
}

/**
 * @brief Initialize platform pinmux configuration
 */
static void devkit_e7_pinmux_init(void)
{
    /* Configure pinmux for OSPI signals */
    pinconf_set(PORT_1, PIN_0, PINMUX_ALTERNATE_FUNCTION_1, PADCTRL_READ_ENABLE);
    pinconf_set(PORT_1, PIN_1, PINMUX_ALTERNATE_FUNCTION_1, 0);
}

/**
 * @brief Initializes basic platform devices such as clocks, UART, and pinmux.
 */
static void devkit_e7_devices_init(void)
{
    devkit_e7_clocks_init();
    devkit_e7_pinmux_init();
}

#if ISSI_HYPERRAM_EN
/**
 * @brief Configure ISS HyperRAM OSPI pins
 */
static void hyperram_pinmux_config(void)
{
    const struct {
        uint8_t port;
        uint8_t pin;
    } hyperram_pins[] = {
        {PORT_2, PIN_0}, {PORT_2, PIN_1}, {PORT_2, PIN_2}, {PORT_2, PIN_3},
        {PORT_2, PIN_4}, {PORT_2, PIN_5}, {PORT_2, PIN_6}, {PORT_2, PIN_7},
        {PORT_3, PIN_0}, {PORT_3, PIN_1}, {PORT_3, PIN_2}, {PORT_1, PIN_6}
    };

    uint32_t pad_ctrl = PADCTRL_READ_ENABLE | PADCTRL_OUTPUT_DRIVE_STRENGTH_12MA;

    for (size_t i = 0; i < ARRAY_SIZE(hyperram_pins); i++) {
        pinconf_set(hyperram_pins[i].port, hyperram_pins[i].pin,
                   PINMUX_ALTERNATE_FUNCTION_1, pad_ctrl);
    }
}

/**
 * @brief Initialize OSPI controller for ISS HyperRAM
 *
 * @param ospi_cfg Pointer to OSPI configuration structure
 */
static void ospi_controller_init(ospi_cfg_t *ospi_cfg)
{
    ospi_cfg->regs = (ospi_regs_t *)OSPI0_BASE;
    ospi_cfg->aes_regs = (aes_regs_t *)AES0_BASE;
    ospi_cfg->ss = 0;

    ospi_disable(ospi_cfg);

    writel(ospi_cfg, baudr, 0x4);
    writel(ospi_cfg, rx_sample_dly, 0x5);
    writel(ospi_cfg, txd_drive_edge, 0x1);

    ospi_enable(ospi_cfg);
}

/**
 * @brief Configures and initializes the ISS HyperRAM via OSPI interface.
 */
static void iss_hyperram_init(void)
{
    static ospi_cfg_t ospi_cfg;
    int ret;

    /* Configure pinmux for HyperRAM OSPI signals */
    hyperram_pinmux_config();

    /* Initialize OSPI controller */
    ospi_controller_init(&ospi_cfg);

    /* Write HyperRAM configuration register 0 */
    ret = hyper_ram_write_conf_reg0(&ospi_cfg, HYPRAM_CONFIG_REG0_VALUE);
    if (ret != 0) {
        ERROR("HyperRAM config register write failed: %d\n", ret);
        panic();
    }

    /* Initialize XIP mode */
    ret = hyper_ram_xip_init(&ospi_cfg);
    if (ret != 0) {
        ERROR("HyperRAM XIP initialization failed: %d\n", ret);
        panic();
    }

    INFO("ISSI HyperRAM initialized successfully\n");
}
#endif /* ISSI_HYPERRAM_EN */

/**
 * @brief Early platform setup called before secure payload is loaded.
 */
void plat_alif_sp_min_early_platform_setup(u_register_t arg0, u_register_t arg1,
                                         u_register_t arg2, u_register_t arg3)
{
    /* Initialize platform devices */
    devkit_e7_devices_init();

    /* Call common early platform setup code */
    alif_sp_min_early_platform_setup((void *)arg0, arg1, arg2, (void *)arg3);
}

/**
 * @brief Final platform setup after early initialization.
 */
void plat_alif_sp_min_platform_setup(void)
{
#if ISSI_HYPERRAM_EN
    /* Initialize ISS HyperRAM */
    iss_hyperram_init();
#elif AP_HYPERRAM_EN
    /* Initialize AP Memory HyperRAM */
    if (ap_memory_hyperram_init() != 0) {
        ERROR("AP Memory: APS512Mb HyperRAM initialization failed\n");
        panic();
    }
#endif

#if ISSI_FLASH_EN
    /* ISSI: Initialize NOR flash if enabled */
    if (init_nor_flash() != 0) {
	ERROR("ISSI: OSPI1 NOR flash initialization failed\n");
	panic();
	}
#elif MX_FLASH_EN
    /* Macronix: Initialize NOR flash if enabled */
    if (init_mx_nor_flash_dtr() != 0) {
        ERROR("MX: OSPI1 NOR flash initialization failed\n");
        panic();
    }
#endif

#if ALIF_SOC_E8
    /* set the NPU-HG in Non-secure, usermode state (E8-only registers) */
    uint32_t value;

    value = mmio_read_32(NPU_HG_ADDR + NPUHG_RESET);
    value |= PENDING_CSL;
    value &= ~PENDING_CPL;
    mmio_write_32((NPU_HG_ADDR + NPUHG_RESET), value);

    value = mmio_read_32(NPU_HG_ADDR + NPUHG_CMD);
    value |= POWER_Q_ENABLE;
    value |= CLOCK_Q_ENABLE;
    mmio_write_32((NPU_HG_ADDR + NPUHG_CMD), value);

    alif_mhu_init();
#endif /* ALIF_SOC_E8 */
}
