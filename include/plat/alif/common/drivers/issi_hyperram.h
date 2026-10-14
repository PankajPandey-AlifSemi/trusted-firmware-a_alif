/*
 * Copyright (c) 2025, Alif Semiconductor. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ISSI_HYPERRAM_H
#define ISSI_HYPERRAM_H

#include "ospi.h"
#include <stdint.h>

/*
 * Initialize the ISSI IS66/67WVH64M8 HyperRAM on OSPI0 and enter XIP mode:
 * - Writes CR0 on both dies at 16.67 MHz with fixed latency of 6 clocks,
 *   then reads ID0 and CR0 back
 * - Raises the bus to 100 MHz (400 MHz core, baud 4)
 * - Configures HyperBus XIP with read and write wait 2 x latency and no RWDS
 *   latency signaling
 * Pins must be configured by the caller.
 */
int issi_hyperram_init(void);

#endif /* ISSI_HYPERRAM_H */
