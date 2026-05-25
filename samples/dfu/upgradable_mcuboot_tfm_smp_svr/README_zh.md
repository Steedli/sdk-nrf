# nRF54LM20A BLE SMP DFU：NSIB、可升级 MCUboot、TF-M 与 DTS 静态分区

这个示例是一个精简后的 `smp_svr` 工程，用于在
`nrf54lm20dk/nrf54lm20a/cpuapp/ns` 上验证 Bluetooth SMP DFU，启动链如下：

```text
NSIB / B0 -> MCUboot S0/S1 -> TF-M secure image -> non-secure BLE SMP app
```

这个工程的目标是支持：

- 通过 Bluetooth SMP 对应用程序做 OTA 升级。
- 支持 upgradable MCUboot，即 MCUboot 本身也可以通过 DFU 更新，然后由
  NSIB/B0 选择并启动。
- 启用 TF-M 的 non-secure application 构建。
- 使用 DTS 静态分区，并关闭 Partition Manager：
  `SB_CONFIG_PARTITION_MANAGER=n`。

这是一个开发验证示例。当前使用 MCUboot 默认 Ed25519 key，仅适合开发测试，
不应直接用于量产。

## 测试目标

- NCS：`v3.3.0`
- Board：`nrf54lm20dk/nrf54lm20a/cpuapp/ns`
- DFU transport：Bluetooth SMP
- 广播名：`NCS_DFU_LM20A`
- 测试使用的构建目录：`build_dts_ns`

## 工程结构和 Sysbuild 集成方式

这个示例是一个 sysbuild application。顶层 application 是 non-secure BLE SMP
server，NSIB/B0 和 MCUboot 是由 sysbuild 加入构建的 bootloader images。

关键目录结构如下：

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

application image 由以下文件配置：

- `prj.conf`
  - 使能 MCUmgr、image management、flash map 和 MCUboot app update。
  - 设置 `CONFIG_USE_DT_CODE_PARTITION=y`，让 app 使用 DTS 中的
    `zephyr,code-partition`。
  - 通过 `CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION` 设置 application signing
    version。

- `bt.conf`
  - 使能 Bluetooth peripheral mode 和 SMP-over-Bluetooth transport。
  - 增大 Bluetooth SMP buffer，提高 DFU throughput。

- `boards/nrf54lm20dk_nrf54lm20a_cpuapp_ns.overlay`
  - 为 non-secure build 提供 DTS 静态分区表。
  - 设置 `zephyr,code-partition = &slot0_ns_partition`。

`sysbuild.conf` 负责把 NSIB/B0 和 MCUboot 加入 sysbuild：

```conf
SB_CONFIG_PARTITION_MANAGER=n
SB_CONFIG_SECURE_BOOT_APPCORE=y
SB_CONFIG_BOOTLOADER_MCUBOOT=y
SB_CONFIG_BOOT_SIGNATURE_TYPE_ED25519=y
```

这些选项的作用如下：

- `SB_CONFIG_PARTITION_MANAGER=n`
  - 关闭 Partition Manager，使本示例使用 DTS fixed partitions。

- `SB_CONFIG_SECURE_BOOT_APPCORE=y`
  - 将 NSIB/B0 image 加入 sysbuild 构建。
  - B0 成为 application core 的第一级不可变 bootloader。

- `SB_CONFIG_BOOTLOADER_MCUBOOT=y`
  - 将 MCUboot 作为第二级 bootloader image 加入构建。
  - 因为同时使能了 secure boot，sysbuild 会把 MCUboot 构建成 S0/S1 可升级布局，
    而不是单一固定 MCUboot partition。

- `SB_CONFIG_BOOT_SIGNATURE_TYPE_ED25519=y`
  - 为 secure boot 和 MCUboot image signing 选择 Ed25519 签名。

签名 key 在 `sysbuild.conf` 中配置：

```conf
SB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE="${ZEPHYR_MCUBOOT_MODULE_DIR}/root-ed25519.pem"
SB_CONFIG_BOOT_SIGNATURE_KEY_FILE="${ZEPHYR_MCUBOOT_MODULE_DIR}/root-ed25519.pem"
```

