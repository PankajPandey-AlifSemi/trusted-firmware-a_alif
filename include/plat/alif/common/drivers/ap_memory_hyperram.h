/*
 * Copyright (c) 2025, Alif Semiconductor. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef AP_MEMORY_HYPERRAM_H
#define AP_MEMORY_HYPERRAM_H

#include <stdint.h>

/**
 * @brief Initialize AP Memory APS512Mb PSRAM via OSPI0
 *
 * This function initializes the APS512XXN-OB9-BG PSRAM connected via OSPI0 interface:
 * - Configures pinmux for OSPI signals (D0-D7 for octal and D0-D15 for dual-octal, SCLK, SCLKN, CS, RESETn, RXDS)
 * - Brings OSPI0 up at 16.67 MHz (400 MHz core, baud 24) for device setup,
 *   because the reset latency code is only rated to 133 MHz
 * - Performs global reset of PSRAM and verifies device ID
 * - Programs fixed latency at read and write latency code 7, rated to 200 MHz
 * - Configures x8 octal and x16 dual-octal DDR mode
 * - Raises OSPI0 to 200 MHz (400 MHz core, baud 2) with the per-signal
 *   TXD/RXD/DM/RXDS delay taps on E8, or the AES RXDS delay alone elsewhere
 * - Configures XIP mode for memory-mapped access with read wait 2 * LC and
 *   write wait WLC, without RXDS latency signaling
 *
 * After initialization, PSRAM is accessible at XIP base address 0xA0000000
 *
 * @return 0 on success, negative error code on failure
 */
int ap_memory_hyperram_init(void);

/**
 * @brief Test HyperRAM 64MB memory space (rigorous)
 *
 * Performs comprehensive memory validation including:
 * - Data bus test (walking 1s and 0s on all 32 bits)
 * - Address bus test (aliasing detection across 64MB)
 * - March C- algorithm (industry-standard fault detection)
 * - Checkerboard pattern test (adjacent cell interference)
 * - Pseudo-random pattern test (data retention)
 * - Address-in-address test (address line verification)
 * - Bit-flip stress test (transition faults)
 *
 * Tests run on 4KB blocks sampled every 4MB across the 64MB range.
 *
 * @return 0 on success, -1 on failure
 */
int ap_memory_hyperram_test_64mb(void);

#endif /* AP_MEMORY_HYPERRAM_H */
