/*
 * Copyright (c) 2025, Alif Semiconductor. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file issi_hyperram.c
 * @brief ISSI IS66/67WVH64M8 HyperRAM driver for the OSPI0 HyperBus interface
 *
 * The 512Mb part is a dual-die package of two 256Mb dies, each with its own
 * configuration register. It supports fixed latency only, so every memory
 * access and register read takes twice the CR0 initial latency.
 *
 * The bus runs at 100 MHz from the 400 MHz OSPI core clock. CR0 is written
 * at 16.67 MHz, then the bus is raised to full speed.
 */

#include <plat/alif/common/plat_alif.h>
#include <platform_def.h>
#include <lib/mmio.h>
#include <common/debug.h>
#include <plat/alif/common/drivers/ospi.h>
#include <plat/alif/common/drivers/issi_hyperram.h>
#include <arch_helpers.h>
#include <lib/utils_def.h>
#include <stdint.h>
#include <lib/libc/errno.h>

/* Timeout and retry settings */
#define MAX_RETRY_COUNT				1000000

/* Command-address words CA[47:16]. CA[47] is R/W#, CA[46] selects register
 * space, CA[45] a linear burst, CA[37] the die and CA[24] CR0 rather than
 * ID0 (datasheet Table 5.1). CA[15:0] is zero for all of them.
 */
#define IS66_CA_REG_WRITE			0x60000000U
#define IS66_CA_REG_READ			0xE0000000U
#define IS66_CA_DIE1				BIT(21)
#define IS66_CA_CR0				BIT(8)
#define IS66_CA_LO				0x0U
#define IS66_DIE_COUNT				2U

#define IS66_ID0_MANUFACTURER_MASK		0xFU
#define IS66_ID0_MANUFACTURER_ISSI		0x3U

/* CR0 fields (datasheet Table 5.4). Reserved bits must be written as 1 and
 * CR0[3] must be set on both dies.
 */
#define IS66_CR0_RESERVED_15			BIT(15)
#define IS66_CR0_DRIVE_STRENGTH_SHIFT		12
#define IS66_CR0_DRIVE_STRENGTH_34_OHM		0x0U
#define IS66_CR0_RESERVED_11_8			(0xFU << 8)
#define IS66_CR0_LATENCY_SHIFT			4
#define IS66_CR0_FIXED_LATENCY			BIT(3)
#define IS66_CR0_LEGACY_WRAP			BIT(2)
#define IS66_CR0_BURST_LEN_64B			0x1U

/* 16-bit frames and a 48-bit command-address for every HyperBus transfer. */
#define IS66_OSPI_DFS				16U
#define IS66_OSPI_ADDR_L_48_BIT			0xCU

/* The baud divider is even only: 400 / 4 = 100 MHz on the bus and
 * 400 / 24 = 16.67 MHz while CR0 is written.
 */
#define IS66_OSPI_CORE_CLK_HZ			400000000U
#define IS66_OSPI_SETUP_BAUDR			24U
#define IS66_OSPI_BAUDR				4U

#define IS66_OSPI_BUS_HZ			(IS66_OSPI_CORE_CLK_HZ / IS66_OSPI_BAUDR)
#define IS66_OSPI_SETUP_BUS_HZ			(IS66_OSPI_CORE_CLK_HZ / IS66_OSPI_SETUP_BAUDR)

/* Initial latency in clocks. Datasheet Table 5.4 rates 6 clocks to 166 MHz. */
#define IS66_LATENCY				6U

/* CR0[7:4] encodes 5 to 8 latency clocks as 0 to 3. */
#define IS66_CR0_VALUE				(IS66_CR0_RESERVED_15 |				\
						 (IS66_CR0_DRIVE_STRENGTH_34_OHM <<		\
						  IS66_CR0_DRIVE_STRENGTH_SHIFT) |		\
						 IS66_CR0_RESERVED_11_8 |			\
						 ((IS66_LATENCY - 5U) << IS66_CR0_LATENCY_SHIFT) | \
						 IS66_CR0_FIXED_LATENCY |			\
						 IS66_CR0_LEGACY_WRAP |				\
						 IS66_CR0_BURST_LEN_64B)

/* Fixed latency doubles the initial latency for reads and writes alike. In
 * HyperBus mode the controller wait fields take the latency clock count as
 * is.
 */
#define IS66_WAIT_CYCLES			(2U * IS66_LATENCY)

