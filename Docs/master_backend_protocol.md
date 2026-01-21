# Master-Backend 通信协议文档

## 文档信息

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-01-08 | 初始版本，基于代码自动生成 |

## 概述

本文档详细描述了Master（主控设备）与Backend（后台服务器）之间的双向通信协议。协议采用帧结构封装，支持多种命令和响应消息类型，实现设备配置、状态查询、控制指令等功能。

## 1. 帧格式 (Frame Format)

### 1.1 帧结构

所有数据采用**小端格式 (Little Endian)**。

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Delimiter 1 | uint8 | 1 Byte | 固定值 `0xAB` |
| Delimiter 2 | uint8 | 1 Byte | 固定值 `0xCD` |
| Packet ID | uint8 | 1 Byte | 包类型标识 |
| Fragments Sequence | uint8 | 1 Byte | 帧分片序号 |
| More Fragments Flag | uint8 | 1 Byte | 0: 无更多分片<br/>1: 有更多分片 |
| Packet Length | uint16 | 2 Bytes | 有效载荷长度（小端） |
| Payload | uint8[] | Variable | 实际数据内容 |

### 1.2 Packet ID 定义

| Packet ID | 值 | 描述 | 通信方向 |
|-----------|-----|------|---------|
| MASTER_TO_SLAVE | 0x00 | 主控→从机 | Master → Slave |
| SLAVE_TO_MASTER | 0x01 | 从机→主控 | Slave → Master |
| **BACKEND_TO_MASTER** | **0x02** | **后台→主控** | **Backend → Master** |
| **MASTER_TO_BACKEND** | **0x03** | **主控→后台** | **Master → Backend** |
| SLAVE_TO_BACKEND | 0x04 | 从机→后台 | Slave → Backend |

### 1.3 帧示例

```
AB CD 02 00 00 14 00 00 02 46 73 3B 4E 02 00 00 00 00 AC 22 30 02 02 00 00 00 00
|  |  |  |  |  |  |  |  |  |                    Payload                         |
|  |  |  |  |  |  |  |  |  +--- Slave Configuration Data
|  |  |  |  |  |  +--+  +------ Message ID (0x00 = SLAVE_CFG_MSG)
|  |  |  |  |  +--+------------ Packet Length (20 bytes, little endian)
|  |  |  |  +----------------- More Fragments Flag
|  |  |  +-------------------- Fragments Sequence
|  |  +----------------------- Packet ID (0x02 = Backend2Master)
+--+-------------------------- Frame Delimiters (0xAB 0xCD)
```

---

## 2. Backend2Master 协议 (后台→主控)

Packet ID: **0x02**

### 2.1 Payload 结构

| 字段 | 类型 | 长度 | 描述 |
|------|------|------|------|
| Message ID | uint8 | 1 Byte | 消息类型标识 |
| Message Data | uint8[] | Variable | 消息内容 |

### 2.2 消息类型列表

| 消息名称 | Message ID | 描述 |
|---------|-----------|------|
| SLAVE_CFG_MSG | 0x00 | 从机配置消息 |
| MODE_CFG_MSG | 0x01 | 模式配置消息 |
| SLAVE_RST_MSG | 0x02 | 从机复位消息 |
| CTRL_MSG | 0x03 | 运行控制消息 |
| INTERVAL_CFG_MSG | 0x04 | 间隔配置消息 |
| PING_CTRL_MSG | 0x10 | Ping控制消息 |
| DEVICE_LIST_REQ_MSG | 0x11 | 设备列表请求 |
| CLEAR_DEVICE_LIST_MSG | 0x12 | 清除设备列表 |
| SET_UWB_CHAN_MSG | 0x13 | 设置UWB信道 |

---

### 2.3 消息详细定义

#### 2.3.1 SLAVE_CFG_MSG (0x00) - 从机配置消息

配置所有从机的参数，包括导通检测、阻值检测、卡钉检测等。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Slave Num | uint8 | 1 Byte | 从机数量 |
| **[重复 Slave Num 次]** | | | |
| └─ Slave ID | uint32 | 4 Bytes | 从机唯一ID（小端） |
| └─ Conduction Num | uint8 | 1 Byte | 导通检测通道数量 |
| └─ Resistance Num | uint8 | 1 Byte | 阻值检测通道数量 |
| └─ Clip Mode | uint8 | 1 Byte | 卡钉检测模式 |
| └─ Clip Status | uint16 | 2 Bytes | 卡钉初始化状态位图（小端） |