- `SB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE`
  - 用于给 MCUboot 签名，供 B0/NSIB 验证。
  - 对应 public key 会作为 `BL_PUBKEY` provision 到 KMU。

- `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE`
  - 用于给 TF-M + application image 签名，供 MCUboot 验证。
  - 对应 public key 会编译进 MCUboot。

B0/NSIB 的 image-specific 配置位于 `sysbuild/b0/`：

- `sysbuild/b0/prj.conf`
  - 设置 `CONFIG_IS_SECURE_BOOTLOADER=y`。
  - 使能 secure boot validation 和 secure boot storage。
  - 保持 B0 配置尽量精简。

- `sysbuild/b0/boards/nrf54lm20dk_nrf54lm20a_cpuapp.overlay`
  - 选择 B0 的 code partition。

MCUboot 的 image-specific 配置位于 `sysbuild/mcuboot/`：

- `sysbuild/mcuboot/prj.conf`
  - 作为空的基础配置文件，board-specific 设置放在 board 文件中。

- `sysbuild/mcuboot/boards/nrf54lm20dk_nrf54lm20a_cpuapp.conf`
  - 根据 `s0_partition` / `s1_partition` 的大小调整 MCUboot 配置。
  - 设置 MCUboot log。
  - 设置 `CONFIG_FW_INFO_FIRMWARE_VERSION`。
  - 确保 MCUboot 能放入 S0/S1 slot。

- `sysbuild/mcuboot/boards/nrf54lm20dk_nrf54lm20a_cpuapp.overlay`
  - include application core 的公共静态分区 overlay。
  - 将 MCUboot 的 `zephyr,code-partition` 设置为 `s0_partition`。
  - sysbuild 还会生成一个链接到 `s1_partition` 的 MCUboot S1 variant。

最后，`sysbuild/CMakeLists.txt` 会生成干净的 `keyfile.json`：

```text
west ncs-provision upload --keyname BL_PUBKEY --dry-run ...
```

这个文件会被 `west flash` / `nrfutil` 使用，在执行 `west flash --recover` 时
将 B0 public key provision 到 KMU。

## 启动和安全模型

NSIB/B0 是第一级不可变 bootloader。它会从 MCUboot S0 或 S1 slot 中验证并
启动 MCUboot。MCUboot 是第二级 bootloader，负责验证由 TF-M secure image
和 non-secure application 组成的 combined application image，然后跳转到
TF-M secure vector table。

### 验证流程

B0/NSIB 会先验证 MCUboot，验证通过后才允许 MCUboot 执行：

1. 复位后，B0 从 `b0_partition` 运行。
2. B0 决定先尝试哪个 MCUboot slot。通常先尝试 `s0_partition`，如果 boot
   状态或 monotonic counter 指示应使用另一个 slot，则尝试 `s1_partition`。
3. B0 从被选中的 S0/S1 slot 中读取 MCUboot image metadata 和 firmware
   information。
4. B0 使用写入 KMU 的 public key 验证 MCUboot 签名。这个 key 的名字是
   `BL_PUBKEY`。在本示例中，`sysbuild/CMakeLists.txt` 会生成
   `keyfile.json`，`west flash` / `nrfutil` 会使用这个文件完成 KMU
   provisioning。
5. 如果签名和版本检查通过，B0 会按需更新 monotonic counter，然后跳转到
   MCUboot。例如从 S0 启动时，跳转地址是 `0x8800`。
6. 如果当前选择的 MCUboot slot 无效，B0 可以根据 boot 状态继续尝试另一个
   MCUboot slot。

然后 MCUboot 会验证 combined TF-M + application image：

1. MCUboot 从 B0 选中的 S0/S1 slot 中启动。
2. MCUboot 将 `slot0_partition` 作为 primary TF-M + application slot，
   将 `slot1_partition` 作为 secondary TF-M + application slot。
3. MCUboot 读取被选中的 application slot 起始处的 image header。对于
   primary slot，这个地址是 `0x42000`。
4. image payload 从 `0x800` 字节 MCUboot header 之后开始。因此在 primary
   slot 中，TF-M 从 `0x42800` 开始，non-secure app vector table 位于
   `0x82800`。
