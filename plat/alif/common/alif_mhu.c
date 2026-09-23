/*
 * Copyright (c) 2026, Alif Semiconductor. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Polled MHUv2 doorbell driver for APSS <-> M55-HE MHU0 channel 0.
 * ACCESS_REQUEST, CH_SET of a 32-bit value, then wait for the receiver
 * to clear CH_ST.
 */

#include <platform_def.h>

#include <arch_helpers.h>
#include <common/debug.h>
#include <drivers/delay_timer.h>
#include <lib/mmio.h>
#include <lib/utils_def.h>
#include <plat/alif/common/alif_mhu.h>

#define MHU_SND_CH_ST			U(0x00)
#define MHU_SND_CH_SET			U(0x0c)
#define MHU_SND_CH_INT_CLR		U(0x14)
#define MHU_SND_CH_INT_EN		U(0x18)

#define MHU_RCV_CH_ST			U(0x00)
#define MHU_RCV_CH_CLR			U(0x08)
#define MHU_RCV_CH_MSK_SET		U(0x14)
#define MHU_RCV_CH_MSK_CLR		U(0x18)

#define MHU_CH_WINDOW_SIZE		U(0x20)

#define MHU_SND_ACCESS_REQUEST		U(0xf88)
#define MHU_SND_ACCESS_READY		U(0xf8c)
#define MHU_INT_CLR			U(0xf94)
#define MHU_INT_EN			U(0xf98)

#define MHU_ACCESS_REQ			U(1)
#define MHU_ACCESS_RDY			U(1)
#define MHU_INT_ALL			U(0x7)
#define MHU_CH_INT_ALL			U(1)
#define MHU_CH_CLR_ALL			U(0xffffffff)

#define MHU_CH_OFFSET			(ALIF_MHU_CHANNEL * MHU_CH_WINDOW_SIZE)
#define MHU_TX_CH(reg)			(ALIF_M55_MHU_TX_BASE + MHU_CH_OFFSET + (reg))
#define MHU_RX_CH(reg)			(ALIF_M55_MHU_RX_BASE + MHU_CH_OFFSET + (reg))

static int alif_mhu_wait_idle(uintptr_t status, uint32_t timeout_us)
{
	uint64_t timeout = timeout_init_us(timeout_us);

	while (mmio_read_32(status) != 0U) {
		if (timeout_elapsed(timeout) != 0U) {
			return -1;
		}
	}

	return 0;
}

static int alif_mhu_send(uint32_t value, uint32_t timeout_us)
{
	uint64_t timeout;
	int ret = -1;

	mmio_write_32(ALIF_M55_MHU_TX_BASE + MHU_SND_ACCESS_REQUEST,
		      MHU_ACCESS_REQ);

	timeout = timeout_init_us(timeout_us);
	while ((mmio_read_32(ALIF_M55_MHU_TX_BASE + MHU_SND_ACCESS_READY) &
		MHU_ACCESS_RDY) == 0U) {
		if (timeout_elapsed(timeout) != 0U) {
			ERROR("MHU: ACCESS_READY timeout\n");
			goto out;
		}
	}

	if (alif_mhu_wait_idle(MHU_TX_CH(MHU_SND_CH_ST), timeout_us) != 0) {
		ERROR("MHU: TX channel busy\n");
		goto out;
	}

	/* Polled completion: do not enable the sender combined interrupt. */
	mmio_write_32(MHU_TX_CH(MHU_SND_CH_INT_EN), U(0));
	mmio_write_32(MHU_TX_CH(MHU_SND_CH_INT_CLR), MHU_CH_INT_ALL);

	dsbsy();
	mmio_write_32(MHU_TX_CH(MHU_SND_CH_SET), value);

	/* M55-HE clears CH_ST after it has taken the doorbell. */
	ret = alif_mhu_wait_idle(MHU_TX_CH(MHU_SND_CH_ST), timeout_us);
	if (ret != 0) {
		ERROR("MHU: M55 did not consume 0x%x\n", value);
	}

out:
	mmio_write_32(ALIF_M55_MHU_TX_BASE + MHU_SND_ACCESS_REQUEST, U(0));
	return ret;
}

