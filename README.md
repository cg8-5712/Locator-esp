# ESP32-S3 4G GPS Locator

## 项目概述

本项目使用 `ESP32-S3` 作为主控，外挂一个 `GPS` 模块和一个 `4G` 模块，实现低带宽定位器。

核心目标如下：

- 从 GPS 模块串口连续读取 NMEA 语句。
- 每秒从多条 NMEA 数据中筛选 `$GNGLL`。
- 对原始 `$GNGLL` 语句执行标准 NMEA 校验。
- 将有效数据整理为紧凑上报格式：
  `3956.20359N,11622.44467E,090353AA*4C`
- 通过 4G 模块的 AT 指令 MQTT 能力发布到服务器。
- 支持远程查看部分 4G 模块状态，例如 `ICCID`、`SIM 卡槽`、`注册状态`、`信号质量`、`MQTT 连接状态`。

项目当前是一个 `PlatformIO + Arduino` 工程，目标板来自现有配置：

- `platform = espressif32`
- `board = esp32-s3-devkitc-1`
- `framework = arduino`

## 硬件组成

- MCU：ESP32-S3
- GNSS：输出 NMEA 的 GPS 模块
- Cellular：支持 AT 指令与 MQTT 的 4G 模块
- 通信方式：
  - ESP32-S3 <-> GPS：UART
  - ESP32-S3 <-> 4G 模块：UART

建议硬件控制信号：

- 4G 模块 `PWRKEY` 或开机控制脚
- 4G 模块 `RESET` 脚
- 可选网络状态脚
- 可选 GPS `PPS` 脚

## 数据流

1. GPS 模块每秒输出 NMEA 数据。
2. ESP32-S3 按行读取完整语句，优先筛选 `$GNGLL`。
3. 先对原始 `$GNGLL` 做 NMEA XOR 校验。
4. 校验通过后提取字段并整理为业务上报字符串。
5. ESP32-S3 通过 4G 模块 AT 指令检查联网状态和 MQTT 状态。
6. 通过 `AT+QMTPUB` 将定位结果发布到服务器。
7. 周期性或按远程指令上报 4G 模块状态信息。

## GPS 样例与目标格式

原始 `$GNGLL` 样例：

```text
$GNGLL,3956.20359,N,11622.44467,E,090353.000,A,A*4C
```

目标上报格式：

```text
3956.20359N,11622.44467E,090353AA*4C
```

整理规则：

- 保留纬度数值 `3956.20359`，并把方向位 `N` 拼接到其后。
- 保留经度数值 `11622.44467`，并把方向位 `E` 拼接到其后。
- UTC 时间 `090353.000` 截断为 `090353`。
- 保留定位状态 `A`。
- 保留模式字段 `A`。
- 保留原始 NMEA 语句中的校验码 `*4C`。

注意：

- `*4C` 对应的是原始 `$GNGLL` 语句的 NMEA 校验结果。
- 业务字符串删除了部分逗号和字符后，已经不再是标准 NMEA 语句，因此不能再用原始 NMEA 规则对整理后的整串重新校验。
- 正确做法是：先校验原始语句，再进行格式整理。

## 4G 模块侧能力

本项目优先使用模块内置 MQTT 指令，而不是自行通过 `QIOPEN` 建 TCP 再手工封装 MQTT。

已确认需要使用的 AT 能力：

- `AT+QCCID`：查询 SIM ICCID
- `AT+SINGLESIM?` / `AT+SINGLESIM=<id>`：查询或切换 SIM 卡槽
- `AT+CREG?`：查询网络注册状态
- `AT+CSQ`：查询信号质量
- `AT+QMTCFG`：配置 MQTT 客户端
- `AT+QMTCONNCFG`：配置 MQTT 服务器
- `AT+QMTSTART`：设置会话与心跳
- `AT+QMTSUB`：订阅主题
- `AT+QMTPUB` / `AT+QMTPUBEX`：发布消息
- `AT+QMTSTATU`：查询 MQTT 状态
- `AT+QMTDISC`：断开 MQTT

`QIOPEN/QISEND/QICLOSE` 可作为后续原始 TCP/UDP 方案备用，不作为主链路。

## 推荐 MQTT 主题

建议至少保留三个主题：

- 上报定位：`locator/<device_id>/location`
- 上报状态：`locator/<device_id>/status`
- 下行指令：`locator/<device_id>/cmd`

建议：

- 定位主题发送紧凑字符串，降低流量。
- 状态主题发送 JSON，方便排查与远程运维。

## 远程可查看信息

建议远程提供以下信息：

- `iccid`
- `sim_slot`
- `creg_stat`
- `csq_rssi`
- `csq_ber`
- `mqtt_status`
- 最近一次有效定位时间
- 最近一次定位字符串

## 建议的软件结构

后续源码建议拆分为以下模块：

- `src/main.cpp`：启动入口
- `src/gps_parser.*`：NMEA 采集、筛选、校验、转换
- `src/modem_at.*`：AT 指令发送与响应解析
- `src/mqtt_client.*`：MQTT 配置、连接、发布、订阅
- `src/app_controller.*`：状态机与任务调度
- `include/config.h`：串口、引脚、APN、MQTT 参数

## 开发里程碑

1. 打通 GPS 串口读取与 `$GNGLL` 校验。
2. 打通 4G 模块联网与 MQTT 连接。
3. 周期上报定位字符串。
4. 增加状态主题与远程查询能力。
5. 增加异常恢复与重连策略。

## 相关文档

- 协作约束见 `codex.md`
- 详细实现说明见 `docs/technical-guide.md`