5. MCUboot 解析 image TLV，检查 image hash，并使用编译进 MCUboot 的
   application verification key 验证签名。
6. 如果 `slot1_partition` 中存在 pending update，MCUboot 会执行当前配置的
   swap 操作。本示例使用的是 `swap using move`，swap 后从
   `slot0_partition` 启动新 image。
7. 当选中的 TF-M + application image 验证通过后，MCUboot 跳转到
   `slot0_partition + 0x800` 处的 TF-M secure vector table。
8. TF-M 配置 secure/non-secure memory boundaries，将 non-secure VTOR/MSP
   设置到 `slot0_ns_partition + 0x800` 处的 non-secure vector table，然后
   启动 non-secure application。

应用运行在 non-secure 域。因此应用不能直接读取 secure flash 区域，例如：

- B0：`0x00000`
- MCUboot S0/S1：`0x08000` 和 `0x20000`
- TF-M storage partitions
- primary application slot 中 secure 部分的起始地址：`0x42000`

这个访问限制对 MCUmgr image management 很重要。Zephyr 默认的 image
management 实现会读取 primary slot header。在这个布局中，该 header 位于
primary TF-M + application slot 的 secure 部分，因此需要一个本地 Zephyr patch
来避免 secure fault。

## 静态 Flash 分区布局

这个工程关闭 Partition Manager，并通过 DTS 定义 flash layout。

bootloader 和 storage 相关区域如下：

- `b0_partition`：`0x00000000`，大小 `0x00008000`
- `s0_partition`：`0x00008000`，大小 `0x00018000`
- `s1_partition`：`0x00020000`，大小 `0x00018000`
- `tfm_its_partition`：`0x00038000`，大小 `0x00004000`
- `tfm_otp_partition`：`0x0003c000`，大小 `0x00002000`
- `tfm_ps_partition`：`0x0003e000`，大小 `0x00004000`
- `storage_partition`：`0x001e4000`，大小 `0x00001000`

combined TF-M + application slots 如下：

- `slot0_partition`：`0x00042000`，大小 `0x000d1000`
- `slot1_partition`：`0x00113000`，大小 `0x000d1000`

每个 combined TF-M + application slot 都是一个 `fixed-subpartitions` 节点：

- `slot0_s_partition`：slot 内相对偏移 `0x00000000`，大小 `0x00040000`
- `slot0_ns_partition`：slot 内相对偏移 `0x00040000`，大小 `0x00091000`
- `slot1_s_partition`：slot 内相对偏移 `0x00000000`，大小 `0x00040000`
- `slot1_ns_partition`：slot 内相对偏移 `0x00040000`，大小 `0x00091000`

`slot0_partition` 的详细结构如下：

```text
slot0_partition，绝对地址 0x00042000，大小 0x000d1000

0x00042000 - 0x000427ff
    MCUboot image header 及其 padding 区域，大小 0x800。
    真正的 MCUboot image header 从 0x42000 开始。

slot0_s_partition，slot 内相对偏移 0x00000000，绝对地址 0x00042000，大小 0x00040000
    0x00042800
        TF-M secure vector table。
        TF-M 通过 TFM_MCUBOOT_OFFSET = 0x800 链接到 MCUboot header 之后。
    0x00042800 - 0x00081fff
        TF-M secure image code、veneers 以及 secure-side read-only data。

slot0_ns_partition，slot 内相对偏移 0x00040000，绝对地址 0x00082000，大小 0x00091000
    0x00082000 - 0x000827ff
        non-secure image 保留的 header/pad 区域，大小 0x800。
        这里不是第二个独立的 MCUboot image header，而是 Zephyr/TF-M
        non-secure image boot offset。
    0x00082800
        non-secure application vector table。
    0x00082800 - 0x00112fff
        non-secure application image 区域。
```

`slot1_partition` 的结构相同，只是整体移动到 secondary slot：

