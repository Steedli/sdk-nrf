#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NONE)

FILE(GLOB app_sources src/*.c)
# NORDIC SDK APP START
target_sources(app PRIVATE ${app_sources})
# NORDIC SDK APP END

if (CONFIG_CODE_DATA_RELOCATION)
zephyr_code_relocate(FILES src/main.c LOCATION MYTCM NOKEEP)
zephyr_code_relocate(FILES ../../../subsys/esb/esb.c LOCATION MYTCM NOKEEP)
zephyr_code_relocate(FILES ../../../subsys/esb/esb_dppi.c LOCATION MYTCM NOKEEP)
endif()
