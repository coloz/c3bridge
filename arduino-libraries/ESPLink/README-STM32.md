# STM32duino

使用官方 STM32duino（`STMicroelectronics:stm32`）。当前板级编译目标为
**3.0.0 / NUCLEO-F411RE**；2.x 的 `HardwareSerial` 与 3.x 的 `Uart`
串口选择分支均有原生契约测试，但这不表示已经完成 2.x 的 MCU 编译或实机测试。

## 接线与串口

NUCLEO-F411RE 自动选择 USART6，TX=PA11，RX=PA12。

| 主控 | ESP32-C3 固件默认引脚 |
| --- | --- |
| PA11 / TX | GPIO5 / RX |
| PA12 / RX | GPIO4 / TX |
| GND | GND |

使用匹配 **921600 baud** 的 C3 固件。`Serial` / USART2 保留为控制台。
其他板卡根据其 `PeripheralPins` 选择最高编号且有 RX/TX 映射的 UART，
因此不能把 F411RE 的接线直接用于其他板卡。

`STM32Peripheral` 与 `STM32WiFiBLE` 的 `build_opt.h` 设置
`-DSERIAL_RX_BUFFER_SIZE=2048`，使该宏同时作用于 core 和库。
这会增加每个串口实例的 RAM 占用；有限 RAM 板卡需要重新核算资源。

自定义连接时，在启动 WiFi / BLE 之前显式绑定：

```cpp
// Serial1 必须已由 core 实例化（如通过 ENABLE_HWSERIAL1）。
ESPLink.begin(Serial1, 115200); // C3 也必须刷入 115200 固件。
```

也可先自行初始化具有自定义引脚的串口，再调用
`ESPLink.begin(static_cast<Stream&>(port))` 借用该串口。
各版本的 UART 创建和引脚配置见
[STM32duino API](https://github.com/stm32duino/Arduino_Core_STM32/wiki/API)。

## 示例和执行模型

- `BLE/examples/STM32Peripheral`：GATT 计数器、通知、命令特征。
- `WiFi/examples/STM32WiFiBLE`：先连接 WiFi，再启动 GATT 广播。
- `WiFi/examples/PortableWiFi`：TCP 请求及 WiFi 事件。

在同一个应用上下文中调用 API，`loop()` 中持续调用 `WiFi.poll()` 和
`BLE.poll()`。没有 CI1306 的后台 I/O 任务；不能在 ISR 中操作这些对象。
长时间 DNS / TCP / TLS 操作仍会延迟 BLE Host 处理，联合示例先完成网络连接。

在项目根目录运行 `tools/test-ports.ps1 -Target stm32`，保留构建日志和 ELF。
本次验证不包含实际接线、无线收发、吞吐或射频共存测试。