**每个从机信息占用 9 字节**

**示例：**

```hex
Frame: AB CD 02 00 00 14 00 00 02 37 32 48 5B 02 00 00 00 00 37 32 48 55 02 00 00 00 00

解析：
- Slave Num: 02 (2个从机)
- Slave 0:
  - ID: 0x5B483237 (1531052599)
  - Conduction Num: 2
  - Resistance Num: 0
  - Clip Mode: 0
  - Clip Status: 0x0000
- Slave 1:
  - ID: 0x55483237 (1430933047)
  - Conduction Num: 2
  - Resistance Num: 0
  - Clip Mode: 0
  - Clip Status: 0x0000
```

---

#### 2.3.2 MODE_CFG_MSG (0x01) - 模式配置消息

设置系统运行模式。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Mode | uint8 | 1 Byte | 0: 导通检测模式<br/>1: 阻值检测模式<br/>2: 卡钉检测模式 |

**示例：**

```hex
Frame: AB CD 02 00 00 02 00 01 01

解析：
- Message ID: 0x01 (MODE_CFG_MSG)
- Mode: 0x01 (阻值检测模式)
```

---

#### 2.3.3 SLAVE_RST_MSG (0x02) - 从机复位消息

设置从机锁定状态并复位指定卡钉孔位。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Slave Num | uint8 | 1 Byte | 从机数量 |
| **[重复 Slave Num 次]** | | | |
| └─ Slave ID | uint32 | 4 Bytes | 从机唯一ID（小端） |
| └─ Lock | uint8 | 1 Byte | 0: 解锁<br/>1: 上锁 |
| └─ Clip Status | uint16 | 2 Bytes | 需要复位的卡钉状态位图（小端） |

**每个从机信息占用 7 字节**

**示例：**

```hex
Frame: AB CD 02 00 00 10 00 02 02 37 32 48 5B 01 12 34 37 32 48 55 01 56 78

解析：
- Slave Num: 02
- Slave 0:
  - ID: 0x5B483237
  - Lock: 0x01 (上锁)
  - Clip Status: 0x3412 (位图)
- Slave 1:
  - ID: 0x55483237
  - Lock: 0x01 (上锁)
  - Clip Status: 0x7856 (位图)
```

---

#### 2.3.4 CTRL_MSG (0x03) - 运行控制消息

控制系统的启停状态。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Running Status | uint8 | 1 Byte | 0: 停止<br/>1: 运行 |

**示例：**

```hex
Frame: AB CD 02 00 00 02 00 03 01

解析：
- Message ID: 0x03 (CTRL_MSG)
- Running Status: 0x01 (启动运行)
```

---

#### 2.3.5 INTERVAL_CFG_MSG (0x04) - 间隔配置消息

配置数据采集间隔时间。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Interval Ms | uint8 | 1 Byte | 间隔时间，单位：毫秒 |

**示例：**

```hex
Frame: AB CD 02 00 00 02 00 04 64

解析：
- Message ID: 0x04 (INTERVAL_CFG_MSG)
- Interval Ms: 100 ms
```

---

#### 2.3.6 PING_CTRL_MSG (0x10) - Ping控制消息

控制Ping测试的执行。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Ping Mode | uint8 | 1 Byte | 0: 单次Ping<br/>1: 连续Ping |
| Ping Count | uint16 | 2 Bytes | Ping次数（小端） |
| Interval | uint16 | 2 Bytes | Ping间隔，单位：ms（小端） |
| Destination ID | uint32 | 4 Bytes | 目标设备ID（小端），支持广播 (0xFFFFFFFF) |

**数据长度：9 字节**

**示例：**

```hex
Frame: AB CD 02 00 00 0A 00 10 01 64 00 E8 03 FF FF FF FF

解析：
- Ping Mode: 0x01 (连续Ping)
- Ping Count: 0x0064 (100次)
- Interval: 0x03E8 (1000 ms)
- Destination ID: 0xFFFFFFFF (广播)
```

---

#### 2.3.7 DEVICE_LIST_REQ_MSG (0x11) - 设备列表请求

