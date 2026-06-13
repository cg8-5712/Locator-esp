# Technical Guide

## 1. 目标

本文档用于指导 `ESP32-S3 + GPS + 4G` 定位器的第一版实现，覆盖数据格式、NMEA 校验、AT 指令流程、MQTT 上报、远程状态查看和错误恢复策略。

## 2. 系统架构

建议架构如下：

```text
GPS UART ---> GpsLineReader ---> GngllParser ---> LocationFormatter ---+
                                                                        |
                                                                        v
                                                           AppController/StateMachine
                                                                        |
                                                                        v
4G UART <--> ModemAtClient <--> MqttService <---------------------------+
```

模块职责：

- `GpsLineReader`：按行接收 NMEA 语句
- `GngllParser`：筛选 `$GNGLL` 并做字段解析
- `LocationFormatter`：生成业务上报字符串
- `ModemAtClient`：发送 AT 指令并解析响应
- `MqttService`：封装 MQTT 配置、连接、发布、订阅
- `AppController`：调度状态机、超时、重试和心跳

## 3. NMEA 数据处理

### 3.1 输入样例

GPS 每秒输出多条 NMEA：

```text
$GNGGA,090353.000,3956.20359,N,11622.44467,E,1,05,4.9,27.1,M,-9.9,M,,*68
$GNGLL,3956.20359,N,11622.44467,E,090353.000,A,A*4C
$GNGSA,A,3,15,18,24,,,,,,,,,,18.5,4.9,17.9,1*34
$GNRMC,090353.000,A,3956.20359,N,11622.44467,E,0.95,293.41,130626,,,A,V*00
```

本项目第一版只处理：

```text
$GNGLL,3956.20359,N,11622.44467,E,090353.000,A,A*4C
```

### 3.2 GNGLL 字段解释

字段拆分如下：

```text
$GNGLL,3956.20359,N,11622.44467,E,090353.000,A,A*4C
        |          | |           | |         | | |
        |          | |           | |         | | +-- 模式
        |          | |           | |         | +---- 状态
        |          | |           | |         +------ UTC 时间
        |          | |           | +---------------- 经度方向
        |          | |           +------------------ 经度
        |          | +------------------------------ 纬度方向
        |          +-------------------------------- 纬度
        +------------------------------------------- 语句类型
```

业务使用字段：

- 纬度：`3956.20359`
- 纬度方向：`N`
- 经度：`11622.44467`
- 经度方向：`E`
- UTC 时间：`090353.000`
- 状态：`A`
- 模式：`A`
- 校验码：`4C`

### 3.3 NMEA 校验规则

校验方法是对 `$` 与 `*` 之间的全部字符逐字节 XOR。

示例校验体：

```text
GNGLL,3956.20359,N,11622.44467,E,090353.000,A,A
```

其校验结果应为：

```text
4C
```

参考实现：

```cpp
uint8_t calcNmeaChecksum(const char* body) {
  uint8_t sum = 0;
  while (*body) {
    sum ^= static_cast<uint8_t>(*body++);
  }
  return sum;
}
```

处理要求：

- 先找到 `$` 和 `*`
- 提取 `*` 前的 body
- 解析 `*` 后两位十六进制校验值
- 比较计算值与报文值

### 3.4 业务字符串转换

原始：

```text
$GNGLL,3956.20359,N,11622.44467,E,090353.000,A,A*4C
```

目标：

```text
3956.20359N,11622.44467E,090353AA*4C
```

转换步骤：

1. `lat = 3956.20359`
2. `ns = N`
3. `lon = 11622.44467`
4. `ew = E`
5. `utc = 090353.000 -> 090353`
6. `status = A`
7. `mode = A`
8. `checksum = 4C`

拼接结果：

```text
lat + ns + "," + lon + ew + "," + hhmmss + status + mode + "*" + checksum
```

即：

```text
3956.20359N,11622.44467E,090353AA*4C
```

注意：

- 这里只是业务压缩格式，不是标准 NMEA。
- `*4C` 是原始 GNGLL 的校验码，不能理解为压缩后字符串的校验码。

### 3.5 建议的解析条件

满足以下条件才算“可上报定位”：

- 语句类型是 `$GNGLL`
- NMEA 校验通过
- 字段数量完整
- 状态字段为 `A`
- 纬度、经度、时间字段非空

