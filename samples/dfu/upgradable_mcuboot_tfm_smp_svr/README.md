# nRF54LM20A BLE SMP DFU with NSIB, Upgradable MCUboot, TF-M, and DTS Partitions

This sample is a reduced `smp_svr` project for testing Bluetooth SMP DFU on
`nrf54lm20dk/nrf54lm20a/cpuapp/ns` with the following boot chain:

```text
NSIB / B0 -> MCUboot S0/S1 -> TF-M secure image -> non-secure BLE SMP app
```

The main goal is to support:

- Application OTA update over Bluetooth SMP.
- Upgradable MCUboot, meaning MCUboot itself can be replaced by DFU and then
  selected by NSIB/B0.
- TF-M enabled non-secure application builds.
- Static DTS flash partitions, with `SB_CONFIG_PARTITION_MANAGER=n`.

This is a development example. It uses MCUboot's default Ed25519 key and should
not be used as-is for production.

## Tested Target

- NCS: `v3.3.0`
- Board: `nrf54lm20dk/nrf54lm20a/cpuapp/ns`
- Transport: Bluetooth SMP
- Advertising name: `NCS_DFU_LM20A`
- Build directory used during testing: `build_dts_ns`

## Project Structure and Sysbuild Integration

This sample is a sysbuild application. The top-level application is the
non-secure BLE SMP server, and sysbuild adds the bootloader images around it.

The important directory structure is:

```text
upgradable_mcuboot_tfm_smp_svr/
├── CMakeLists.txt
├── prj.conf
├── bt.conf
├── sysbuild.conf
├── boards/
│   ├── nrf54lm20dk_nrf54lm20a_cpuapp.overlay
│   └── nrf54lm20dk_nrf54lm20a_cpuapp_ns.overlay
├── src/
│   ├── main.c
│   └── bluetooth.c
└── sysbuild/
    ├── CMakeLists.txt
    ├── b0/
    │   ├── prj.conf
    │   └── boards/nrf54lm20dk_nrf54lm20a_cpuapp.overlay
    └── mcuboot/
        ├── prj.conf
        └── boards/
            ├── nrf54lm20dk_nrf54lm20a_cpuapp.conf
            └── nrf54lm20dk_nrf54lm20a_cpuapp.overlay
```

The application image is configured by:

- `prj.conf`
  - Enables MCUmgr, image management, flash map, and MCUboot app update support.
  - Selects `CONFIG_USE_DT_CODE_PARTITION=y` so the app uses the DTS
    `zephyr,code-partition`.
  - Sets the application signing version with
    `CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION`.

- `bt.conf`
  - Enables Bluetooth peripheral mode and the SMP-over-Bluetooth transport.
  - Increases the Bluetooth SMP buffers for better DFU throughput.

- `boards/nrf54lm20dk_nrf54lm20a_cpuapp_ns.overlay`
  - Provides the DTS static partition table for the non-secure build.
  - Sets `zephyr,code-partition = &slot0_ns_partition`.

Sysbuild adds NSIB/B0 and MCUboot through `sysbuild.conf`:

```conf
SB_CONFIG_PARTITION_MANAGER=n
SB_CONFIG_SECURE_BOOT_APPCORE=y
SB_CONFIG_BOOTLOADER_MCUBOOT=y
SB_CONFIG_BOOT_SIGNATURE_TYPE_ED25519=y
```

These options have the following roles:

- `SB_CONFIG_PARTITION_MANAGER=n`
  - Disables Partition Manager and makes this sample use the DTS fixed
    partitions.

- `SB_CONFIG_SECURE_BOOT_APPCORE=y`
  - Adds the NSIB/B0 image to the sysbuild build.
  - B0 becomes the immutable first-stage bootloader for the application core.

- `SB_CONFIG_BOOTLOADER_MCUBOOT=y`
  - Adds MCUboot as a second-stage bootloader image.
  - Because secure boot is also enabled, sysbuild builds MCUboot in an S0/S1
    upgradable layout instead of a single fixed MCUboot partition.

