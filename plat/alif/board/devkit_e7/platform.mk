#
# Copyright (c) 2025, Alif Semiconductor. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
#

# Flash / RAM driver and AES feature selection, all disabled by default.
ISSI_HYPERRAM_EN	?=	0
ISSI_FLASH_EN		?=	0
MX_FLASH_EN		?=	0
AP_HYPERRAM_EN		?=	0
AES_EN			?=	0

$(eval $(call add_define,ISSI_HYPERRAM_EN))
$(eval $(call add_define,ISSI_FLASH_EN))
$(eval $(call add_define,MX_FLASH_EN))
$(eval $(call add_define,AES_EN))
$(eval $(call add_define,AP_HYPERRAM_EN))

# Ensemble E8 SoC selector. Used to select code blocks that are applicable
# only on the E8 SoC. Enabled by default. set ALIF_SOC_E8=0 to build for E7.
ALIF_SOC_E8		?=	1
$(eval $(call add_define,ALIF_SOC_E8))

ifeq "1" "${AES_EN}"
ifneq "1" "${ISSI_FLASH_EN}"
ifneq "1" "${MX_FLASH_EN}"
$(error AES_EN is set to 1 but neither ISSI_FLASH_EN nor MX_FLASH_EN is set. Set either ISSI_FLASH_EN or MX_FLASH_EN to 1)
endif
endif
ifeq "" "${AES_ENC_KEY}"
$(error AES_EN is set to 1 but AES_ENC_KEY is empty.Set AES key in AES_ENC_KEY)
endif
endif

# AP Memory bus width check - required when AP_HYPERRAM_EN is enabled
ifeq "1" "${AP_HYPERRAM_EN}"
ifeq "" "${AP_MEMORY_BUS_WIDTH}"
$(error AP_MEMORY_BUS_WIDTH must be set when AP_HYPERRAM_EN=1. Use AP_MEMORY_BUS_WIDTH=8 or AP_MEMORY_BUS_WIDTH=16)
endif
$(eval $(call add_define,AP_MEMORY_BUS_WIDTH))
endif

CPPFLAGS		+= -DAES_ENC_KEY=\"${AES_ENC_KEY}\"

BL32_SOURCES		+=	lib/xlat_tables/aarch32/xlat_tables.c	\
				lib/xlat_tables/xlat_tables_common.c	\
				plat/alif/common/alif_common.c		\
				lib/cpus/aarch32/cortex_a32.S		\
				plat/alif/board/devkit_e7/se_service/services.c		\
				plat/arm/board/corstone700/common/drivers/mhu/corstone700_mhu.c	\
				plat/alif/common/alif_mhu.c

PLAT_INCLUDES		:=	-Iplat/alif/board/devkit_e7/common/include	\
				-Iinclude/plat/alif/common			\
				-Iplat/alif/common/drivers/ospi			\
				-Iplat/alif/board/devkit_e7/se_service		\
				-Iplat/arm/board/corstone700/common/include 	\
				-Iplat/arm/board/corstone700/common/drivers/mhu \
				-Iinclude/plat/arm/common 			\
				-Iinclude/plat/alif/common/drivers

NEED_BL32		:=	yes

# This platform ships only SP_MIN as its BL32, so default AARCH32_SP when the
# build did not set it.
ifeq (${AARCH32_SP},none)
    AARCH32_SP := sp_min
endif

# Include GICv2 driver files
include drivers/arm/gic/v2/gicv2.mk

DEVKIT_E7_GIC_SOURCES	:=	${GICV2_SOURCES}			\
				plat/alif/common/alif_gicv2.c

# BL1/BL2 Image not a part of the capsule Image for devkit e7
override NEED_BL1	:=	no
override NEED_BL2	:=	no
override NEED_BL2U	:=	no
override NEED_BL33	:=	no

#TFA for ALIF platforms starts from BL32
override RESET_TO_SP_MIN	:=	1
override BL32_IN_XIP_MEM	:= 	1

#
# Bakery locks must use normal memory + cache maintenance (not coherent RAM).
# PSCI CPU_OFF calls cortex_a32_core_pwr_dwn() which clears CPUECTLR.SMPEN before
# releasing power-domain locks. Coherent bakery locks then read as empty tickets
# and assert in bakery_lock_release() (lib/locks/bakery/bakery_lock_coherent.c:154),
# so AFFINITY_INFO never goes OFF and Linux reports "unable to kill".
#
USE_COHERENT_MEM               :=      0

# When the BL33 image is a Linux kernel, SP_MIN must pass it a preloaded
# device tree. Other payloads (e.g. Zephyr) do not need one, so this is
# opt-in via ARM_LINUX_KERNEL_AS_BL33=1.
ARM_LINUX_KERNEL_AS_BL33	?=	0
$(eval $(call add_define,ARM_LINUX_KERNEL_AS_BL33))

ifeq (${ARM_LINUX_KERNEL_AS_BL33},1)
  ifndef ARM_PRELOADED_DTB_BASE
    $(error "ARM_PRELOADED_DTB_BASE must be set if ARM_LINUX_KERNEL_AS_BL33 is used.")
  endif
  $(eval $(call add_define,ARM_PRELOADED_DTB_BASE))
endif

# Adding TARGET_PLATFORM as a GCC define (-D option)
$(eval $(call add_define,TARGET_PLATFORM_$(call uppercase,${TARGET_PLATFORM})))

# ----------------------------------------------------------------------------
# BL32 relocation knobs.
#
# Each region is exposed as base + size so the image can be moved and resized
# for different boot scenarios (Linux, Zephyr, custom MRAM/SRAM carve-outs)
# without editing platform_def.h:
#
#   ALIF_BL32_XIP_BASE / _SIZE      code + rodata, executed in place from MRAM
#   ALIF_TRUSTED_SRAM_BASE / _SIZE  data/bss/stack, run from Trusted SRAM
#
# Both bases must be 4 KB (page) aligned.
# ----------------------------------------------------------------------------

ALIF_BL32_XIP_BASE	?=	0x80002000
ALIF_BL32_XIP_SIZE	?=	0x8000
ALIF_TRUSTED_SRAM_BASE	?=	0x2000000
ALIF_TRUSTED_SRAM_SIZE	?=	0x20000

$(eval $(call add_define,ALIF_BL32_XIP_BASE))
$(eval $(call add_define,ALIF_BL32_XIP_SIZE))
$(eval $(call add_define,ALIF_TRUSTED_SRAM_BASE))
$(eval $(call add_define,ALIF_TRUSTED_SRAM_SIZE))