请求获取当前连接的设备列表。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Reserve | uint8 | 1 Byte | 保留字段，填充 0x00 |

**示例：**

```hex
Frame: AB CD 02 00 00 02 00 11 00

解析：
- Message ID: 0x11 (DEVICE_LIST_REQ_MSG)
- Reserve: 0x00
```

---

#### 2.3.8 CLEAR_DEVICE_LIST_MSG (0x12) - 清除设备列表

清除主控设备管理的设备列表。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Reserve | uint8 | 1 Byte | 保留字段，填充 0x00 |

**示例：**

```hex
Frame: AB CD 02 00 00 02 00 12 00

解析：
- Message ID: 0x12 (CLEAR_DEVICE_LIST_MSG)
- Reserve: 0x00
```

---

#### 2.3.9 SET_UWB_CHAN_MSG (0x13) - 设置UWB信道

设置UWB无线通信信道。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Channel | uint8 | 1 Byte | UWB信道号 (有效范围: 5-10) |

**示例：**

```hex
Frame: AB CD 02 00 00 02 00 13 05

解析：
- Message ID: 0x13 (SET_UWB_CHAN_MSG)
- Channel: 0x05 (信道5)
```

---

## 3. Master2Backend 协议 (主控→后台)

Packet ID: **0x03**

### 3.1 Payload 结构

| 字段 | 类型 | 长度 | 描述 |
|------|------|------|------|
| Message ID | uint8 | 1 Byte | 消息类型标识 |
| Message Data | uint8[] | Variable | 消息内容 |

### 3.2 消息类型列表

| 消息名称 | Message ID | 描述 |
|---------|-----------|------|
| SLAVE_CFG_RSP_MSG | 0x00 | 从机配置响应 |
| MODE_CFG_RSP_MSG | 0x01 | 模式配置响应 |
| RST_RSP_MSG | 0x02 | 复位响应 |
| CTRL_RSP_MSG | 0x03 | 控制响应 |
| PING_RES_MSG | 0x04 | Ping测试结果 |
| DEVICE_LIST_RSP_MSG | 0x05 | 设备列表响应 |
| INTERVAL_CFG_RSP_MSG | 0x06 | 间隔配置响应 |
| SET_UWB_CHAN_RSP_MSG | 0x13 | UWB信道设置响应 |

---

### 3.3 消息详细定义

#### 3.3.1 SLAVE_CFG_RSP_MSG (0x00) - 从机配置响应

响应从机配置命令，返回当前配置状态。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Status | uint8 | 1 Byte | 0: 成功<br/>1: 失败 |
| Slave Num | uint8 | 1 Byte | 从机数量 |
| **[重复 Slave Num 次]** | | | |
| └─ Slave ID | uint32 | 4 Bytes | 从机唯一ID（小端） |
| └─ Conduction Num | uint8 | 1 Byte | 导通检测通道数量 |
| └─ Resistance Num | uint8 | 1 Byte | 阻值检测通道数量 |
| └─ Clip Mode | uint8 | 1 Byte | 卡钉检测模式 |
| └─ Clip Status | uint16 | 2 Bytes | 卡钉初始化状态位图（小端） |

**每个从机信息占用 9 字节**

**示例：**

```hex
Frame: AB CD 03 00 00 15 00 00 00 02 37 32 48 5B 02 00 00 00 00 37 32 48 55 02 00 00 00 00

解析：
- Status: 0x00 (成功)
- Slave Num: 02
- Slave 0:
  - ID: 0x5B483237
  - Conduction Num: 2
  - Resistance Num: 0
  - Clip Mode: 0
  - Clip Status: 0x0000
- Slave 1:
  - ID: 0x55483237
  - Conduction Num: 2
  - Resistance Num: 0
  - Clip Mode: 0
  - Clip Status: 0x0000
```

---

#### 3.3.2 MODE_CFG_RSP_MSG (0x01) - 模式配置响应

响应模式配置命令。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Status | uint8 | 1 Byte | 0: 成功<br/>1: 失败 |
| Mode | uint8 | 1 Byte | 0: 导通检测模式<br/>1: 阻值检测模式<br/>2: 卡钉检测模式 |

**示例：**

```hex
Frame: AB CD 03 00 00 03 00 01 00 01

解析：
- Status: 0x00 (成功)
- Mode: 0x01 (阻值检测模式)
```

---