- `SB_CONFIG_BOOT_SIGNATURE_TYPE_ED25519=y`
  - Selects Ed25519 signatures for both secure boot and MCUboot image signing.

The signing keys are configured in `sysbuild.conf`:

```conf
SB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE="${ZEPHYR_MCUBOOT_MODULE_DIR}/root-ed25519.pem"
SB_CONFIG_BOOT_SIGNATURE_KEY_FILE="${ZEPHYR_MCUBOOT_MODULE_DIR}/root-ed25519.pem"
```

- `SB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE`
  - Signs MCUboot for B0/NSIB.
  - Its public key is provisioned to KMU as `BL_PUBKEY`.

- `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE`
  - Signs the TF-M + application image for MCUboot.
  - Its public key is compiled into MCUboot.

B0/NSIB image-specific configuration is under `sysbuild/b0/`:

- `sysbuild/b0/prj.conf`
  - Sets `CONFIG_IS_SECURE_BOOTLOADER=y`.
  - Enables secure boot validation and secure boot storage.
  - Keeps the B0 configuration minimal.

- `sysbuild/b0/boards/nrf54lm20dk_nrf54lm20a_cpuapp.overlay`
  - Selects the B0 code partition.

MCUboot image-specific configuration is under `sysbuild/mcuboot/`:

- `sysbuild/mcuboot/prj.conf`
  - Kept as an empty base configuration. Board-specific settings are in the
    board file.

- `sysbuild/mcuboot/boards/nrf54lm20dk_nrf54lm20a_cpuapp.conf`
  - Tunes MCUboot for the `s0_partition` / `s1_partition` sizes.
  - Sets MCUboot logging.
  - Sets `CONFIG_FW_INFO_FIRMWARE_VERSION`.
  - Keeps MCUboot within the S0/S1 slot size.

- `sysbuild/mcuboot/boards/nrf54lm20dk_nrf54lm20a_cpuapp.overlay`
  - Includes the common application-core static partition overlay.
  - Sets MCUboot's `zephyr,code-partition` to `s0_partition`.
  - Sysbuild also generates an S1 variant of MCUboot linked for `s1_partition`.

Finally, `sysbuild/CMakeLists.txt` generates a clean `keyfile.json`:

```text
west ncs-provision upload --keyname BL_PUBKEY --dry-run ...
```

This file is consumed by `west flash` / `nrfutil` to provision the B0 public key
into KMU during `west flash --recover`.

## Boot and Security Model

NSIB/B0 is the immutable first-stage bootloader. It verifies MCUboot from either
the S0 or S1 MCUboot slot. MCUboot is the second-stage bootloader. It verifies
the combined TF-M + non-secure application image and jumps to the TF-M secure
vector table.

### Verification Flow

B0/NSIB verifies MCUboot before MCUboot is allowed to execute:

1. After reset, B0 runs from `b0_partition`.
2. B0 decides which MCUboot slot to try, normally `s0_partition` first unless
   the boot state or monotonic counter indicates that `s1_partition` should be
   used.
3. B0 reads the MCUboot image metadata and firmware information from the
   selected S0/S1 slot.
4. B0 verifies the MCUboot signature using the public key provisioned into KMU
   as `BL_PUBKEY`. In this sample, `sysbuild/CMakeLists.txt` generates the
   `keyfile.json` used by `west flash` / `nrfutil` to provision that key.
5. If the signature and version checks pass, B0 updates the monotonic counter
   as needed and jumps to MCUboot, for example at `0x8800` when booting S0.
6. If the selected slot is invalid, B0 can try the other MCUboot slot depending
   on the boot state.

MCUboot then verifies the combined TF-M + application image:

