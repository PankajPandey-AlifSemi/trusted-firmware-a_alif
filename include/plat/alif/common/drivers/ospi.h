/*
 * Copyright (c) 2025, Alif Semiconductor. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef OSPI_H
#define OSPI_H

#ifdef  __cplusplus
extern "C"
{
#endif

#include <stdint.h>

typedef struct {
	volatile uint32_t ctrlr0; 		/* SPI Control Register 0 (0x0) */
	volatile uint32_t ctrlr1; 		/* SPI Control Register 1 (0x4) */
	volatile uint32_t ssienr; 		/* SPI Enable Register  (0x8) */
	volatile uint32_t mwcr;   		/* SPI Microwire Control Register (0xC) */
	volatile uint32_t ser;    		/* SPI Slave Enable Register (0x10) */
	volatile uint32_t baudr;  		/* SPI Baud Rate Select Register (0x14) */
	volatile uint32_t txftlr; 		/* SPI Transmit FIFO Threshold Level Register (0x18) */
	volatile uint32_t rxftlr; 		/* SPI Receive  FIFO Threshold Level Register (0x1C) */
	volatile uint32_t txflr;  		/* SPI Transmit FIFO Level Register  (0x20)*/
	volatile uint32_t rxflr;  		/* SPI Receive  FIFO Level Register  (0x24)*/
	volatile uint32_t sr;     		/* SPI Status   Register  (0x28) */
	volatile uint32_t imr;			/* SPI Interrupt Mask Register  (0x2C) */
	volatile uint32_t isr;			/* SPI Interrupt Status Register  (0x30) */
	volatile uint32_t risr;			/* SPI Raw Interrupt Status Register (0x34)*/
	volatile uint32_t txoicr;		/* SPI Transmit FIFO Overflow Interrupt Clear Register  (0x38) */
	volatile uint32_t rxoicr;		/* SPI Receive  FIFO Overflow Interrupt Clear Register  (0x3C) */
	volatile uint32_t rxuicr; 		/* SPI Receive FIFO Underflow Interrupt Clear Register  (0x40) */
	volatile uint32_t msticr;		/* SPI Multi-Master Interrupt Clear Register (0x44) */
	volatile uint32_t icr;			/* SPI Interrupt Clear Register  (0x48) */
	volatile uint32_t dmacr;		/* DMA Control Register  (0x4C) */
	volatile uint32_t dmatxdlr;   		/* DMA Transmit Data Level  (0x50) */
	volatile uint32_t dmarxdlr;   		/* DMA Receive Data Level  (0x54) */
	volatile uint32_t spi_idr;    		/* SPI Identification Register  (0x58) */
	volatile uint32_t spi_ver_id; 		/* Synopsys component version (0x5C) */
	volatile uint32_t datareg;		/* SPI DATA Register for both Read and Write  (0x60) */
	volatile uint32_t drs[35];		/* SPI DATA Register for both Read and Write  (0x64-0xEC) */
	volatile uint32_t rx_sample_dly;	/* Rx Sample Delay Register (0xF0) */
	volatile uint32_t spi_ctrlr0;		/* SPI Control Register 0 (0xF4) */
	volatile uint32_t txd_drive_edge;	/* Transmit Drive Edge Register (0xF8) */
	volatile uint32_t xip_mode_bits;	/* eXecute in Place - Mode bits (0xFC) */
	volatile uint32_t xip_incr_inst;	/* XiP INCR transfer opcode (0x100) */
	volatile uint32_t xip_wrap_inst;	/* XiP WRAP transfer opcode (0x104) */
	volatile uint32_t xip_ctrl;		/* XiP Control Register (0x108) */
	volatile uint32_t xip_ser;		/* XiP Slave Enable Register (0x10C) */
	volatile uint32_t xip_xrxoicr;		/* XiP Receive FIFO Overflow Interrupt Clear Register (0x110) */
	volatile uint32_t xip_cnt_time_out; 	/* XiP time out register for continuous transfers (0x114) */
	volatile uint32_t spi_ctrlr1;		/* SPI Control Register 1 (0x118) */
	volatile uint32_t spitecr;		/* SPI Transmit Error Interrupt Clear Register (0x11C) */
	volatile uint32_t spidr;		/* SPI Device Register (0x120) */
	volatile uint32_t spiar;		/* SPI Device Address Register (0x124) */
	volatile uint32_t axiar0;		/* AXI Address Register 0 (0x128) */
	volatile uint32_t axiar1;		/* AXI Address Register 1 (0x12C) */
	volatile uint32_t axiecr;		/* AXI Master Error Interrupt Clear Register (0x130) */
	volatile uint32_t donecr;		/* Transfer Done Clear Interrupt Clear Register (0x134) */
	volatile uint32_t rsvd[2];
	volatile uint32_t xip_write_incr_inst;	/* XiP Write INCR transfer opcode (0x140) */
	volatile uint32_t xip_write_wrap_inst;	/* XiP Write WRAP transfer opcode (0x144) */
	volatile uint32_t xip_write_ctrl;	/* XiP Write Control Register (0x148) */
} ospi_regs_t;