/* The DDR drive edge applies at baud dividers above 2, so setup and full
 * speed both use it.
 */
#define IS66_TXD_DRIVE_EDGE			1U

/* RWDS strobes the read data, so the controller sample delay stays zero and
 * the AES RXDS tap places the capture.
 */
#define IS66_RX_SAMPLE_DLY			0U

/* AES RXDS taps at the setup and full bus speeds. Board specific. */
#define IS66_AES_RXDS_DELAY_SETUP		11U
#define IS66_AES_RXDS_DELAY			6U

/**
 * @brief Wait until the TX FIFO has drained and the transfer has ended
 *
 * @param ospi Pointer to OSPI configuration
 * @return int 0 on success, -ETIMEDOUT on failure
 */
static int hyper_ram_wait_transfer_complete(ospi_cfg_t *ospi)
{
    uint32_t retry;

    for (retry = 0; retry < MAX_RETRY_COUNT; retry++) {
        if ((readl(ospi, sr) & (SR_TF_EMPTY | SR_BUSY)) == SR_TF_EMPTY) {
            return 0;
        }
    }

    ERROR("ISSI HyperRAM: Transfer timeout\n");
    return -ETIMEDOUT;
}

/**
 * @brief Select the OSPI core clock and run the bus at the setup speed
 *
 * @param ospi Pointer to OSPI configuration
 */
static void hyper_ram_controller_init(ospi_cfg_t *ospi)
{
#if ALIF_SOC_E8
    mmio_clrbits_32(CGU_MISC_CLK_CTRL, CGU_MISC_CLK_CTRL_OSPI_CLK_SEL);
    dsb();
#endif /* ALIF_SOC_E8 */

    ospi_disable(ospi);

    INFO("ISSI HyperRAM: OSPI0 setup at %u MHz (core %u MHz, baud %u)\n",
         IS66_OSPI_SETUP_BUS_HZ / 1000000U, IS66_OSPI_CORE_CLK_HZ / 1000000U,
         IS66_OSPI_SETUP_BAUDR);
    writel(ospi, ser, 0);
    writel(ospi, baudr, IS66_OSPI_SETUP_BAUDR);
    writel(ospi, rx_sample_dly, IS66_RX_SAMPLE_DLY);
    writel(ospi, txd_drive_edge, IS66_TXD_DRIVE_EDGE);
    writel(ospi, txftlr, 0);

    ospi->aes_regs->aes_rxds_delay = IS66_AES_RXDS_DELAY_SETUP;

    ospi_enable(ospi);
}

/**
 * @brief Configure OSPI for a HyperBus register access
 *
 * @param ospi Pointer to OSPI configuration
 * @param tmod SPI_TMOD_TO for a write, SPI_TMOD_RO for a read
 * @param wait_cycles Initial latency clocks before the data word
 */
static void hyper_ram_config_ospi(ospi_cfg_t *ospi, uint32_t tmod, uint32_t wait_cycles)
{
    uint32_t val;

    /* Configure OSPI for OCTAL HyperBus mode */
    val = BIT(OSPI_CTRLR0_SPI_HE_OFFSET) |
          (SPI_OCTAL << OSPI_CTRLR0_SPI_FRF_OFFSET) |
          (tmod << OSPI_CTRLR0_TMOD_OFFSET) |
          ((IS66_OSPI_DFS - 1U) << OSPI_CTRLR0_DFS_OFFSET);

    writel(ospi, ctrlr0, val);

    /* A read returns a single 16-bit register word */
    writel(ospi, ctrlr1, 0);

    val = OSPI_TRANS_TYPE_FRF_DEFINED |
          (IS66_OSPI_ADDR_L_48_BIT << OSPI_SPI_CTRLR0_ADDR_L_OFFSET) |
          (0 << OSPI_SPI_CTRLR0_INST_L_OFFSET) |
          (wait_cycles << OSPI_SPI_CTRLR0_WAIT_CYCLES_OFFSET) |
          BIT(OSPI_SPI_CTRLR0_SPI_DDR_EN_OFFSET) |
          (0 << OSPI_SPI_CTRLR0_SPI_DM_EN_OFFSET) |
          (0 << OSPI_SPI_CTRLR0_SPI_RXDS_SIG_EN_OFFSET);

    if (tmod == SPI_TMOD_RO) {
        val |= BIT(OSPI_SPI_CTRLR0_SPI_RXDS_EN_OFFSET);
    }

    writel(ospi, spi_ctrlr0, val);
}