1. MCUboot starts from the S0/S1 slot chosen by B0.
2. MCUboot opens `slot0_partition` as the primary TF-M + application slot and
   `slot1_partition` as the secondary TF-M + application slot.
3. MCUboot reads the image header at the beginning of the selected application
   slot. For the primary slot this is `0x42000`.
4. The image payload starts after the `0x800`-byte MCUboot header. In the
   primary slot this means TF-M starts at `0x42800`, and the non-secure app
   vector table is at `0x82800`.
5. MCUboot parses the image TLVs, checks the image hash, and verifies the
   signature using the application verification key compiled into MCUboot.
6. If `slot1_partition` contains a pending update, MCUboot performs the
   configured swap operation (`swap using move` in this sample), then boots the
   new image from `slot0_partition`.
7. Once the selected TF-M + application image is valid, MCUboot jumps to the
   TF-M secure vector table at `slot0_partition + 0x800`.
8. TF-M configures secure/non-secure memory boundaries, sets the non-secure
   VTOR/MSP to the non-secure vector table at `slot0_ns_partition + 0x800`, and
   starts the non-secure application.

The application runs in the non-secure domain. Because of this, the application
cannot read secure flash regions such as:

- B0 at `0x00000`
- MCUboot S0/S1 at `0x08000` and `0x20000`
- TF-M storage partitions
- The secure part of the primary application slot at `0x42000`

That access restriction is important for MCUmgr image management. A normal
Zephyr image management implementation tries to read the primary slot header.
In this layout that header is in the secure part of the primary TF-M +
application slot, so a local Zephyr patch is needed to avoid a secure fault.

## Static Flash Layout

The project disables Partition Manager and defines the flash layout in DTS.

The primary bootloader and storage regions are:

- `b0_partition`: `0x00000000`, size `0x00008000`
- `s0_partition`: `0x00008000`, size `0x00018000`
- `s1_partition`: `0x00020000`, size `0x00018000`
- `tfm_its_partition`: `0x00038000`, size `0x00004000`
- `tfm_otp_partition`: `0x0003c000`, size `0x00002000`
- `tfm_ps_partition`: `0x0003e000`, size `0x00004000`
- `storage_partition`: `0x001e4000`, size `0x00001000`

The combined TF-M + application slots are:

- `slot0_partition`: `0x00042000`, size `0x000d1000`
- `slot1_partition`: `0x00113000`, size `0x000d1000`

Each combined TF-M + application slot is a `fixed-subpartitions` node:

- `slot0_s_partition`: slot-relative offset `0x00000000`, size `0x00040000`
- `slot0_ns_partition`: slot-relative offset `0x00040000`, size `0x00091000`
- `slot1_s_partition`: slot-relative offset `0x00000000`, size `0x00040000`
- `slot1_ns_partition`: slot-relative offset `0x00040000`, size `0x00091000`

Detailed structure of `slot0_partition`:

```text
slot0_partition, absolute 0x00042000, size 0x000d1000

0x00042000 - 0x000427ff
    MCUboot image header and padded header area, size 0x800.
    The actual MCUboot image header starts at 0x42000.

slot0_s_partition, slot-relative 0x00000000, absolute 0x00042000, size 0x00040000
    0x00042800
        TF-M secure vector table.
        TF-M is linked to start after the MCUboot header by using
        TFM_MCUBOOT_OFFSET = 0x800.
    0x00042800 - 0x00081fff
        TF-M secure image code, veneers, and secure-side read-only data.

slot0_ns_partition, slot-relative 0x00040000, absolute 0x00082000, size 0x00091000
    0x00082000 - 0x000827ff
        Non-secure image reserved header/pad area, size 0x800.
        This is not a second independent MCUboot image header; it is the
        Zephyr/TF-M non-secure image boot offset.
    0x00082800
        Non-secure application vector table.
    0x00082800 - 0x00112fff
        Non-secure application image area.
```

Detailed structure of `slot1_partition` is identical, but shifted to the
secondary slot:

```text
slot1_partition, absolute 0x00113000, size 0x000d1000

0x00113000 - 0x001137ff
    MCUboot image header and padded header area, size 0x800.

slot1_s_partition, slot-relative 0x00000000, absolute 0x00113000, size 0x00040000
    0x00113800
        TF-M secure vector table for the secondary image.
    0x00113800 - 0x00152fff
        TF-M secure image area for the secondary image.

slot1_ns_partition, slot-relative 0x00040000, absolute 0x00153000, size 0x00091000
    0x00153000 - 0x001537ff
        Non-secure image reserved header/pad area, size 0x800.
    0x00153800
        Non-secure application vector table for the secondary image.
    0x00153800 - 0x001e3fff
        Non-secure application image area for the secondary image.
```

The non-secure application uses:

```dts
chosen {
	zephyr,code-partition = &slot0_ns_partition;
};
```

Important resolved addresses in the signed application image are:

- MCUboot image header: `0x42000`
- TF-M secure vector table: `0x42800`
- Non-secure application vector table: `0x82800`

## Important Project Files

- `sysbuild.conf`
  - Disables Partition Manager.
  - Enables NSIB/B0 and MCUboot.
  - Selects Ed25519 signatures.
  - Uses the default MCUboot root key for development.
  - Sets the MCUboot S0/S1 version with
    `SB_CONFIG_SECURE_BOOT_MCUBOOT_VERSION`.

- `sysbuild/CMakeLists.txt`
  - Generates a clean `keyfile.json` for KMU provisioning.
  - Uses `west ncs-provision upload --dry-run`.
  - Avoids duplicate KMU key entries that previously caused provisioning
    failures.

- `boards/nrf54lm20dk_nrf54lm20a_cpuapp.overlay`
  - Defines the secure CPUAPP flash layout for B0, MCUboot, TF-M, app slots,
    and storage.

- `boards/nrf54lm20dk_nrf54lm20a_cpuapp_ns.overlay`
  - Defines the same layout for the non-secure app build.
  - Chooses `slot0_ns_partition` as the non-secure code partition.

- `prj.conf`
  - Enables MCUmgr, image management, flash map support, and MCUboot app update
    support.
  - Sets the application image version with
    `CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION`.
  - Forces signing placement with
    `CONFIG_MCUBOOT_EXTRA_IMGTOOL_ARGS="--slot-size 0xd1000 --hex-addr 0x42000"`.

- `bt.conf`
  - Enables BLE peripheral mode and Bluetooth SMP transport.
  - Increases MTU and buffer sizes for better DFU throughput.

- `sysbuild/mcuboot/boards/nrf54lm20dk_nrf54lm20a_cpuapp.conf`
  - Keeps MCUboot small enough for the S0/S1 slots.
  - Sets `CONFIG_FW_INFO_FIRMWARE_VERSION`.
  - Disables `FPROTECT` for this nRF54L secure bootloader configuration.

- `src/main.c`
  - Starts Bluetooth SMP advertising.
  - Prints build time and application image version at boot.

## Local NCS Source Changes

This example required local changes inside the NCS tree. These files are not
part of the application directory, but they are needed for this DTS-only
prototype to work with TF-M and MCUmgr.

Modified local NCS files:

- `modules/tee/tf-m/trusted-firmware-m/platform/ext/target/nordic_nrf/common/nrf54lm20a/partition/flash_layout.h`
  - Added DTS-based flash layout support when Partition Manager is disabled.
  - Reads `slot0_s_partition`, `slot0_ns_partition`, `tfm_ps_partition`,
    `tfm_its_partition`, `tfm_otp_partition`, and `storage_partition` from
    generated devicetree macros.

- `modules/tee/tf-m/trusted-firmware-m/platform/ext/target/nordic_nrf/common/nrf54lm20a/partition/region_defs.h`
  - Applies `TFM_MCUBOOT_OFFSET` to secure and non-secure code start addresses.
  - Keeps the non-secure partition base at the start of `slot0_ns_partition`
    for SAU/MPC permissions, while using the vector address after the MCUboot
    header for execution.

