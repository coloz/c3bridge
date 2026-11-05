# ESP32-C3 WiFi 配网

ESP32-C3 固件使用 Espressif 官方 `wifi_prov` 配网协议，支持 Espressif 的
ESP BLE Prov / ESP SoftAP Prov 手机应用及 `esp_prov` 工具。主控无需 ESP-IDF、
BLE 协议栈或 protobuf，使用 ESPLink 串口 API 即可控制配网。

固件基于 Arduino-ESP32 **3.3.11**，使用该版本 core 同样采用的
`network_provisioning` / `network_prov_mgr` 组件（`wifi_provisioning` 的继任组件）。
避免同时链接两个组件产生 protobuf 重复符号；沿用 core 在 END 事件中释放管理器的流程，
等待 DEINIT 事件处理后才允许重新启动配网或 HCI。
主控需同时更新本仓库的 **ESPLink 0.3.0、WiFi 0.3.0**；固件版本为 **0.2.0**。
握手新增 `c3::Provisioning` 能力位，旧固件调用配网 API 返回 `c3::Unsupported`。

## 快速开始

```cpp
#include <WiFi.h>
#include <WiFiProv.h>

void setup() {
  Serial.begin(115200);
  // 每台设备应配置自己的 PoP，在手机应用中输入同一值。
  if (!WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE,
                              WIFI_PROV_SECURITY_1, "device-pop-1234")) {
    Serial.println(WiFiProv.lastError());
  }
}

void loop() {
  WiFi.poll();
  WiFiProvStatus status;
  if (WiFiProv.getStatus(status)) {
    // status.serviceName 默认是 PROV_<MAC末三字节>。
    // status.state / failure 描述本次配网进度；active 表示资源尚未释放。
    // 正常联网结果仍通过 WiFi.status()、WiFi.localIP() 查询。
  }
  delay(250);
}
```

CI1306 默认使用 `Serial2`、921600 baud，与 C3 的 TX/RX 交叉连接并共地。
需要指定其他 UART 时，先调用 `ESPLink.begin(serial, baud)`。

完整示例：

- [WiFiProvBLE](examples/WiFiProvBLE/WiFiProvBLE.ino)：BLE 配网、状态查询、错误凭据后重新提交。
- [WiFiProvSoftAP](examples/WiFiProvSoftAP/WiFiProvSoftAP.ino)：SoftAP 配网，独立设置 PoP 和 AP 密码。

## API

所有控制 API 返回 `bool`，失败时用 `WiFiProv.lastError()` 获取 `c3::Status`。
这些调用使用 ESPLink，与现有 WiFi/BLE API 一样，应在同一应用上下文串行调用。

| API | 行为 |
| --- | --- |
| `beginProvision(scheme, security, pop, serviceName=nullptr, serviceKey=nullptr, resetProvisioned=false)` | 启动配网。已有配置且未要求重置时，连接已有网络，不开启配网服务。返回 true 表示请求成功，不表示已经联网。 |
| `endProvision()` | 等待配网传输停止并清理管理器；保留 WiFi 凭据与连接。可重复调用。 |
| `getStatus(WiFiProvStatus&)` | 获取当前快照。失败时保持输出对象不变。 |
| `isProvisioned()` | 是否存在 WiFi STA 配置中的 SSID；不是连接成功标志。返回 false 时检查 `lastError()`。 |
| `resetProvisioned()` | 清除 C3 的 WiFi 配置，使用官方 `network_prov_mgr_reset_wifi_provisioning()`，会重置 WiFi 设置。配网活动期间返回 Busy，应先 `endProvision()`。 |
| `resetOnFailure()` | 在凭据失败状态清除失败凭据，允许手机重新提交。未开始配网返回 NotConnected，尚未失败返回 Busy。 |
| `lastError()` | 最近一次 API 的 RPC/协议错误码。 |

`WiFiProv.h` 是本仓库的主控 API，**不是 Arduino-ESP32 WiFiProv 的完整签名兼容层**；
不提供其 scheme-handler、UUID、二维码打印、Security 2、定制端点接口。
配网进度通过 `getStatus()` 查询，不通过 `WiFi.onEvent()` 分发；正常 WiFi 连接事件仍可使用。

参数：

- `WIFI_PROV_SCHEME_BLE` / `WIFI_PROV_SCHEME_SOFTAP`。
- `WIFI_PROV_SECURITY_1`：X25519 + PoP + AES-CTR；本 API 要求非空 PoP，最长 64 字节。
- `WIFI_PROV_SECURITY_0`：明文协议，仅在明确需要时使用，可传空 PoP。
- `serviceName`：空值自动生成。BLE 最长 31 字节，SoftAP 最长 32 字节。
- `serviceKey`：SoftAP 密码，空值为开放 AP，非空需 8–63 字节；BLE 必须为空。
- `resetProvisioned=true`：明确清除现有 WiFi 配置后重新配网；正常启动保持默认 false。

## 状态、持久化与 BLE 共存

`WiFiProvStatus` 包含 `state`、`scheme`、`active`、`provisioned`、`failure`、
`ssid[33]`、`serviceName[33]`，不回传 WiFi 密码或 PoP。
状态查询会按需初始化 STA（不自动连接），用于读取 WiFi 配置。

状态依次可能为 `Idle`、`Started`、`CredentialsReceived`、`Failed`、`Success`、`Stopped`。
`failure` 为 `None`、`AuthError` 或 `APNotFound`。`Success` 在自动停止后继续保留；
SDK 会给手机获取结果留出时间，因此 `Success` 时 `active` 可能仍为 true。
失败后先读取原因，再调用 `resetOnFailure()` 并在手机上重新提交。