```text
slot1_partition，绝对地址 0x00113000，大小 0x000d1000

0x00113000 - 0x001137ff
    MCUboot image header 及其 padding 区域，大小 0x800。

slot1_s_partition，slot 内相对偏移 0x00000000，绝对地址 0x00113000，大小 0x00040000
    0x00113800
        secondary image 的 TF-M secure vector table。
    0x00113800 - 0x00152fff
        secondary image 的 TF-M secure image 区域。

slot1_ns_partition，slot 内相对偏移 0x00040000，绝对地址 0x00153000，大小 0x00091000
    0x00153000 - 0x001537ff
        non-secure image 保留的 header/pad 区域，大小 0x800。
    0x00153800
        secondary image 的 non-secure application vector table。
    0x00153800 - 0x001e3fff
        secondary image 的 non-secure application image 区域。
```

non-secure application 使用如下 code partition：

```dts
chosen {
	zephyr,code-partition = &slot0_ns_partition;
};
```

最终 signed application image 中几个关键地址为：

- MCUboot image header：`0x42000`
- TF-M secure vector table：`0x42800`
- non-secure application vector table：`0x82800`

## 关键工程文件

- `sysbuild.conf`
  - 关闭 Partition Manager。
  - 使能 NSIB/B0 和 MCUboot。
  - 选择 Ed25519 签名。
  - 开发阶段使用 MCUboot 默认 root key。
  - 通过 `SB_CONFIG_SECURE_BOOT_MCUBOOT_VERSION` 设置 MCUboot S0/S1 版本。

- `sysbuild/CMakeLists.txt`
  - 生成干净的 `keyfile.json`，用于 KMU provisioning。
  - 使用 `west ncs-provision upload --dry-run`。
  - 避免重复 KMU key entry 导致 provisioning 失败。

- `boards/nrf54lm20dk_nrf54lm20a_cpuapp.overlay`
  - 定义 secure CPUAPP 侧 flash layout，包括 B0、MCUboot、TF-M、app slots
    和 storage。

- `boards/nrf54lm20dk_nrf54lm20a_cpuapp_ns.overlay`
  - 为 non-secure app build 定义同样的 layout。
  - 选择 `slot0_ns_partition` 作为 non-secure code partition。

- `prj.conf`
  - 使能 MCUmgr、image management、flash map 和 MCUboot app update。
  - 通过 `CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION` 设置 application image 版本。
  - 通过
    `CONFIG_MCUBOOT_EXTRA_IMGTOOL_ARGS="--slot-size 0xd1000 --hex-addr 0x42000"`
    强制指定签名镜像地址。

- `bt.conf`
  - 使能 BLE peripheral 和 Bluetooth SMP transport。
  - 增大 MTU 和 buffer，以提高 DFU throughput。

- `sysbuild/mcuboot/boards/nrf54lm20dk_nrf54lm20a_cpuapp.conf`
  - 控制 MCUboot 尺寸，使其能放入 S0/S1 slot。
  - 设置 `CONFIG_FW_INFO_FIRMWARE_VERSION`。
  - 在当前 nRF54L secure bootloader 配置下关闭 `FPROTECT`。

- `src/main.c`
  - 启动 Bluetooth SMP advertising。
  - 打印 build time 和 application image version。

## 本地 NCS 源码修改

这个示例需要修改 NCS tree 内的一些本地源码。这些文件不在 application
目录中，但对于这个 DTS-only + TF-M + MCUmgr 原型是必须的。

修改过的本地 NCS 文件如下：

- `modules/tee/tf-m/trusted-firmware-m/platform/ext/target/nordic_nrf/common/nrf54lm20a/partition/flash_layout.h`
  - 在 Partition Manager 关闭时增加 DTS-based flash layout 支持。
  - 从生成的 devicetree 宏读取 `slot0_s_partition`、`slot0_ns_partition`、
    `tfm_ps_partition`、`tfm_its_partition`、`tfm_otp_partition` 和
    `storage_partition`。

- `modules/tee/tf-m/trusted-firmware-m/platform/ext/target/nordic_nrf/common/nrf54lm20a/partition/region_defs.h`
  - 对 secure 和 non-secure code start address 应用 `TFM_MCUBOOT_OFFSET`。
  - SAU/MPC 权限仍覆盖 `slot0_ns_partition` 起点开始的完整 non-secure
    partition，但执行入口使用 MCUboot header 之后的 vector address。