#### 3.3.3 RST_RSP_MSG (0x02) - 复位响应

响应复位命令。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Status | uint8 | 1 Byte | 0: 成功<br/>1: 失败 |
| Slave Num | uint8 | 1 Byte | 从机数量 |
| **[重复 Slave Num 次]** | | | |
| └─ Slave ID | uint32 | 4 Bytes | 从机唯一ID（小端） |
| └─ Lock | uint8 | 1 Byte | 0: 解锁<br/>1: 上锁 |
| └─ Clip Status | uint16 | 2 Bytes | 复位的卡钉状态位图（小端） |

**每个从机信息占用 7 字节**

**示例：**

```hex
Frame: AB CD 03 00 00 11 00 02 00 02 37 32 48 5B 01 12 34 37 32 48 55 01 56 78

解析：
- Status: 0x00 (成功)
- Slave Num: 02
- Slave 0:
  - ID: 0x5B483237
  - Lock: 0x01 (上锁)
  - Clip Status: 0x3412
- Slave 1:
  - ID: 0x55483237
  - Lock: 0x01 (上锁)
  - Clip Status: 0x7856
```

---

#### 3.3.4 CTRL_RSP_MSG (0x03) - 控制响应

响应控制命令。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Status | uint8 | 1 Byte | 0: 成功<br/>1: 失败 |
| Running Status | uint8 | 1 Byte | 0: 停止<br/>1: 运行 |

**示例：**

```hex
Frame: AB CD 03 00 00 03 00 03 00 01

解析：
- Status: 0x00 (成功)
- Running Status: 0x01 (运行中)
```

---

#### 3.3.5 PING_RES_MSG (0x04) - Ping测试结果

返回Ping测试的统计结果。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Ping Mode | uint8 | 1 Byte | 0: 单次Ping<br/>1: 连续Ping |
| Total Count | uint16 | 2 Bytes | 总发送次数（小端） |
| Success Count | uint16 | 2 Bytes | 成功接收次数（小端） |
| Destination ID | uint32 | 4 Bytes | 目标设备ID（小端） |

**数据长度：9 字节**

**示例：**

```hex
Frame: AB CD 03 00 00 0A 00 04 01 64 00 5A 00 37 32 48 5B

解析：
- Ping Mode: 0x01 (连续Ping)
- Total Count: 0x0064 (100次)
- Success Count: 0x005A (90次)
- Destination ID: 0x5B483237
- 成功率: 90%
```

---

#### 3.3.6 DEVICE_LIST_RSP_MSG (0x05) - 设备列表响应

返回当前管理的设备列表信息。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Device Count | uint8 | 1 Byte | 设备数量 |
| **[重复 Device Count 次]** | | | |
| └─ Device ID | uint32 | 4 Bytes | 设备唯一全局ID（小端） |
| └─ Short ID | uint8 | 1 Byte | 主控分配的短ID |
| └─ Online | uint8 | 1 Byte | 0: 离线<br/>1: 在线 |
| └─ Version Major | uint8 | 1 Byte | 固件主版本号 |
| └─ Version Minor | uint8 | 1 Byte | 固件次版本号 |
| └─ Version Patch | uint16 | 2 Bytes | 固件补丁版本号（小端） |
| └─ Battery Level | uint8 | 1 Byte | 电池电量 (0-100%) |

**每个设备信息占用 11 字节**

**示例：**

```hex
Frame: AB CD 03 00 00 18 00 05 02 37 32 48 5B 01 01 01 02 00 05 64 55 48 32 37 02 01 01 03 00 0A 50

解析：
- Device Count: 0x02 (2个设备)
- Device 0:
  - Device ID: 0x5B483237
  - Short ID: 0x01
  - Online: 0x01 (在线)
  - Version: v1.2.5
  - Battery Level: 100%
- Device 1:
  - Device ID: 0x37324855
  - Short ID: 0x02
  - Online: 0x01 (在线)
  - Version: v1.3.10
  - Battery Level: 80%
```

---

#### 3.3.7 INTERVAL_CFG_RSP_MSG (0x06) - 间隔配置响应

响应间隔配置命令。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Status | uint8 | 1 Byte | 0: 成功<br/>1: 失败 |
| Interval Ms | uint8 | 1 Byte | 间隔时间，单位：毫秒 |

**示例：**