- `modules/tee/tf-m/trusted-firmware-m/platform/ext/target/nordic_nrf/common/core/target_cfg.c`
  - Uses `TFM_MCUBOOT_OFFSET` when selecting the non-secure VTOR/MSP address.
  - This lets TF-M jump to `0x82800` instead of `0x82000`.

- `nrf/modules/trusted-firmware-m/tfm_boards/partition/region_defs.h`
  - Applies `TFM_MCUBOOT_OFFSET` to the generic Nordic TF-M board
    `NS_CODE_START` when Partition Manager is disabled.

- `zephyr/subsys/mgmt/mcumgr/grp/img_mgmt/src/img_mgmt.c`
  - Avoids reading the secure part of the primary TF-M + application slot from
    the non-secure app.
  - For slot 0 image state, returns the current app version from
    `CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION` instead of reading address `0x42000`.
  - This prevents a secure fault during APP DFU image state queries.

These changes are local patches for this experiment. For production or a shared
SDK tree, carry them as explicit patches and rebase them carefully when moving
to another NCS release.

## Problems Found During Bring-up

The following issues were found and fixed while creating the example.

- Duplicate KMU key provisioning entries
  - Symptom: `KEY Provision Error` during `west flash`.
  - Fix: generate a clean `keyfile.json` from `sysbuild/CMakeLists.txt` and use
    `west flash --recover` for initial provisioning.

- MCUboot could not find a bootable application image
  - Symptom: `Image in the primary slot is not valid` and
    `Unable to find bootable image`.
  - Root cause: the combined TF-M + NS image was larger than the original secure
    slot size. MCUboot validated only a `0x40000` slot while the image extended
    beyond it.
  - Fix: make `slot0_partition` and `slot1_partition` full TF-M + application
    slots of `0xd1000` bytes.

- Signed image was shifted by the MCUboot header
  - Symptom: MCUboot printed `Jumping to the first image slot`, but the app did
    not boot.
  - Root cause: TF-M, NS app, and `imgtool --pad-header` did not agree on the
    header offset.
  - Fix: define `slot*_s_partition` at slot-relative `0x0` and let TF-M apply
    `TFM_MCUBOOT_OFFSET`.

- TF-M jumped to the wrong non-secure vector table
  - Symptom: boot stopped after MCUboot jump.
  - Root cause: TF-M used the non-secure partition base `0x82000`, but the NS
    vector table is after the MCUboot header at `0x82800`.
  - Fix: patch TF-M `target_cfg.c` and region definitions to use
    `TFM_MCUBOOT_OFFSET` for VTOR/MSP selection.

- APP DFU caused a secure fault
  - Symptom: secure fault at address `0x42000` during APP upgrade.
  - Root cause: MCUmgr image state handling in the non-secure app tried to read
    the primary TF-M + application slot header, which is in the secure part of
    the slot.
  - Fix: patch `img_mgmt_read_info()` so slot 0 state does not read secure
    flash from the non-secure app.

## Build

Use the nRF Connect SDK toolchain environment. From this directory:

```powershell
$env:ZEPHYR_BASE='D:\workspace\NCS\v3.3.0\zephyr'
& "D:\workspace\NCS\toolchains\936afb6332\opt\bin\python.exe" -m west build `
  -p always `
  -b nrf54lm20dk/nrf54lm20a/cpuapp/ns `
  --sysbuild `
  -d build_dts_ns `
  -- '-DEXTRA_CONF_FILE=bt.conf'
