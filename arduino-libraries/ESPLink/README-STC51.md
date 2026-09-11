# arduino-stc51 适配

适配依据为 `D:\Git\stc51\arduino-stc51`，平台版本 **0.0.2**。
本次使用的 CLI 安装副本，其 `cores/STC` 全部 100 个文件、`boards.txt`、
`platform.txt` 与该目录逐字节一致；没有修改 STC core 或 ESP32-C3 固件。

## 支持范围

当前目标为 **STC32G144K246，MCS251，C++ core，12 MHz**：

- `ESPLink`：会话、RPC、CRC32C、重试、轮询及自动 UART1 绑定。
- `WiFi`：保持原有 Arduino API，移除 STL 容器依赖，增加显式分配失败处理，
  兼容 16 位 `int`、core 的 `min/max` 宏及严格 override 检查。
- `ESPLinkHCI`：按字节访问 C3 BLE Controller，提供非连接广播便捷方法。
  不依赖主控字节序，不需要包含 `BLE.h`。

**完整 ArduinoBLE GATT / Central / 配对 Host 尚未适配 STC。**
MCS251 在该工具链中是大端；现有 ArduinoBLE 的 HCI/ATT 实现直接覆盖小端
packed struct 和整数指针。`BLEConfig.h` 会阻止该组合编译，防止错误协议报文。
`ESPLinkHCI` 是控制器级接口，不是 ArduinoBLE API 的替代实现。
其 `read()` 可用于接收自行配置的扫描事件或实现自有 Host；本次提供广播示例，
没有提供扫描器、GATT 服务或配对实现。

这不是对全部 STC 型号的支持声明。STC8 / STC15 / STC89 以及
AI8051U 的小程序空间配置不在本次验证范围。当前广播示例约需 69 KB 程序空间，
已经超过 64 KB；ESPLink 本身还需约 8 KB 动态状态及调用栈。
不能通过更改 `architectures` 或把协议包长随意调小来使这些芯片自动兼容。
若要覆盖小容量 STC，需要另行设计小内存传输，以及将 BLE Host 下沉到 C3 的协议。

## 配置和接线

必须在 Arduino 菜单启用 **Experimental C++11 / `cppcore=enabled`**，
并选择 **12 MHz / `clock=12m`**。Plain C 无法使用这些 C++ 库。
保留大容量目标的默认内存模型、堆和栈配置；core 的 RAM 用量包含预留堆，
Arduino CLI 的静态 RAM 数字不代表运行时峰值。

core 仅公开一个 C++ 串口对象 `Serial`，对应 UART1；没有 `Serial1/Serial2`。
ESPLink 在此平台默认 **115200 baud**，采用协作式 `poll()`，不使用 FreeRTOS。

| STC UART1 | ESP32-C3 固件默认引脚 |
| --- | --- |
| P3.1 / TX | GPIO5 / RX |
| P3.0 / RX | GPIO4 / TX |
| GND | GND |

C3 必须使用 **115200** 协议速率的固件；主机调用 `begin()` 不会改变 C3 波特率。
STC 的输出若为 5 V，连接 C3 RX 前需电平转换。
`Serial` 专用于链路，**不要在这个串口调用 `Serial.print()` 输出日志**。
示例将状态保存在全局变量，可通过调试器或自行添加的独立显示设备观察。

core 默认 RX 缓冲只有 16 字节。构建脚本对 C 和 C++ 全部编译单元设置
`SERIAL_RX_BUFFER_SIZE=128`。core 当前上限为 255，不能照搬 STM32 的 2048。
这并不保证 12 MHz 下的 UART 时序：需要实机检查溢出、CRC、超时和重试计数。

## 示例和构建

- `ESPLink/examples/STC51Link`：握手、周期 Ping。
- `WiFi/examples/STC51WiFi`：WiFi 连接与 TCP HTTP 请求。
- `ESPLink/examples/STC51BLEBeacon`：名字为 `STC51` 的非连接广播。
- `WiFi/examples/STC51WiFiBeacon`：先连接 WiFi，再启动广播。

在项目根目录：

```powershell
.\tools\test-ports.ps1 -Target stc -StcConfig D:\Git\stc51\.release-work\install-windows\arduino-cli.yaml
```

或使用你自己的 CLI 配置文件，提供已安装的 `arduino-stc51:mcs51`。
脚本默认 FQBN：
`arduino-stc51:mcs51:stc32g144k246:cppcore=enabled,clock=12m`。
该路径不存在时需先按 STC core 的说明安装；这里不会自动修改你的安装。

## HCI 接口约束

`ESPLinkHCI` 和 `BLE.begin()` 不能同时拥有同一个 C3 Controller。
WiFi 可以共享同一个 ESPLink。所有调用必须串行，不能在中断中调用。

`command()` 等待对应 opcode 的 Command Complete/Status，期间丢弃其他事件，
因此仅用于广播配置等初始化操作。自行扫描或建立连接时，应使用
`writePacket()` / `read()` 实现事件分发；`read()` 返回的是字节流，可能拆包或粘包。
`command()` 的等待周期受 `timeoutMs` 控制，底层同步 RPC 另有最多 2 秒的等待；
它不是可中断的硬实时调用。

UART 会话变化、Controller 丢包、畸形响应、HCI 命令失败会使对象不再健康。
错误后应调用 `end()` / `begin()` 重建状态，不能直接继续发送。
`lastError()` 返回 ESPLink 错误码；HCI 状态映射为 `-256 - status`。

原生测试验证协议字节序、H4 分片、广播命令、迟到响应隔离、错误和重连。
MCU 编译与原生替身测试不代替 STC 实机、UART 持续收发或 BLE 空口验证。