官方管理器将收到的凭据保存到 C3 的 NVS；失败凭据也可能已经写入，
所以 `provisioned` 只表示有配置，不能替代 `WiFi.isConnected()`。
ESPLink 会话重建会停止配网并复位本次状态，但保留 NVS 凭据；再次调用
`beginProvision(..., false)` 可使用已有配置连接。普通 `WiFi.begin(ssid, password)`
沿用固件原有的 RAM 配置行为。

BLE 配网与 ArduinoBLE HCI 共用同一控制器。先 `BLE.end()` 再开始 BLE 配网；
等到 `status.active == false` 或 `endProvision()` 成功后，才调用 `BLE.begin()`。
冲突操作返回 `c3::Busy`，不会隐式关闭正在使用的 BLE。
清理时不永久释放控制器内存，以便后续重复启动 BLE。
SoftAP 配网不占用 BLE 控制器，可与 HCI 同时使用，但实机共存仍需验证。

配网活动期间，修改 WiFi 模式、连接/断开、STA/AP 配置、启动/删除扫描、
AP 启停及更改连接选项的指令返回 `Busy`。状态查询和网络通信仍可调用。
主控应持续 `WiFi.poll()` / `ESPLink.poll()`，以保持串口会话。

## 串口扩展

协议版本保持 1，新增能力位 `0x200`。所有整数使用小端序，字符串使用
`u16` 字节长度前缀，不携带结尾 NUL。

| Opcode | 请求 | 成功响应 |
| --- | --- | --- |
| `0x0130 WifiProvBegin` | `u8 scheme, u8 security, u8 reset, string pop, string serviceName, string serviceKey` | 空 |
| `0x0131 WifiProvEnd` | 空 | 空 |
| `0x0132 WifiProvStatus` | 空 | `u8 state, u8 scheme, u8 active, u8 provisioned, u8 failure, string ssid, string serviceName` |
| `0x0133 WifiProvReset` | 空 | 空 |
| `0x0134 WifiProvRetry` | 空 | 空 |

Scheme 为 SoftAP=0 / BLE=1；Security 为 0 / 1；State 按上述顺序为 0–5；
Failure 为 None=0 / AuthError=1 / APNotFound=2。
共享格式在两份 `C3Provisioning.h` 中定义，测试检查其与 `C3Protocol.h` 的一致性。

## 编译与验证

从仓库根目录运行（Windows，已安装 Arduino CLI、ESP32 3.3.11 和 ChipIntelli 工具链）：

```powershell
.\tools\test-provisioning.ps1 -Ci130xRoot D:\Git\chipintelli\arduino-ci130x
```

脚本使用该目录的本地 core，通过临时 sketchbook junction 注册为
`c3prov:ci13xx:ci1306:Algorithm=null`，并显式引用本仓库配套库。
构建 BLE/SoftAP 配网示例、WiFiBasics、CI1306Peripheral 和 C3 固件。
固件沿用原来的 4 MB 分区表，每个 OTA 槽为 `0x180000`（1.5 MiB）；
脚本将 `upload.maximum_size` 设为相同大小，并检查最终 BIN 是否能装入分区。
日志、返回码 JSON、ELF/BIN 位于 `.build/provisioning/`。
可用 `-Target firmware` 或 `-Target ci130x` 单独构建。

Linux / WSL：

```sh
sh tools/test-provisioning-native.sh
sh arduino-libraries/WiFi/tests/run.sh
```

原生测试编译实际 API 与固件配网服务，用 SDK/串口替身覆盖协议边界、旧固件、
错误响应、配网失败重试、资源释放和凭据复用，并开启 ASan/UBSan。
编译和替身测试不等于实机手机配网验证；BLE 广播发现、手机 Security 1 握手、
DHCP、断电保存、BLE→配网→BLE 切换仍需两块板和手机联调。

### 本次验证结果（2026-09-17）

CI130x 使用 `D:\Git\chipintelli\arduino-ci130x` 工作目录的 1.0.17 core
（Git `8411657`），配套库来自当前 c3bridge 仓库。该 core 工作目录未产生修改。

| 构建目标 | 结果 | Sketch 字节 | 静态 RAM 字节 |
| --- | --- | ---: | ---: |
| ESP32-C3 固件，Arduino-ESP32 3.3.11，4 MB Flash | 通过 | 1,562,481 | 69,812 |
| CI1306 / Algorithm=null / WiFiProvBLE | 通过 | 83,323 | 111,492 |
| CI1306 / Algorithm=null / WiFiProvSoftAP | 通过 | 83,323 | 111,492 |
| CI1306 / Algorithm=null / WiFiBasics | 通过 | 87,573 | 111,508 |
| CI1306 / Algorithm=null / CI1306Peripheral | 通过 | 110,566 | 113,240 |

实际 C3 应用 BIN 为 **1,562,624 字节**；已解码生成的分区表核对两槽容量，
每槽剩余 **10,240 字节（10 KiB）**。4 MB 分区布局保持不变，后续增加功能需留意容量。
BIN / ELF / 合并固件位于 `.build/provisioning/C3Bridge-build/`；
本次 CI1306 产物位于 `.build/provisioning/ci130x/`，综合结果及源码 SHA-256
位于 `.build/provisioning/validation.json`。

新增配网 API / 固件服务原生测试，以及现有 WiFi 的三种 IPAddress 配置、
存储与内存分配回归测试均通过。没有进行烧录或手机实机配网。

官方参考：[Network Provisioning 组件](https://github.com/espressif/idf-extra-components/tree/master/network_provisioning)、
[Arduino-ESP32 配网集成](https://github.com/espressif/arduino-esp32/blob/3.3.11/libraries/WiFi/src/WiFiGeneric.cpp)。