```hex
Frame: AB CD 03 00 00 03 00 06 00 64

解析：
- Status: 0x00 (成功)
- Interval Ms: 100 ms
```

---

#### 3.3.8 SET_UWB_CHAN_RSP_MSG (0x13) - UWB信道设置响应

响应UWB信道设置命令。

**数据格式：**

| 字段名称 | 类型 | 长度 | 描述 |
|---------|------|------|------|
| Status | uint8 | 1 Byte | 0: 成功<br/>1: 失败 |
| Channel | uint8 | 1 Byte | 当前设置的UWB信道号 (5-10) |

**示例：**

```hex
Frame: AB CD 03 00 00 03 00 13 00 05

解析：
- Status: 0x00 (成功)
- Channel: 0x05 (信道5)
```

---

## 4. 状态码定义

所有响应消息中的 `Status` 字段统一定义如下：

| 状态码 | 值 | 含义 |
|--------|-----|------|
| SUCCESS | 0x00 | 成功 |
| FAILURE | 0x01 | 失败 |

---

## 5. 通信流程示例

### 5.1 从机配置流程

```
Backend                          Master
   |                                |
   |--- SLAVE_CFG_MSG (0x00) -----> |
   |    (配置2个从机)                 |
   |                                | (处理配置)
   |                                |
   | <--- SLAVE_CFG_RSP_MSG (0x00)--|
   |      (返回配置状态)              |
   |                                |
```

### 5.2 设备列表查询流程

```
Backend                          Master
   |                                |
   |--- DEVICE_LIST_REQ_MSG (0x11)->|
   |                                | (收集设备信息)
   |                                |
   | <--- DEVICE_LIST_RSP_MSG (0x05)|
   |      (返回设备列表)              |
   |                                |
```

### 5.3 Ping测试流程

```
Backend                          Master                          Slave
   |                                |                                |
   |--- PING_CTRL_MSG (0x10) -----> |                                |
   |    (Ping 100次，目标设备)        |                                |
   |                                |--- PING_REQ (多次) ----------->|
   |                                | <--- PING_RSP ------------------|
   |                                |                                |
   |                                | (统计结果)                       |
   | <--- PING_RES_MSG (0x04) ------|                                |
   |      (100次发送，90次成功)       |                                |
   |                                |                                |
```

---

## 6. 数据字节序说明

### 6.1 多字节数据字节序

所有多字节数据（uint16, uint32）均采用**小端格式 (Little Endian)**：
- 低位字节存储在低地址
- 高位字节存储在高地址

**示例：**

```
uint16 值: 0x1234
存储: [0x34, 0x12]
       低字节 高字节

uint32 值: 0x12345678
存储: [0x78, 0x56, 0x34, 0x12]
       低字节        高字节
```

### 6.2 代码实现参考

在 C/C++ 中读写多字节数据：

```cpp
// 写入 uint16 (小端)
void writeUint16LE(std::vector<uint8_t>& buffer, uint16_t value) {
    buffer.push_back(value & 0xFF);
    buffer.push_back((value >> 8) & 0xFF);
}

// 读取 uint16 (小端)
uint16_t readUint16LE(const std::vector<uint8_t>& data, size_t offset) {
    return data[offset] | (data[offset + 1] << 8);
}

// 写入 uint32 (小端)
void writeUint32LE(std::vector<uint8_t>& buffer, uint32_t value) {
    buffer.push_back(value & 0xFF);
    buffer.push_back((value >> 8) & 0xFF);
    buffer.push_back((value >> 16) & 0xFF);
    buffer.push_back((value >> 24) & 0xFF);
}

// 读取 uint32 (小端)
uint32_t readUint32LE(const std::vector<uint8_t>& data, size_t offset) {
    return data[offset] | 
           (data[offset + 1] << 8) | 
           (data[offset + 2] << 16) | 
           (data[offset + 3] << 24);
}
```

---

## 7. 常见问题 (FAQ)

### Q1: 为什么所有响应消息都包含 Status 字段？

**A:** Status 字段提供了命令执行结果的反馈机制，确保后台能够及时了解命令是否成功执行，从而进行相应的错误处理或重试。

### Q2: Clip Status 位图如何解析？

