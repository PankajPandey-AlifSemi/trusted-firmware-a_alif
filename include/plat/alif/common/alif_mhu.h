/*
 * Copyright (c) 2026, Alif Semiconductor. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ALIF_MHU_H
#define ALIF_MHU_H

#include <stdint.h>

#include <lib/utils_def.h>

/*
 * APSS (TF-A) <-> M55-HE MHU0 doorbell protocol.
 *
 * Same transport as the Linux/Zephyr MHU0 samples:
 * one 32-bit value on channel 0. A32 TX is 0x1B040000, A32 RX is 0x1B050000.
 *
 * Keep these opcodes in sync with the Zephyr responder:
 *   sdk-alif/alif/samples/drivers/ipm/ipm_arm_mhuv2/include/a32_going_off.h
 */
#define A32_GOING_OFF			U(0xA320FF01)
#define A32_GOING_OFF_ACK		U(0xA320FFA1)

/*
 * M55-master opcodes (Linux MHU IRQ). TF-A does not send these;
 * SYSTEM_OFF still uses A32_GOING_OFF after orderly_poweroff.
 */
#define M55_PERIPH_OFF_REQ		U(0xA320FF10)
#define M55_PERIPH_OFF_REQ_ACK		U(0xA320FF11)

#define ALIF_MHU_CHANNEL		U(0)

void alif_mhu_init(void);

/*
 * Send A32_GOING_OFF and wait up to ALIF_M55_MHU_ACK_TIMEOUT_US for
 * A32_GOING_OFF_ACK. Returns 0 on ACK, -1 on timeout or transport error.
 * SYSTEM_OFF continues either way; the caller must not hang forever.
 */
int alif_mhu_notify_going_off(void);

/* Mask, drain and drop ACCESS_REQUEST after the going-off handshake. */
void alif_mhu_shutdown(void);

#endif /* ALIF_MHU_H */
