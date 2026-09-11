# ESP32-C3 串口 WiFi / BLE 扩展

使用esp32c3为没有wifi、ble的arduino扩展相关功能，使其能达到和arduino UNO R4近似的功能。

- `c3bridge-firmware/`：ESP32-C3 源码。
- `arduino-libraries/ESPLink/`：共用 UART 协议、会话、重试与诊断。
- `arduino-libraries/WiFi/`：Arduino WiFi、TCP、UDP、TLS API。
- `arduino-libraries/BLE/`：运行在主控上的 ArduinoBLE Host。

将所需库目录安装到 Arduino 的 `libraries/`，或在 Arduino CLI 使用
`--libraries D:\Git\ESP32C3Link\arduino-libraries`。

## 主控适配

| 平台 | 实现范围 | 说明 |
| --- | --- | --- |
| ci130x-arduino | WiFi + ArduinoBLE Host | 保留原有 Serial2 / FreeRTOS 优化 |
| STM32duino | WiFi + ArduinoBLE Host | 协作式轮询；NUCLEO-F411RE / core 3.0.0 板级编译验证 |
| arduino-stc51 | ESPLink、WiFi、HCI 控制器接口及非连接 BLE 广播 | STC32G144K246 / C++ core；完整 ArduinoBLE GATT Host 尚未适配 |

详情见 [STM32 接线与构建](arduino-libraries/ESPLink/README-STM32.md) 和
[STC51 支持范围与构建](arduino-libraries/ESPLink/README-STC51.md)。
编译和主机测试不等同于目标板实机无线验证。
具体结果和未验证项见 [适配验证记录](PORTING-VALIDATION.md)。

## 回归验证

Linux / WSL，在本项目根目录：

```sh
sh tools/test-native.sh
```

Windows，已安装 STM32duino：

```powershell
.\tools\test-ports.ps1 -Target stm32
```

STC 使用已有的 arduino-stc51 CLI 配置文件，脚本不会修改或安装 core：

```powershell
.\tools\test-ports.ps1 -Target stc -StcConfig D:\Git\stc51\.release-work\install-windows\arduino-cli.yaml
```

每个构建使用 `-j 1`，日志和目标产物保存在 `build/ports/`；失败会返回错误。
该目录的 JSON 记录每个 FQBN、返回码和日志位置。