```

Expected generated artifacts:

- `build_dts_ns/dfu_application.zip`
  - Contains `smp_svr.signed.bin`.
  - Current manifest image index: `0`.
  - Current manifest load address: `0x42000`.

- `build_dts_ns/dfu_mcuboot.zip`
  - Contains `signed_by_mcuboot_and_b0_mcuboot.bin`.
  - Contains `signed_by_mcuboot_and_b0_mcuboot_s1_variant.bin`.
  - These are the S0 and S1 MCUboot variants for NSIB/B0.

## Initial Flash

Use `--recover` for the first flash, or whenever KMU provisioning needs to be
reset. `--erase` is not enough for the initial secure boot key provisioning
case.

```powershell
$env:ZEPHYR_BASE='D:\workspace\NCS\v3.3.0\zephyr'
& "D:\workspace\NCS\toolchains\936afb6332\opt\bin\python.exe" -m west flash `
  --recover `
  --no-rebuild `
  -d build_dts_ns `
  --dev-id 1051878096
```

Expected boot log after a successful flash:

```text
Attempting to boot slot 0.
Booting (0x8800).
*** Booting MCUboot ...
I: Bootloader chainload address offset: 0x42000
I: Image version: v1.0.x
I: Jumping to the first image slot
*** Booting nRF Connect SDK ...
<inf> smp_bt_sample: Advertising successfully started
<inf> smp_sample: APP image version: ...
```

## Updating the Application over Bluetooth

Use this flow to update the non-secure application while keeping B0 and MCUboot
unchanged.

1. Change the application version in `prj.conf`.

   Example:

   ```conf
   CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION="1.0.3+5"
   ```

2. Rebuild.

   ```powershell
   $env:ZEPHYR_BASE='D:\workspace\NCS\v3.3.0\zephyr'
   & "D:\workspace\NCS\toolchains\936afb6332\opt\bin\python.exe" -m west build `
     -b nrf54lm20dk/nrf54lm20a/cpuapp/ns `
     --sysbuild `
     -d build_dts_ns `
     -- '-DEXTRA_CONF_FILE=bt.conf'
   ```

3. Use the generated package:

   ```text
   build_dts_ns/dfu_application.zip
   ```

4. With nRF Connect Device Manager:

   - Scan for `NCS_DFU_LM20A`.
   - Connect to the device.
   - Open the image or DFU tab.
   - Select `build_dts_ns/dfu_application.zip`.
   - Start the upload.
   - Mark the uploaded image as test or confirm, depending on the UI flow.
   - Reset the device from the app or by using the OS reset command.

5. With `mcumgr` CLI, if available:

   Extract `smp_svr.signed.bin` from `dfu_application.zip`, then run a BLE
   upload similar to:

   ```powershell
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image upload smp_svr.signed.bin
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image list
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image test <uploaded-image-hash>
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A reset
   ```

6. Confirm the new app after it boots.

   If the image was only marked as test:

   ```powershell
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image confirm
   ```

7. Check the UART log.

   The app prints:

   ```text
   APP image version: 1.0.3+5
   ```

## Updating MCUboot over Bluetooth

MCUboot is stored in two NSIB/B0-controlled slots:

- S0: `s0_partition` at `0x8000`
- S1: `s1_partition` at `0x20000`

B0 selects one of those slots during reset. MCUboot itself handles the update
selection and B0 validates the selected slot on the next boot.

1. Change the MCUboot version.

   In `sysbuild.conf`, update:

   ```conf
   SB_CONFIG_SECURE_BOOT_MCUBOOT_VERSION="0.0.1+3"
   ```

   In `sysbuild/mcuboot/boards/nrf54lm20dk_nrf54lm20a_cpuapp.conf`, update:

   ```conf
   CONFIG_FW_INFO_FIRMWARE_VERSION=3
   ```

   Keep both values moving forward together during testing so the serial log is
   easy to interpret.

2. Rebuild.

   ```powershell
   $env:ZEPHYR_BASE='D:\workspace\NCS\v3.3.0\zephyr'
   & "D:\workspace\NCS\toolchains\936afb6332\opt\bin\python.exe" -m west build `
     -p always `
     -b nrf54lm20dk/nrf54lm20a/cpuapp/ns `
     --sysbuild `
     -d build_dts_ns `
     -- '-DEXTRA_CONF_FILE=bt.conf'
   ```