否则：

- 丢弃本条定位
- 保留计数和日志
- 不影响主循环继续运行

## 4. 4G 模块流程

### 4.1 开机后最小检查流程

建议顺序：

1. 模块上电
2. 等待 `RDY`
3. 查询 `AT+CREG?`
4. 查询 `AT+CSQ`
5. 查询 `AT+QCCID`
6. 查询 `AT+SINGLESIM?`
7. 配置 MQTT
8. 建立 MQTT 连接

### 4.2 需要保存的调制解调器状态

建议在 RAM 中维护以下结构：

```json
{
  "iccid": "898604E6192391620488",
  "sim_slot": 0,
  "creg_n": 0,
  "creg_stat": 1,
  "csq_rssi": 15,
  "csq_ber": 99,
  "mqtt_status": 1
}
```

### 4.3 网络状态 AT 指令

#### 查询 ICCID

```text
AT+QCCID
+QICCID:898604E6192391620488
OK
```

处理建议：

- 如果只有 `+QICCID:` 但无内容，按“未识别到 SIM”处理
- 把 ICCID 作为设备身份或诊断信息缓存

#### 查询/设置 SIM 卡槽

查询：

```text
AT+SINGLESIM?
+SINGLESIM:0
OK
```

设置：

```text
AT+SINGLESIM=0
OK
RDY
SIM_SUCCESS
NETWORK_ACTIVATE_SUCCESS
```

处理建议：

- 只在网络已初始化后切换
- 切换后认为模块重启，必须重新走初始化流程
- 远程下发 `switch_sim` 命令时要增加权限或防误触保护

#### 查询注册状态

```text
AT+CREG?
+CREG:0,1
OK
```

重点：

- `stat=1` 或 `stat=5` 视为可正常接入网络
- 其他状态应继续等待或重试

#### 查询信号质量

```text
AT+CSQ
+CSQ:15,99
OK
```

解释：

- `rssi=15` 表示信号中等
- `ber=99` 表示误码率未知或未检测

## 5. MQTT 方案

### 5.1 为什么优先用模块内置 MQTT

你给出的模块已经直接提供：

- `AT+QMTCFG`
- `AT+QMTCONNCFG`
- `AT+QMTSTART`
- `AT+QMTSUB`
- `AT+QMTPUB`
- `AT+QMTSTATU`
- `AT+QMTDISC`

因此第一版建议直接使用模块 MQTT 能力，原因如下：

- ESP32 侧实现更简单
- 避免自行维护 MQTT 报文编码
- 降低调试复杂度
- 便于后续远程命令扩展

`AT+QIOPEN` 适合做原始 TCP/UDP 备用通道，不应作为第一版主链路。

### 5.2 建议连接流程

#### 配置客户端

```text
AT+QMTCFG="locator-001","MQTT1","123456",0,0,,
OK
```

#### 配置服务器

```text
AT+QMTCONNCFG="broker.emqx.io",1883,1
OK
MQTTCONNECT
```

#### 配置会话与心跳

```text
AT+QMTSTART=1,30
OK
```

#### 查询 MQTT 状态

```text
AT+QMTSTATU
+QMTSTATU:1
OK
```

状态含义：

- `0`：未建立连接
- `1`：已建立连接
- `2`：连接中

### 5.3 发布定位

建议主题：

```text
locator/<device_id>/location
```

建议命令：

```text
AT+QMTPUB="locator/locator-001/location",0,0,"3956.20359N,11622.44467E,090353AA*4C"
OK
```

### 5.4 发布状态

建议主题：

```text
locator/<device_id>/status
```

建议消息体使用 JSON，例如：

```json
{
  "iccid": "898604E6192391620488",
  "sim_slot": 0,
  "creg": 1,
  "csq": {
    "rssi": 15,
    "ber": 99
  },
  "mqtt": 1,
  "last_fix": "3956.20359N,11622.44467E,090353AA*4C"
}
```

如果消息长度过长，改用：

```text
AT+QMTPUBEX=<topic>,<qos>,<retain>,<msgLen>
```

### 5.5 订阅远程命令

建议命令主题：

```text
locator/<device_id>/cmd
```

建议命令示例：

```json
{"cmd":"query_status"}
{"cmd":"query_iccid"}
{"cmd":"query_signal"}
{"cmd":"query_network"}
{"cmd":"switch_sim","slot":1}
{"cmd":"reconnect_mqtt"}
```