typedef struct {
	volatile uint32_t  aes_control;		/* AES Control Register (0x0)			*/
	volatile uint32_t  aes_interrupt;	/* AES Interrupt Control Register (0x4)		*/
	volatile uint32_t  aes_interrupt_mask;  /* AES Interrupt Mask Register (0x8)		*/
	volatile uint32_t  aes_clk_dis;		/* AES Clock Disable Register (0xC) - E8 only	*/
	volatile uint32_t  aes_addr_control;	/* AES Address Control Register (0x10) - E8 only */
	volatile uint32_t  reserved[3];		/* Reserved (0x14, 0x18, 0x1C)			*/
	volatile uint32_t  aes_rxds_delay;	/* AES RXDS Delay Register (0x20)		*/
	volatile uint32_t  reserved1[7];	/* Reserved (0x24-0x3C)				*/
	volatile uint32_t  aes_rxd_delay_0;	/* RXD[3:0] delay (0x40)			*/
	volatile uint32_t  aes_rxd_delay_1;	/* RXD[7:4] delay (0x44)			*/
	volatile uint32_t  aes_rxd_delay_2;	/* RXD[11:8] delay (0x48)			*/
	volatile uint32_t  aes_rxd_delay_3;	/* RXD[15:12] delay (0x4C)			*/
	volatile uint32_t  aes_txd_delay_0;	/* TXD[3:0] delay (0x50)			*/
	volatile uint32_t  aes_txd_delay_1;	/* TXD[7:4] delay (0x54)			*/
	volatile uint32_t  aes_txd_delay_2;	/* TXD[11:8] delay (0x58)			*/
	volatile uint32_t  aes_txd_delay_3;	/* TXD[15:12] delay (0x5C)			*/
	volatile uint32_t  aes_ssi_oe_n_delay_0; /* SSI_OE_N[3:0] delay (0x60)		*/
	volatile uint32_t  aes_ssi_oe_n_delay_1; /* SSI_OE_N[7:4] delay (0x64)		*/
	volatile uint32_t  aes_ssi_oe_n_delay_2; /* SSI_OE_N[11:8] delay (0x68)		*/
	volatile uint32_t  aes_ssi_oe_n_delay_3; /* SSI_OE_N[15:12] delay (0x6C)		*/
	volatile uint32_t  aes_ss_n_delay;	/* SS_N delay (0x70)				*/
	volatile uint32_t  aes_txd_dm_delay;	/* TXD_DM and DM_OE_N delay (0x74)		*/
	volatile uint32_t  aes_sclk_delay;	/* SCLK and SCLKN delay (0x78)			*/
} aes_regs_t;

/* Bit fields in CTRLR0 based on DWC_ssi_databook.pdf v1.02a */
#define OSPI_CTRLR0_SPI_HE_OFFSET			24
#define OSPI_CTRLR0_SPI_FRF_OFFSET			22
#define OSPI_CTRLR0_CFS_OFFSET				16
#define OSPI_CTRLR0_SSTE_OFFSET				14
#define OSPI_CTRLR0_SRL_OFFSET				13
#define OSPI_CTRLR0_SLV_OE_OFFSET			12
#define OSPI_CTRLR0_TMOD_OFFSET				10
#define OSPI_CTRLR0_TMOD_MASK				(3 << OSPI_CTRLR0_TMOD_OFFSET)
#define OSPI_CTRLR0_SCPOL_OFFSET			9
#define OSPI_CTRLR0_SCPH_OFFSET				8
#define OSPI_CTRLR0_FRF_OFFSET				6
#define OSPI_CTRLR0_DFS_OFFSET				0

#define OSPI_CTRLR0_IS_MST				0x80000000

#define	SPI_TMOD_TR					0
#define SPI_TMOD_TO					1
#define SPI_TMOD_RO					2
#define SPI_TMOD_EPROMREAD				3

/* Bit fields for SPI FRF */
#define SPI_SINGLE					0
#define SPI_DUAL					1
#define SPI_QUAD					2
#define SPI_OCTAL					3

/* Bit fields for Frame Format FRF */
#define SPI_FRF_SPI					0
#define SPI_FRF_SSP					1
#define SPI_FRF_MICROWIRE				2