- `modules/tee/tf-m/trusted-firmware-m/platform/ext/target/nordic_nrf/common/core/target_cfg.c`
  - 在选择 non-secure VTOR/MSP 地址时使用 `TFM_MCUBOOT_OFFSET`。
  - 这样 TF-M 会跳转到 `0x82800`，而不是 `0x82000`。

- `nrf/modules/trusted-firmware-m/tfm_boards/partition/region_defs.h`
  - 在 Partition Manager 关闭时，对 Nordic TF-M board 通用
    `NS_CODE_START` 应用 `TFM_MCUBOOT_OFFSET`。

- `zephyr/subsys/mgmt/mcumgr/grp/img_mgmt/src/img_mgmt.c`
  - 避免 non-secure app 读取 primary TF-M + application slot 的 secure 部分。
  - 查询 slot 0 image state 时，不再读取 `0x42000`，而是从
    `CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION` 返回当前 app version。
  - 这样可以避免 APP DFU image state 查询时触发 secure fault。

这些修改都是当前实验使用的本地 patch。量产或共享 SDK tree 时，建议将它们
整理成明确 patch，并在切换 NCS 版本时仔细 rebase。

## 开发过程中遇到的问题

以下是这个例子搭建过程中遇到并修复的问题。

- KMU key provisioning 出现重复 entry
  - 现象：`west flash` 时出现 `KEY Provision Error`。
  - 修复：在 `sysbuild/CMakeLists.txt` 中生成干净的 `keyfile.json`，初次烧录
    使用 `west flash --recover`。

- MCUboot 找不到可启动 application image
  - 现象：日志中出现 `Image in the primary slot is not valid` 和
    `Unable to find bootable image`。
  - 原因：combined TF-M + NS image 大于原先 secure slot size。MCUboot 只按
    `0x40000` 的 slot 进行校验，但实际 image 超出了这个范围。
  - 修复：将 `slot0_partition` 和 `slot1_partition` 改为完整 TF-M + application
    slot，大小为 `0xd1000`。

- signed image 被 MCUboot header 偏移影响
  - 现象：MCUboot 打印 `Jumping to the first image slot` 后，app 没有启动。
  - 原因：TF-M、NS app 和 `imgtool --pad-header` 对 header offset 的理解不一致。
  - 修复：将 `slot*_s_partition` 定义为 slot-relative `0x0`，由 TF-M 自己应用
    `TFM_MCUBOOT_OFFSET`。

- TF-M 跳转到了错误的 non-secure vector table
  - 现象：MCUboot jump 之后启动停止。
  - 原因：TF-M 使用 non-secure partition base `0x82000`，但 NS vector table
    位于 MCUboot header 之后，即 `0x82800`。
  - 修复：patch TF-M `target_cfg.c` 和 region definitions，使 VTOR/MSP 选择时
    使用 `TFM_MCUBOOT_OFFSET`。

- APP DFU 时触发 secure fault
  - 现象：APP 升级时在地址 `0x42000` 触发 secure fault。
  - 原因：non-secure app 中的 MCUmgr image state 处理逻辑尝试读取 primary
    primary TF-M + application slot header，而该地址属于 secure flash 区域。
  - 修复：patch `img_mgmt_read_info()`，让 slot 0 state 不再从 non-secure app
    读取 secure flash。

## 编译

使用 nRF Connect SDK toolchain 环境。在本目录执行：

```powershell
$env:ZEPHYR_BASE='D:\workspace\NCS\v3.3.0\zephyr'
& "D:\workspace\NCS\toolchains\936afb6332\opt\bin\python.exe" -m west build `
  -p always `
  -b nrf54lm20dk/nrf54lm20a/cpuapp/ns `
  --sysbuild `
  -d build_dts_ns `
  -- '-DEXTRA_CONF_FILE=bt.conf'