3. Use the generated package:

   ```text
   build_dts_ns/dfu_mcuboot.zip
   ```

   The package contains both MCUboot variants:

   - `signed_by_mcuboot_and_b0_mcuboot.bin`
     - Linked for S0, load address `0x8000`.
   - `signed_by_mcuboot_and_b0_mcuboot_s1_variant.bin`
     - Linked for S1, load address `0x20000`.

4. Check which MCUboot slot is currently active.

   Look at the B0 log after reset:

   ```text
   Attempting to boot slot 0.
   ```

   or:

   ```text
   Attempting to boot slot 1.
   ```

5. Upload the MCUboot package.

   The recommended flow is to use nRF Connect Device Manager and select:

   ```text
   build_dts_ns/dfu_mcuboot.zip
   ```

   The zip manifest identifies both S0 and S1 variants. A tool that understands
   the Nordic DFU manifest can select the correct inactive MCUboot slot.

   If using a lower-level tool that only accepts raw binaries, choose the
   variant for the inactive slot:

   - If B0 currently boots slot 0, upload the S1 variant:
     `signed_by_mcuboot_and_b0_mcuboot_s1_variant.bin`.
   - If B0 currently boots slot 1, upload the S0 variant:
     `signed_by_mcuboot_and_b0_mcuboot.bin`.

6. Mark the uploaded image for test or permanent upgrade, then reset.

   With `mcumgr`, the raw flow is similar to:

   ```powershell
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image upload <selected-mcuboot-variant.bin>
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image list
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image test <uploaded-image-hash>
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A reset
   ```

7. Verify the B0 and MCUboot logs.

   After reboot, B0 should verify the new MCUboot image and may switch to the
   other slot:

   ```text
   Attempting to boot slot 1.
   I: Firmware signature verified.
   Firmware version 3
   Booting (...)
   *** Booting MCUboot ...
   ```

8. If the client reports the image as unconfirmed, confirm it after successful
   boot:

   ```powershell
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image confirm
   ```

## Version Changes

Application version:

```conf
CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION="1.0.2+4"
```

MCUboot version:

```conf
SB_CONFIG_SECURE_BOOT_MCUBOOT_VERSION="0.0.1+2"
CONFIG_FW_INFO_FIRMWARE_VERSION=2
```

The application prints its version at boot. B0 prints the MCUboot firmware
version it validated.

## Signing Keys and Public Key Provisioning

The current sample uses the MCUboot default development Ed25519 private key:

```text
D:/workspace/NCS/v3.3.0/bootloader/mcuboot/root-ed25519.pem
```

It is selected in `sysbuild.conf`:

```conf
SB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE="${ZEPHYR_MCUBOOT_MODULE_DIR}/root-ed25519.pem"
SB_CONFIG_BOOT_SIGNATURE_KEY_FILE="${ZEPHYR_MCUBOOT_MODULE_DIR}/root-ed25519.pem"
```

The private key and public key are a pair:

- The private key is used by the build system to sign images.
- The public key is derived from the private key.
- Verifiers use only the public key.
- The private key must not be provisioned to the device.

There are two verification relationships in this boot chain:

- B0 verifies MCUboot.
  - MCUboot S0/S1 images are signed with the private key selected by
    `SB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE`.
  - The matching public key is provisioned into KMU as `BL_PUBKEY`.
  - In this sample, `sysbuild/CMakeLists.txt` generates `keyfile.json` for that
    KMU provisioning step.

- MCUboot verifies the TF-M + application image.
  - Application images are signed with the private key selected by
    `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE`.
  - The matching public key is compiled into MCUboot.
  - MCUboot uses that compiled-in public key to verify APP DFU images.