/* Bit fields in SPI_CTRLR0 */
#define OSPI_SPI_CTRLR0_CLK_STRETCH_EN_OFFSET 		30
#define OSPI_SPI_CTRLR0_XIP_PREFETCH_EN_OFFSET 		29
#define OSPI_SPI_CTRLR0_XIP_MBL_OFFSET 			26
#define OSPI_SPI_CTRLR0_SPI_RXDS_SIG_EN_OFFSET 		25
#define OSPI_SPI_CTRLR0_SPI_DM_EN_OFFSET 		24
#define OSPI_SPI_CTRLR0_XIP_CONT_EN_OFFSET 		21
#define OSPI_SPI_CTRLR0_XIP_INST_EN_OFFSET 		20
#define OSPI_SPI_CTRLR0_XIP_DFS_HC_OFFSET 		19
#define OSPI_SPI_CTRLR0_SPI_RXDS_EN_OFFSET 		18
#define OSPI_SPI_CTRLR0_INST_DDR_EN_OFFSET		17
#define OSPI_SPI_CTRLR0_SPI_DDR_EN_OFFSET 		16
#define OSPI_SPI_CTRLR0_WAIT_CYCLES_OFFSET 		11
#define OSPI_SPI_CTRLR0_INST_L_OFFSET 			8
#define OSPI_SPI_CTRLR0_XIP_MD_EN_OFFSET 		7
#define OSPI_SPI_CTRLR0_ADDR_L_OFFSET 			2
#define OSPI_SPI_CTRLR0_TRANS_TYPE_OFFSET 		0

#define OSPI_SPI_CTRLR0_TRANS_TYPE_MASK			3
#define OSPI_TRANS_TYPE_STANDARD	 		0
#define OSPI_TRANS_TYPE_FRF_DEFINED			2

/* Bit fields in XIP_CTRL */
#define OSPI_XIP_CTRL_RXDS_SIG_EN_OFFSET          	25
#define OSPI_XIP_CTRL_XIP_HYPERBUS_EN_OFFSET      	24
#define OSPI_XIP_CTRL_RXDS_EN_OFFSET              	21
#define OSPI_XIP_CTRL_DDR_EN_OFFSET               	19
#define OSPI_XIP_CTRL_DFS_HC_OFFSET               	18
#define OSPI_XIP_CTRL_WAIT_CYCLES_OFFSET          	13
#define OSPI_XIP_CTRL_INST_L_OFFSET                	9
#define OSPI_XIP_CTRL_ADDR_L_OFFSET                	4
#define OSPI_XIP_CTRL_TRANS_TYPE_OFFSET            	2
#define OSPI_XIP_CTRL_FRF_OFFSET                   	0

/* Bit fields in XIP_WR_CTRL */
#define OSPI_XIP_WR_CTRL_WR_FRF                    	0
#define OSPI_XIP_WR_CTRL_WR_TRANS_TYPE             	2
#define OSPI_XIP_WR_CTRL_WR_ADDR_L                 	4
#define OSPI_XIP_WR_CTRL_WR_INST_L                 	8
#define OSPI_XIP_WR_CTRL_WR_SPI_DDR_EN             	10
#define OSPI_XIP_WR_CTRL_WR_INST_DDR_EN            	11
#define OSPI_XIP_WR_CTRL_WR_HYPERBUS_EN            	12
#define OSPI_XIP_WR_CTRL_WR_RXDS_SIG_EN            	13
#define OSPI_XIP_WR_CTRL_WR_DM_EN                  	14
#define OSPI_XIP_WR_CTRL_WR_RSVD                   	15
#define OSPI_XIP_WR_CTRL_WAIT_CYCLES               	16
#define OSPI_XIP_WR_CTRL_DFS_HC                    	21

#define SR_MASK                                         0x7F
#define SR_BUSY                                         (1U << 0)
#define SR_TF_NOT_FULL                                  (1U << 1)
#define SR_TF_EMPTY                                     (1U << 2)
#define SR_RF_NOT_EMPT                                  (1U << 3)
#define SR_RF_FULL                                      (1U << 4)
#define SR_TX_ERR                                       (1U << 5)
#define SR_DCOL                                         (1U << 6)

/* AES_CONTROL fields */
#define AES_CONTROL_LD_KEY                              (1U << 7)
#define AES_CONTROL_XIP_EN                              (1U << 4)
#define AES_CONTROL_DECRYPT_EN                          (1U << 0)

typedef struct {
	ospi_regs_t  		*regs;			/* OSPI registers */
	aes_regs_t  		*aes_regs;		/* AES registers */
	uint8_t 		ss;			/* Slave selct */
} ospi_cfg_t ;

#define readl(a, r)		(a->regs->r)
#define writel(a, r, v)	    	a->regs->r = (v);

#define ospi_enable(ospi) 	writel(ospi, ssienr, 1)
#define ospi_disable(ospi) 	writel(ospi, ssienr, 0)

#ifdef  __cplusplus
}
#endif

#endif /* OSPI_H */