```

预期生成的 DFU 产物：

- `build_dts_ns/dfu_application.zip`
  - 内含 `smp_svr.signed.bin`。
  - 当前 manifest image index：`0`。
  - 当前 manifest load address：`0x42000`。

- `build_dts_ns/dfu_mcuboot.zip`
  - 内含 `signed_by_mcuboot_and_b0_mcuboot.bin`。
  - 内含 `signed_by_mcuboot_and_b0_mcuboot_s1_variant.bin`。
  - 它们分别是 NSIB/B0 使用的 S0 和 S1 MCUboot variant。

## 初次烧录

初次烧录，或者需要重新进行 KMU provisioning 时，使用 `--recover`。仅使用
`--erase` 不足以覆盖 secure boot key provisioning 场景。

```powershell
$env:ZEPHYR_BASE='D:\workspace\NCS\v3.3.0\zephyr'
& "D:\workspace\NCS\toolchains\936afb6332\opt\bin\python.exe" -m west flash `
  --recover `
  --no-rebuild `
  -d build_dts_ns `
  --dev-id 1051878096
```

烧录成功后的典型启动日志：

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

## 通过 Bluetooth 升级 Application

使用这个流程升级 non-secure application，不改变 B0 和 MCUboot。

1. 在 `prj.conf` 中修改 application version。

   示例：

   ```conf
   CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION="1.0.3+5"
   ```

2. 重新编译。

   ```powershell
   $env:ZEPHYR_BASE='D:\workspace\NCS\v3.3.0\zephyr'
   & "D:\workspace\NCS\toolchains\936afb6332\opt\bin\python.exe" -m west build `
     -b nrf54lm20dk/nrf54lm20a/cpuapp/ns `
     --sysbuild `
     -d build_dts_ns `
     -- '-DEXTRA_CONF_FILE=bt.conf'
   ```

3. 使用生成的升级包：

   ```text
   build_dts_ns/dfu_application.zip
   ```

4. 使用 nRF Connect Device Manager：

   - 扫描 `NCS_DFU_LM20A`。
   - 连接设备。
   - 打开 image 或 DFU 页面。
   - 选择 `build_dts_ns/dfu_application.zip`。
   - 开始上传。
   - 根据工具流程，将上传的 image 标记为 test 或 confirm。
   - 通过 app 发送 OS reset，或者手动复位设备。

5. 如果使用 `mcumgr` CLI：

   先从 `dfu_application.zip` 中解压 `smp_svr.signed.bin`，然后执行类似命令：

   ```powershell
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image upload smp_svr.signed.bin
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image list
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image test <uploaded-image-hash>
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A reset
   ```

6. 新 APP 启动后确认 image。

   如果之前只是 test image，需要执行：

   ```powershell
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image confirm
   ```

7. 查看 UART 日志。

   APP 会打印：

   ```text
   APP image version: 1.0.3+5
   ```

## 通过 Bluetooth 升级 MCUboot

MCUboot 存放在两个由 NSIB/B0 控制的 slot 中：

- S0：`s0_partition`，地址 `0x8000`
- S1：`s1_partition`，地址 `0x20000`

B0 在 reset 时选择其中一个 slot。MCUboot 本身处理升级选择，B0 在下一次启动时
验证被选择的 slot。

1. 修改 MCUboot version。

   在 `sysbuild.conf` 中修改：

   ```conf
   SB_CONFIG_SECURE_BOOT_MCUBOOT_VERSION="0.0.1+3"
   ```

   在 `sysbuild/mcuboot/boards/nrf54lm20dk_nrf54lm20a_cpuapp.conf` 中修改：

   ```conf
   CONFIG_FW_INFO_FIRMWARE_VERSION=3
   ```

   测试时建议这两个版本同步递增，方便从串口日志判断当前启动的是哪个版本。

2. 重新编译。

   ```powershell
   $env:ZEPHYR_BASE='D:\workspace\NCS\v3.3.0\zephyr'
   & "D:\workspace\NCS\toolchains\936afb6332\opt\bin\python.exe" -m west build `
     -p always `
     -b nrf54lm20dk/nrf54lm20a/cpuapp/ns `
     --sysbuild `
     -d build_dts_ns `
     -- '-DEXTRA_CONF_FILE=bt.conf'
   ```