For simplicity, this sample uses the same default key for both relationships.
For a customer project, using separate keys is recommended:

- one key pair for B0 verifying MCUboot;
- one key pair for MCUboot verifying TF-M + application images.

### Generate Customer Keys

Create a local `keys` directory:

```powershell
cd D:\workspace\NCS\v3.3.0\nrf\samples\dfu\upgradable_mcuboot_tfm_smp_svr
mkdir keys
```

Generate a key pair for B0 verifying MCUboot:

```powershell
python D:\workspace\NCS\v3.3.0\bootloader\mcuboot\scripts\imgtool.py keygen `
  -k keys\b0-root-ed25519.pem `
  -t ed25519
```

Generate a key pair for MCUboot verifying TF-M + application images:

```powershell
python D:\workspace\NCS\v3.3.0\bootloader\mcuboot\scripts\imgtool.py keygen `
  -k keys\mcuboot-root-ed25519.pem `
  -t ed25519
```

Optional: print or export the public key material derived from each private key:

```powershell
python D:\workspace\NCS\v3.3.0\bootloader\mcuboot\scripts\imgtool.py getpub `
  -k keys\b0-root-ed25519.pem

python D:\workspace\NCS\v3.3.0\bootloader\mcuboot\scripts\imgtool.py getpub `
  -k keys\mcuboot-root-ed25519.pem
```

### Configure the Customer Keys

Update `sysbuild.conf`:

```conf
SB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE="${APP_DIR}/keys/b0-root-ed25519.pem"
SB_CONFIG_BOOT_SIGNATURE_KEY_FILE="${APP_DIR}/keys/mcuboot-root-ed25519.pem"
```

Also update `sysbuild/CMakeLists.txt`, because this sample explicitly generates
a clean KMU provisioning file. The key used for `BL_PUBKEY` must match
`SB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE`:

```cmake
set(b0_signing_key ${CMAKE_CURRENT_LIST_DIR}/../keys/b0-root-ed25519.pem)
```

If this file is not updated, B0 can be provisioned with the old public key while
MCUboot is signed by the new private key. In that case B0 will reject MCUboot.

After changing keys, always run a pristine build:

```powershell
west build -p always -b nrf54lm20dk/nrf54lm20a/cpuapp/ns --sysbuild -d build -- -DEXTRA_CONF_FILE=bt.conf
```

Then flash with `--recover`, because the KMU public key must be provisioned
again:

```powershell
west flash --recover --no-rebuild -d build
```

Do not commit private keys to a public repository. Keep production signing keys
in a controlled key-management process.

## Known Limitations

- This is a local NCS prototype, not an upstream-supported board configuration.
- The default MCUboot signing key is used for development only.
- The TF-M and Zephyr local patches must be carried when rebuilding from a clean
  SDK checkout.
- The non-secure application cannot inspect secure primary slot contents.
  MCUmgr slot 0 state is therefore synthesized from the current application
  configuration.
- APP updates and MCUboot updates share the secondary TF-M + application slot as
  the upload staging area. Do not attempt both updates at the same time.
- Always use a pristine rebuild after changing the DTS partition layout or TF-M
  flash layout patches.

## Useful Verification Commands

Check generated vectors in the signed app hex:

```powershell
# Expected:
# 0x42000: MCUboot image header
# 0x42800: TF-M secure vector table
# 0x82800: non-secure app vector table
```

Flash the already built image:

```powershell
$env:ZEPHYR_BASE='D:\workspace\NCS\v3.3.0\zephyr'
& "D:\workspace\NCS\toolchains\936afb6332\opt\bin\python.exe" -m west flash `
  --recover `
  --no-rebuild `
  -d build_dts_ns `
  --dev-id 1051878096
```

Expected advertising log:

```text
<inf> smp_bt_sample: Advertising successfully started
<inf> smp_sample: APP image version: ...
```