static int alif_mhu_recv(uint32_t *value, uint32_t timeout_us)
{
	uint64_t timeout = timeout_init_us(timeout_us);
	uint32_t status;

	do {
		status = mmio_read_32(MHU_RX_CH(MHU_RCV_CH_ST));
		if (status != 0U) {
			mmio_write_32(MHU_RX_CH(MHU_RCV_CH_CLR), MHU_CH_CLR_ALL);
			dsbsy();
			*value = status;
			return 0;
		}
	} while (timeout_elapsed(timeout) == 0U);

	return -1;
}

void alif_mhu_init(void)
{
	mmio_write_32(ALIF_M55_MHU_TX_BASE + MHU_SND_ACCESS_REQUEST, U(0));
	mmio_write_32(ALIF_M55_MHU_TX_BASE + MHU_INT_EN, U(0));
	mmio_write_32(ALIF_M55_MHU_TX_BASE + MHU_INT_CLR, MHU_INT_ALL);
	mmio_write_32(MHU_TX_CH(MHU_SND_CH_INT_EN), U(0));
	mmio_write_32(MHU_TX_CH(MHU_SND_CH_INT_CLR), MHU_CH_INT_ALL);

	mmio_write_32(ALIF_M55_MHU_RX_BASE + MHU_INT_EN, U(0));
	mmio_write_32(ALIF_M55_MHU_RX_BASE + MHU_INT_CLR, MHU_INT_ALL);

	/* Unmask and drain channel 0 so a stale doorbell cannot look like ACK. */
	mmio_write_32(MHU_RX_CH(MHU_RCV_CH_MSK_CLR), MHU_CH_CLR_ALL);
	mmio_write_32(MHU_RX_CH(MHU_RCV_CH_CLR), MHU_CH_CLR_ALL);

	VERBOSE("MHU0: A32 <-> M55-HE doorbell ready (ch %u)\n",
		ALIF_MHU_CHANNEL);
}

int alif_mhu_notify_going_off(void)
{
	uint32_t ack = 0U;

	INFO("MHU0: sending A32_GOING_OFF (0x%x) to M55-HE\n", A32_GOING_OFF);

	mmio_write_32(MHU_RX_CH(MHU_RCV_CH_CLR), MHU_CH_CLR_ALL);

	if (alif_mhu_send(A32_GOING_OFF, ALIF_M55_MHU_ACK_TIMEOUT_US) != 0) {
		WARN("MHU0: A32_GOING_OFF not accepted by M55-HE\n");
		return -1;
	}

	if (alif_mhu_recv(&ack, ALIF_M55_MHU_ACK_TIMEOUT_US) != 0) {
		WARN("MHU0: timed out waiting for A32_GOING_OFF_ACK\n");
		return -1;
	}

	if (ack != A32_GOING_OFF_ACK) {
		WARN("MHU0: unexpected ACK 0x%x (expected 0x%x)\n",
		     ack, A32_GOING_OFF_ACK);
		return -1;
	}

	INFO("MHU0: received A32_GOING_OFF_ACK from M55-HE\n");
	return 0;
}

void alif_mhu_shutdown(void)
{
	mmio_write_32(MHU_RX_CH(MHU_RCV_CH_MSK_SET), MHU_CH_CLR_ALL);
	mmio_write_32(MHU_RX_CH(MHU_RCV_CH_CLR), MHU_CH_CLR_ALL);
	mmio_write_32(ALIF_M55_MHU_RX_BASE + MHU_INT_EN, U(0));
	mmio_write_32(ALIF_M55_MHU_RX_BASE + MHU_INT_CLR, MHU_INT_ALL);

	mmio_write_32(MHU_TX_CH(MHU_SND_CH_INT_EN), U(0));
	mmio_write_32(MHU_TX_CH(MHU_SND_CH_INT_CLR), MHU_CH_INT_ALL);
	mmio_write_32(ALIF_M55_MHU_TX_BASE + MHU_INT_EN, U(0));
	mmio_write_32(ALIF_M55_MHU_TX_BASE + MHU_INT_CLR, MHU_INT_ALL);
	mmio_write_32(ALIF_M55_MHU_TX_BASE + MHU_SND_ACCESS_REQUEST, U(0));
	dsbsy();

	VERBOSE("MHU0: doorbells stopped after A32_GOING_OFF\n");
}