3. 使用生成的升级包：

   ```text
   build_dts_ns/dfu_mcuboot.zip
   ```

   这个包包含两个 MCUboot variant：

   - `signed_by_mcuboot_and_b0_mcuboot.bin`
     - 链接到 S0，load address 为 `0x8000`。
   - `signed_by_mcuboot_and_b0_mcuboot_s1_variant.bin`
     - 链接到 S1，load address 为 `0x20000`。

4. 确认当前 active MCUboot slot。

   查看 reset 后的 B0 日志：

   ```text
   Attempting to boot slot 0.
   ```

   或：

   ```text
   Attempting to boot slot 1.
   ```

5. 上传 MCUboot package。

   推荐使用 nRF Connect Device Manager，并选择：

   ```text
   build_dts_ns/dfu_mcuboot.zip
   ```

   zip manifest 中包含 S0 和 S1 两个 variant。理解 Nordic DFU manifest 的工具
   可以根据当前状态选择正确的 inactive MCUboot slot。

   如果使用只接受 raw binary 的低层工具，需要手动选择 inactive slot 对应的
   variant：

   - 如果 B0 当前启动 slot 0，则上传 S1 variant：
     `signed_by_mcuboot_and_b0_mcuboot_s1_variant.bin`。
   - 如果 B0 当前启动 slot 1，则上传 S0 variant：
     `signed_by_mcuboot_and_b0_mcuboot.bin`。

6. 将上传的 image 标记为 test 或 permanent upgrade，然后 reset。

   使用 `mcumgr` 时，raw 流程类似：

   ```powershell
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image upload <selected-mcuboot-variant.bin>
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image list
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image test <uploaded-image-hash>
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A reset
   ```

7. 通过 B0 和 MCUboot 日志验证。

   重启后，B0 应验证新的 MCUboot image，并可能切换到另一个 slot：

   ```text
   Attempting to boot slot 1.
   I: Firmware signature verified.
   Firmware version 3
   Booting (...)
   *** Booting MCUboot ...
   ```

8. 如果客户端显示 image 未确认，在成功启动后进行确认：

   ```powershell
   mcumgr --conntype ble --connstring peer_name=NCS_DFU_LM20A image confirm
   ```

## 版本号修改

Application version：

```conf
CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION="1.0.2+4"
```

MCUboot version：

```conf
SB_CONFIG_SECURE_BOOT_MCUBOOT_VERSION="0.0.1+2"
CONFIG_FW_INFO_FIRMWARE_VERSION=2
```

APP 会在启动后打印自己的版本。B0 会打印它验证到的 MCUboot firmware version。

## 签名私钥、公钥与 KMU Provisioning

当前示例使用的是 MCUboot 默认开发 Ed25519 私钥：

```text
D:/workspace/NCS/v3.3.0/bootloader/mcuboot/root-ed25519.pem
```

它在 `sysbuild.conf` 中配置：

```conf
SB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE="${ZEPHYR_MCUBOOT_MODULE_DIR}/root-ed25519.pem"
SB_CONFIG_BOOT_SIGNATURE_KEY_FILE="${ZEPHYR_MCUBOOT_MODULE_DIR}/root-ed25519.pem"
```

私钥和公钥是一对：

- 私钥由 build system 用来给 image 签名。
- 公钥可以从私钥中派生出来。
- 验证方只需要公钥。
- 私钥不应该写入设备，也不应该泄露。

当前启动链中有两层验证关系：

- B0 验证 MCUboot。
  - MCUboot S0/S1 image 使用 `SB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE`
    指定的私钥签名。
  - 对应的公钥会作为 `BL_PUBKEY` provision 到 KMU。
  - 在本示例中，`sysbuild/CMakeLists.txt` 会生成用于 KMU provisioning 的
    `keyfile.json`。

- MCUboot 验证 TF-M + application image。
  - application image 使用 `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` 指定的私钥签名。
  - 对应的公钥会编译进 MCUboot。
  - MCUboot 使用这个内置公钥验证 APP DFU image。

为了简化示例，当前这两层验证使用同一把默认 key。客户项目中建议使用两套
独立 key：