**A:** Clip Status 是一个 16 位位图，每一位代表一个卡钉孔位的状态：
- Bit 0 → 卡钉孔 0
- Bit 1 → 卡钉孔 1
- ...
- Bit 15 → 卡钉孔 15

例如：`0x0005` (二进制: 0000000000000101) 表示卡钉孔 0 和 2 有效。

### Q3: 如何实现设备的广播通信？

**A:** 在 PING_CTRL_MSG 中，将 Destination ID 设置为 `0xFFFFFFFF` 即可实现广播，所有从机都会响应。

### Q4: UWB信道的有效范围是什么？

**A:** UWB信道的有效范围是 5-10（共6个信道）。超出此范围的设置会被拒绝，响应消息中 Status 字段将返回 FAILURE (0x01)。

### Q5: 帧分片机制如何使用？

**A:** 当数据量过大时，可以将数据分成多个帧发送：
1. 设置 `Fragments Sequence` 为分片序号（从0开始）
2. 设置 `More Fragments Flag` = 1 表示后续还有分片
3. 最后一个分片设置 `More Fragments Flag` = 0

接收端根据序号重组完整数据。

---

## 8. 协议版本历史

| 版本 | 日期 | 主要变更 |
|------|------|---------|
| v1.0 | 2026-01-08 | • 初始版本<br/>• 定义 Backend2Master 9种消息类型<br/>• 定义 Master2Backend 8种消息类型<br/>• 包含设备列表管理和Ping测试功能<br/>• 新增 SET_UWB_CHAN 消息支持信道配置 |

---

## 9. 参考资料

- **WhtsProtocol::Common**: `protocol/Common.h`
- **WhtsProtocol::Frame**: `protocol/Frame.h`, `protocol/Frame.cpp`
- **Backend2Master Messages**: `protocol/messages/Backend2Master.h`, `protocol/messages/Backend2Master.cpp`
- **Master2Backend Messages**: `protocol/messages/Master2Backend.h`, `protocol/messages/Master2Backend.cpp`
- **ByteUtils**: `protocol/utils/ByteUtils.h`, `protocol/utils/ByteUtils.cpp`

---

## 10. 附录

### 10.1 完整消息类型速查表

#### Backend2Master (Packet ID: 0x02)

| ID | 名称 | 数据长度 | 功能 |
|----|------|---------|------|
| 0x00 | SLAVE_CFG_MSG | 1 + N×9 | 配置从机参数 |
| 0x01 | MODE_CFG_MSG | 1 | 设置工作模式 |
| 0x02 | SLAVE_RST_MSG | 1 + N×7 | 复位从机 |
| 0x03 | CTRL_MSG | 1 | 启停控制 |
| 0x04 | INTERVAL_CFG_MSG | 1 | 配置间隔 |
| 0x10 | PING_CTRL_MSG | 9 | Ping测试 |
| 0x11 | DEVICE_LIST_REQ_MSG | 1 | 请求设备列表 |
| 0x12 | CLEAR_DEVICE_LIST_MSG | 1 | 清除设备列表 |
| 0x13 | SET_UWB_CHAN_MSG | 1 | 设置UWB信道 |

#### Master2Backend (Packet ID: 0x03)

| ID | 名称 | 数据长度 | 功能 |
|----|------|---------|------|
| 0x00 | SLAVE_CFG_RSP_MSG | 2 + N×9 | 配置响应 |
| 0x01 | MODE_CFG_RSP_MSG | 2 | 模式响应 |
| 0x02 | RST_RSP_MSG | 2 + N×7 | 复位响应 |
| 0x03 | CTRL_RSP_MSG | 2 | 控制响应 |
| 0x04 | PING_RES_MSG | 9 | Ping结果 |
| 0x05 | DEVICE_LIST_RSP_MSG | 1 + N×11 | 设备列表 |
| 0x06 | INTERVAL_CFG_RSP_MSG | 2 | 间隔响应 |
| 0x13 | SET_UWB_CHAN_RSP_MSG | 2 | UWB信道响应 |

### 10.2 测试工具推荐

- **串口调试工具**: SSCOM, Hercules
- **协议分析工具**: Wireshark (自定义解析器)
- **十六进制编辑器**: HxD, 010 Editor

---

**文档结束**

---

*本文档由代码自动生成，基于 WhtsProtocol 库 v1.0*  
*生成时间: 2026-01-08*  
*维护者: CGJ Team*