说明：

- 文档片段只给出了订阅配置方法，没有给出下行消息的 URC 格式
- 真正实现远程命令前，必须从完整 AT 手册确认订阅消息到达时的串口回显格式

## 6. 是否需要 `QIOPEN`

第一版不需要。

`QIOPEN`、`QISEND`、`QICLOSE` 适合以下场景：

- 后端不使用 MQTT，只提供 TCP/UDP
- 需要私有二进制协议
- 模块 MQTT 功能不稳定，需要临时切换传输层

如果后续采用 `QIOPEN`：

- 推荐继续复用 `ModemAtClient`
- 增加单独的 `RawSocketService`
- 不要把 MQTT 与 TCP 逻辑混在同一个状态机分支里

## 7. 建议状态机

### 7.1 状态定义

- `BOOT`
- `MODEM_INIT`
- `NET_WAIT`
- `MQTT_INIT`
- `MQTT_WAIT`
- `GPS_WAIT`
- `RUNNING`
- `RECOVERY`

### 7.2 状态流转

```text
BOOT
  -> MODEM_INIT
  -> NET_WAIT
  -> MQTT_INIT
  -> MQTT_WAIT
  -> GPS_WAIT
  -> RUNNING

异常时：
RUNNING -> RECOVERY -> MODEM_INIT
```

### 7.3 建议超时

- 单条 AT 指令超时：`1s ~ 3s`
- 联网等待：`30s ~ 60s`
- MQTT 建连等待：`10s ~ 30s`
- GPS 有效定位等待：`10s ~ 60s`

### 7.4 恢复策略

- `CREG` 长时间非 `1/5`：重试注册或重启模块
- `QMTSTATU=0`：重新配置或重新连接 MQTT
- GPS 长时间无有效 `$GNGLL`：保留状态上报，但停止位置上报
- SIM 切换后：视为冷启动，重新初始化整个 4G 流程

## 8. 代码实现建议

### 8.1 任务拆分

建议拆为以下源文件：

- `main.cpp`
- `gps_parser.h/.cpp`
- `modem_at.h/.cpp`
- `mqtt_service.h/.cpp`
- `app_controller.h/.cpp`
- `config.h`

### 8.2 循环模型

在 `loop()` 中做三件事：

1. 轮询 GPS 串口并组包
2. 轮询 Modem 串口并解析响应/URC
3. 驱动状态机和定时发布

不要：

- 在一个函数中长时间 `delay`
- 发送 AT 后阻塞等待整段大响应
- 在 GPS 收到一行后直接写串口发送 MQTT

### 8.3 推荐数据结构

```cpp
struct GngllData {
  String lat;
  char ns;
  String lon;
  char ew;
  String utc_hhmmss;
  char status;
  char mode;
  uint8_t checksum;
};

struct ModemStatus {
  String iccid;
  int simSlot;
  int cregN;
  int cregStat;
  int csqRssi;
  int csqBer;
  int mqttStatus;
};
```

## 9. 测试建议

### 9.1 GPS 解析测试

至少覆盖：

- 正常 `$GNGLL`
- 错误校验码
- 字段缺失
- 状态非 `A`
- 时间字段为空
- 混入其他 NMEA 语句

### 9.2 Modem AT 测试

至少覆盖：

- `OK` 正常返回
- `ERROR` 返回
- 超时
- 空 SIM
- 弱网 `CREG=0,0`
- MQTT 未连接 `QMTSTATU=0`

### 9.3 联调测试

至少覆盖：

- 上电自动联网
- MQTT 自动发布定位
- 周期状态上报
- 模拟断网恢复
- 模拟 MQTT 断连恢复
- 远程查询模块状态
- 切换 SIM 后重连

## 10. 第一版完成标准

满足以下条件即可认为第一版完成：

- ESP32-S3 能持续读取 GPS NMEA
- 正确筛选并校验 `$GNGLL`
- 正确生成 `3956.20359N,11622.44467E,090353AA*4C`
- 能通过 MQTT 上报定位数据
- 能查询并上报 `ICCID`、`SIM 卡槽`、`CREG`、`CSQ`、`MQTT 状态`
- 出现异常时可自动恢复，不需要人工重启