/**
 * @brief Write a 16-bit HyperRAM register
 *
 * Register writes have zero latency, so they work at any CR0 setting.
 *
 * @param ospi Pointer to OSPI configuration
 * @param ca_hi Command-address CA[47:16] of the register
 * @param data 16-bit value to write
 * @return int 0 on success, negative error code on failure
 */
static int hyper_ram_write_reg(ospi_cfg_t *ospi, uint32_t ca_hi, uint16_t data)
{
    int ret;

    ospi_disable(ospi);
    writel(ospi, ser, 0);
    hyper_ram_config_ospi(ospi, SPI_TMOD_TO, 0);
    ospi_enable(ospi);

    /* The transfer starts once the slave is selected */
    writel(ospi, datareg, ca_hi);
    writel(ospi, datareg, IS66_CA_LO);
    writel(ospi, datareg, data);
    writel(ospi, ser, BIT(ospi->ss));

    ret = hyper_ram_wait_transfer_complete(ospi);
    writel(ospi, ser, 0);

    if (ret != 0) {
        ERROR("ISSI HyperRAM: Failed to write register 0x%x\n", ca_hi);
    }

    return ret;
}

/**
 * @brief Read a 16-bit HyperRAM register at the programmed CR0 latency
 *
 * @param ospi Pointer to OSPI configuration
 * @param ca_hi Command-address CA[47:16] of the register
 * @param data Register value
 * @return int 0 on success, -ETIMEDOUT on failure
 */
static int hyper_ram_read_reg(ospi_cfg_t *ospi, uint32_t ca_hi, uint16_t *data)
{
    uint32_t retry;
    int ret = -ETIMEDOUT;

    ospi_disable(ospi);
    writel(ospi, ser, 0);
    hyper_ram_config_ospi(ospi, SPI_TMOD_RO, IS66_WAIT_CYCLES);
    ospi_enable(ospi);

    writel(ospi, datareg, ca_hi);
    writel(ospi, datareg, IS66_CA_LO);
    writel(ospi, ser, BIT(ospi->ss));

    for (retry = 0; retry < MAX_RETRY_COUNT; retry++) {
        if (readl(ospi, rxflr) != 0U) {
            *data = (uint16_t)readl(ospi, datareg);
            ret = 0;
            break;
        }
    }

    if (hyper_ram_wait_transfer_complete(ospi) != 0) {
        ret = -ETIMEDOUT;
    }
    writel(ospi, ser, 0);

    return ret;
}

/**
 * @brief Log one die's ID0 and CR0 and warn if CR0 did not take
 *
 * @param ospi Pointer to OSPI configuration
 * @param die Die index
 */
static void hyper_ram_check_die(ospi_cfg_t *ospi, uint32_t die)
{
    uint32_t die_ca = (die != 0U) ? IS66_CA_DIE1 : 0U;
    uint16_t id0 = 0U;
    uint16_t cr0 = 0U;

    if ((hyper_ram_read_reg(ospi, IS66_CA_REG_READ | die_ca, &id0) != 0) ||
        (hyper_ram_read_reg(ospi, IS66_CA_REG_READ | die_ca | IS66_CA_CR0, &cr0) != 0)) {
        WARN("ISSI HyperRAM: die %u register read timed out\n", die);
        return;
    }

    INFO("ISSI HyperRAM: die %u ID0 = 0x%04x, CR0 = 0x%04x\n", die, id0, cr0);

    if (((id0 & IS66_ID0_MANUFACTURER_MASK) != IS66_ID0_MANUFACTURER_ISSI) ||
        (cr0 != IS66_CR0_VALUE)) {
        WARN("ISSI HyperRAM: die %u expected ISSI ID0 and CR0 = 0x%04x\n",
             die, IS66_CR0_VALUE);
    }
}

/**
 * @brief Raise the bus to IS66_OSPI_BUS_HZ and set the AES RXDS delay
 *
 * @param ospi Pointer to OSPI configuration
 */
