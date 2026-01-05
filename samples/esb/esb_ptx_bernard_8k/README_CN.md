# ESB PTX Bernard 8kHz 示例

## 概述

本示例演示了使用 Enhanced ShockBurst (ESB) 协议的发送端 (PTX) 实现，可以以 8kHz 的高速率发送数据包。该示例模拟了一个循环运动轨迹（如鼠标的圆周运动），通过 ESB 协议将位置数据发送给接收端。

## 主要特性

- **高速数据传输**：支持最高 8kHz 的数据包发送速率
- **多种传输速率**：可通过按键在 unlimited、8kHz、7kHz、6kHz、5kHz、4kHz、3kHz、2kHz、1kHz、500Hz、200Hz、100Hz 之间切换
- **循环运动轨迹**：预定义 32 个点的圆形运动轨迹，模拟平滑的圆周运动
- **双向运动控制**：支持顺时针和逆时针运动
- **ESB 快速切换**：启用了 ESB 快速切换和快速信道切换功能
- **实时统计**：显示发送成功、失败和接收的数据包数量

## 硬件要求

- 支持的 Nordic 开发板（如 nRF54L15 DK）
- 至少 4 个按键用于控制

## 按键功能

| 按键 | 功能 |
|------|------|
| 按键1 (BTN1) | 增加发送速率（向下一个更低频率切换） |
| 按键2 (BTN2) | 降低发送速率（向上一个更高频率切换） |
| 按键3 (BTN3) | 启动/停止顺时针运动发送 |
| 按键4 (BTN4) | 启动/停止逆时针运动发送 |

## 数据包格式

发送的数据包包含 4 个字节：
- 字节0: 保留（0x00）
- 字节1: X 方向位移（dx，有符号 8 位）
- 字节2: Y 方向位移（dy，有符号 8 位）
- 字节3: 保留（0x00）

## 工作原理

1. **初始化**：启动时钟、ESB 协议、按键和定时器
2. **待机模式**：等待按键3或按键4启动发送
3. **发送模式**：
   - 根据当前运动方向（顺时针/逆时针）从圆形轨迹数组中取点
   - 按照选定的发送速率填充并发送数据包
   - 定期输出统计信息（发送数量、失败数量、接收数量等）
4. **速率调整**：在待机或发送过程中均可调整发送速率

## ESB 配置

- **协议**：ESB_DPL（动态有效载荷长度）
- **比特率**：4 Mbps
- **重传延迟**：450 微秒
- **重传次数**：400 次
- **RF 信道**：40
- **基地址0**：0xE7E7E7E7
- **基地址1**：0xC2C2C2C2
- **选择性自动应答**：启用

## 构建和烧录

```bash
# 进入项目目录
cd nrf/samples/esb/esb_ptx_bernard_8k

# 构建项目（以 nRF54L15 DK 为例）
west build -b nrf54l15dk/nrf54l15/cpuapp

# 烧录到开发板
west flash
```

## 日志输出示例

```
*** Booting nRF Connect SDK v3.1.1 ***
I: Enhanced ShockBurst ptx sample
I: Initialization complete
I: Started clockwise motion
I: Sent 1000 packets. Failed 0 packets. Received 0 packets.
I: Elapsed 125 milliseconds.
I: Sent 1000 packets. Failed 0 packets. Received 0 packets.
I: Elapsed 125 milliseconds.
```

## 配置选项

主要配置在 `prj.conf` 中：
- `CONFIG_ESB=y`：启用 ESB 支持
- `CONFIG_ESB_FAST_SWITCHING=y`：启用快速切换
- `CONFIG_ESB_FAST_CHANNEL_SWITCHING=y`：启用快速信道切换
- `CONFIG_ESB_TX_FIFO_SIZE`：发送 FIFO 大小
- `CONFIG_ESB_PTX_BATCH_SIZE`：批量发送大小

## 注意事项

1. 需要配合 ESB PRX（接收端）示例使用
2. 确保两端使用相同的 ESB 配置（地址、信道等）
3. 发送速率越高，CPU 占用率越高
4. 高速率下可能需要调整重传参数以保证可靠性

## 许可证

SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

Copyright (c) 2018 Nordic Semiconductor ASA
