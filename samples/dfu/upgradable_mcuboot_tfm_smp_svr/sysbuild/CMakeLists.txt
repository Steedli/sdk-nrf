# SPDX-License-Identifier: Apache-2.0

find_package(Sysbuild REQUIRED HINTS $ENV{ZEPHYR_BASE})
find_package(Python3 REQUIRED COMPONENTS Interpreter)

project(sysbuild LANGUAGES)

set(kmu_keyfile ${CMAKE_BINARY_DIR}/keyfile.json)
set(b0_signing_key ${ZEPHYR_MCUBOOT_MODULE_DIR}/root-ed25519.pem)

add_custom_command(
  OUTPUT ${kmu_keyfile}
  COMMAND ${CMAKE_COMMAND} -E rm -f ${kmu_keyfile}
  COMMAND ${Python3_EXECUTABLE} -m west ncs-provision upload
          --keyname BL_PUBKEY
          --key ${b0_signing_key}
          --build-dir ${CMAKE_BINARY_DIR}
          --soc nrf54lm20a
          --dry-run
  DEPENDS ${b0_signing_key}
  COMMENT "Generating clean KMU keyfile (${kmu_keyfile})"
  WORKING_DIRECTORY ${ZEPHYR_BASE}
  VERBATIM
)

add_custom_target(generate_clean_kmu_keyfile ALL DEPENDS ${kmu_keyfile})
