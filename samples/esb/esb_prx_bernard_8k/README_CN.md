# ESB PRX Bernard 8kHz 示例

## 概述

本示例演示了使用 Enhanced ShockBurst (ESB) 协议的接收端 (PRX) 实现，可以接收来自 PTX 端的高速数据包（最高 8kHz），并通过 USB HID 设备将数据转发给主机。该示例主要用于接收运动轨迹数据，并可作为 USB 鼠标设备使用。

## 主要特性

- **高速数据接收**：支持接收最高 8kHz 的数据包
- **USB HID 转发**：将接收到的 ESB 数据包转发到 USB HID 接口
- **多种工作模式**：
  - USB 转发模式：将接收的数据通过 USB 发送给主机
  - 发送编程模式：可向 PTX 端回传数据（如果配置了发送长度）
- **实时统计**：定期显示接收和处理的数据包数量
- **按键控制**：通过按键切换工作模式

## 硬件要求

- 支持的 Nordic 开发板（如 nRF54L15 DK）
- USB 连接用于 HID 设备功能
- 至少 1 个按键用于控制 USB 转发开关（可选）

## 工作原理

1. **初始化**：
   - 启动时钟和 ESB 协议
   - 初始化 USB 设备（HID 鼠标）
   - 配置定时器用于周期性统计

2. **接收模式**：
   - 持续监听 ESB 数据包
   - 接收到数据后存入接收 FIFO
   - 根据当前模式处理数据

3. **数据处理**：
   - **USB 转发模式**：将接收的运动数据（dx, dy）转换为 USB HID 报告发送给主机
   - **发送编程模式**：可配置向 PTX 端回传应答数据

4. **统计输出**：
   - 每秒输出一次统计信息
   - 显示接收数量、USB 发送数量等

## 按键功能

| 按键 | 功能 |
|------|------|
| 按键1 (BTN1) | 切换是否将接收到的数据转发到 USB 端口 |
| 按键2 (BTN2) | 无功能 |
| 按键3 (BTN3) | 无功能 |
| 按键4 (BTN4) | 无功能 |

## USB HID 功能

本示例作为 USB HID 鼠标设备：
- **供应商 ID (VID)**：Nordic Semiconductor
- **产品 ID (PID)**：自定义
- **HID 报告**：包含鼠标移动数据（X、Y 位移）
- **数据转换**：将 ESB 接收的 8 位有符号值转换为 USB HID 报告格式

## ESB 配置

- **协议**：ESB_DPL（动态有效载荷长度）
- **比特率**：4 Mbps
- **重传延迟**：450 微秒
- **重传次数**：400 次
- **RF 信道**：40
- **基地址0**：0xE7E7E7E7
- **基地址1**：0xC2C2C2C2
- **工作模式**：PRX（主接收）

## 构建和烧录

```bash
# 进入项目目录
cd nrf/samples/esb/esb_prx_bernard_8k

# 构建项目（以 nRF54L15 DK 为例）
west build -b nrf54l15dk/nrf54l15/cpuapp

# 烧录到开发板
west flash
```

## 日志输出示例

```
*** Booting nRF Connect SDK v3.1.1 ***
*** Using Zephyr OS v4.1.99-ff8f0c579eeb ***
I: Enhanced ShockBurst prx sample
I: Initialization complete
I: Print packet counts every 1.00 seconds
I: ACK payload length = 0 bytes
I: Setting up for 
packet receiption
I: # USB report = 0.
I: # USB report = 3615.
I: # USB report = 7969.
I: # USB report = 7952.
I: # USB report = 7957.
I: # USB report = 7990.
I: # USB report = 7990.
I: # USB report = 7990.
I: # USB report = 7990.
```

## 配置选项

主要配置在 `prj.conf` 中：
- `CONFIG_ESB=y`：启用 ESB 支持
- `CONFIG_ESB_FAST_SWITCHING=y`：启用快速切换
- `CONFIG_ESB_FAST_CHANNEL_SWITCHING=y`：启用快速信道切换
- `CONFIG_ESB_RX_FIFO_SIZE`：接收 FIFO 大小
- `CONFIG_ESB_PRX_TX_LENGTH`：PRX 端发送数据长度（0 表示禁用）
- USB 相关配置（USBD、HID 等）

## 与 PTX 端配对使用

1. 先启动 PRX（本示例）
2. 将开发板通过 USB 连接到 PC
3. PC 会识别为 USB HID 鼠标设备
4. 启动 PTX 端并按下按键3或按键4开始发送
5. 观察 PC 上鼠标指针的圆周运动

## 性能优化

- 使用了代码数据重定位（`CONFIG_CODE_DATA_RELOCATION=y`）
- 禁用了不必要的功能（电源管理、时间片轮转）以降低延迟
- ESB 快速切换减少了收发延迟
- 优化的中断优先级配置

## 故障排除

1. **未接收到数据**：
   - 检查 PTX 端是否启动并开始发送
   - 确认两端 ESB 配置一致
   - 检查 RF 信道和地址设置

2. **USB 设备未识别**：
   - 检查 USB 连接
   - 查看日志确认 USB 初始化成功
   - 尝试重新插拔 USB

3. **数据丢失**：
   - 增加接收 FIFO 大小
   - 检查 USB 传输速率是否跟上接收速率
   - 降低 PTX 端发送速率

## 注意事项

1. 需要配合 ESB PTX（发送端）示例使用
2. USB 功能需要开发板支持 USB 外设
3. 高速率接收对 CPU 和 USB 性能有一定要求
4. 建议在生产环境中修改 ESB 地址以避免干扰

## 许可证

SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

Copyright (c) 2018 Nordic Semiconductor ASA