- 一套 key 用于 B0 验证 MCUboot；
- 一套 key 用于 MCUboot 验证 TF-M + application image。

### 生成客户自己的 key

在 sample 目录下创建本地 `keys` 目录：

```powershell
cd D:\workspace\NCS\v3.3.0\nrf\samples\dfu\upgradable_mcuboot_tfm_smp_svr
mkdir keys
```

生成用于 B0 验证 MCUboot 的 key pair：

```powershell
python D:\workspace\NCS\v3.3.0\bootloader\mcuboot\scripts\imgtool.py keygen `
  -k keys\b0-root-ed25519.pem `
  -t ed25519
```

生成用于 MCUboot 验证 TF-M + application image 的 key pair：

```powershell
python D:\workspace\NCS\v3.3.0\bootloader\mcuboot\scripts\imgtool.py keygen `
  -k keys\mcuboot-root-ed25519.pem `
  -t ed25519
```

可选：打印或导出由私钥派生出的公钥内容：

```powershell
python D:\workspace\NCS\v3.3.0\bootloader\mcuboot\scripts\imgtool.py getpub `
  -k keys\b0-root-ed25519.pem

python D:\workspace\NCS\v3.3.0\bootloader\mcuboot\scripts\imgtool.py getpub `
  -k keys\mcuboot-root-ed25519.pem
```

### 配置客户自己的 key

修改 `sysbuild.conf`：

```conf
SB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE="${APP_DIR}/keys/b0-root-ed25519.pem"
SB_CONFIG_BOOT_SIGNATURE_KEY_FILE="${APP_DIR}/keys/mcuboot-root-ed25519.pem"
```

还需要同步修改 `sysbuild/CMakeLists.txt`。因为本示例会显式生成干净的 KMU
provisioning 文件，生成 `BL_PUBKEY` 使用的 key 必须和
`SB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE` 一致：

```cmake
set(b0_signing_key ${CMAKE_CURRENT_LIST_DIR}/../keys/b0-root-ed25519.pem)
```

如果这里没有同步修改，就可能出现“MCUboot 使用新私钥签名，但 B0/KMU 里仍然是
旧公钥”的情况，结果是 B0 拒绝启动 MCUboot。

修改 key 后必须 pristine build：

```powershell
west build -p always -b nrf54lm20dk/nrf54lm20a/cpuapp/ns --sysbuild -d build -- -DEXTRA_CONF_FILE=bt.conf
```

然后使用 `--recover` 烧录，因为 KMU public key 需要重新 provision：

```powershell
west flash --recover --no-rebuild -d build
```

不要把私钥提交到公开仓库。量产项目应使用受控的 key-management 流程保存和使用
签名私钥。

## 已知限制

- 这是一个本地 NCS prototype，不是上游正式支持的 board 配置。
- 当前使用 MCUboot 默认签名 key，仅适合开发测试。
- TF-M 和 Zephyr 的本地 patch 必须在干净 SDK checkout 后重新应用。
- non-secure application 不能检查 secure primary slot 内容，因此 MCUmgr slot 0
  state 是根据当前 application 配置合成的。
- APP update 和 MCUboot update 会共用 secondary TF-M + application slot 作为上传
  暂存区，不要同时进行两类升级。
- 修改 DTS partition layout 或 TF-M flash layout patch 后，应使用 pristine rebuild。

## 常用验证命令

检查 signed app hex 中的关键 vector 地址：

```powershell
# 预期：
# 0x42000: MCUboot image header
# 0x42800: TF-M secure vector table
# 0x82800: non-secure app vector table
```

烧录已经编译好的镜像：

```powershell
$env:ZEPHYR_BASE='D:\workspace\NCS\v3.3.0\zephyr'
& "D:\workspace\NCS\toolchains\936afb6332\opt\bin\python.exe" -m west flash `
  --recover `
  --no-rebuild `
  -d build_dts_ns `
  --dev-id 1051878096
```

预期 advertising 日志：

```text
<inf> smp_bt_sample: Advertising successfully started
<inf> smp_sample: APP image version: ...
```