static void hyper_ram_set_full_speed(ospi_cfg_t *ospi)
{
    ospi_disable(ospi);

    INFO("ISSI HyperRAM: OSPI0 %u MHz (core %u MHz, baud %u)\n",
         IS66_OSPI_BUS_HZ / 1000000U, IS66_OSPI_CORE_CLK_HZ / 1000000U,
         IS66_OSPI_BAUDR);
    writel(ospi, baudr, IS66_OSPI_BAUDR);
    writel(ospi, rx_sample_dly, IS66_RX_SAMPLE_DLY);
    writel(ospi, txd_drive_edge, IS66_TXD_DRIVE_EDGE);

    ospi->aes_regs->aes_rxds_delay = IS66_AES_RXDS_DELAY;

    ospi_enable(ospi);
}

/**
 * @brief Put OSPI0 in HyperBus XIP mode
 *
 * @param ospi Pointer to OSPI configuration
 */
static void hyper_ram_xip_init(ospi_cfg_t *ospi)
{
    uint32_t val;

    ospi_disable(ospi);

    val = (SPI_OCTAL << OSPI_CTRLR0_SPI_FRF_OFFSET) |
          (SPI_TMOD_RO << OSPI_CTRLR0_TMOD_OFFSET) |
          ((IS66_OSPI_DFS - 1U) << OSPI_CTRLR0_DFS_OFFSET);

    writel(ospi, ctrlr0, val);

    /* The fixed latency doubling is in the wait count, so RWDS latency
     * signaling stays off for reads and writes.
     */
    val = (OSPI_TRANS_TYPE_FRF_DEFINED << OSPI_XIP_CTRL_TRANS_TYPE_OFFSET) |
          (IS66_WAIT_CYCLES << OSPI_XIP_CTRL_WAIT_CYCLES_OFFSET) |
          BIT(OSPI_XIP_CTRL_XIP_HYPERBUS_EN_OFFSET) |
          BIT(OSPI_XIP_CTRL_DFS_HC_OFFSET);

    writel(ospi, xip_ctrl, val);

    val = (OSPI_TRANS_TYPE_FRF_DEFINED << OSPI_XIP_WR_CTRL_WR_TRANS_TYPE) |
          (IS66_WAIT_CYCLES << OSPI_XIP_WR_CTRL_WAIT_CYCLES) |
          BIT(OSPI_XIP_WR_CTRL_WR_DM_EN) |
          BIT(OSPI_XIP_WR_CTRL_WR_HYPERBUS_EN) |
          BIT(OSPI_XIP_WR_CTRL_DFS_HC);

    writel(ospi, xip_write_ctrl, val);

    /* XIP starts from a cleared TXFTLR. E8 has no XIP_SER register. */
    writel(ospi, txftlr, 0);
    writel(ospi, rx_sample_dly, IS66_RX_SAMPLE_DLY);
    writel(ospi, xip_mode_bits, 0);
    writel(ospi, ser, BIT(ospi->ss));

    ospi_enable(ospi);

    ospi->aes_regs->aes_control |= AES_CONTROL_XIP_EN;
}

/**
 * @brief Initialize the ISSI HyperRAM on OSPI0 and enter XIP mode
 *
 * @return int 0 on success, negative error code on failure
 */
int issi_hyperram_init(void)
{
    static ospi_cfg_t ospi_cfg;
    ospi_cfg_t *ospi = &ospi_cfg;
    uint32_t die;
    int ret;

    ospi->regs = (ospi_regs_t *)OSPI0_BASE;
    ospi->aes_regs = (aes_regs_t *)AES0_BASE;
    ospi->ss = 0;

    hyper_ram_controller_init(ospi);

    INFO("ISSI HyperRAM: bus %u MHz, fixed latency %u, CR0 = 0x%04x, XIP wait %u\n",
         IS66_OSPI_BUS_HZ / 1000000U, IS66_LATENCY, IS66_CR0_VALUE, IS66_WAIT_CYCLES);

    for (die = 0; die < IS66_DIE_COUNT; die++) {
        ret = hyper_ram_write_reg(ospi,
                                  IS66_CA_REG_WRITE | IS66_CA_CR0 |
                                  ((die != 0U) ? IS66_CA_DIE1 : 0U),
                                  IS66_CR0_VALUE);
        if (ret != 0) {
            return ret;
        }
    }

    for (die = 0; die < IS66_DIE_COUNT; die++) {
        hyper_ram_check_die(ospi, die);
    }

    /* Both dies now carry the latency for the full bus speed. */
    hyper_ram_set_full_speed(ospi);

    hyper_ram_xip_init(ospi);
    INFO("ISSI HyperRAM: XIP mode enabled\n");

    return 0;
}
