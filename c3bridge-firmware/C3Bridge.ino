#include "BridgeConfig.h"
#include "RpcServer.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

HardwareSerial BridgeSerial(1);
RpcServer bridge;

#if C3_BRIDGE_LOG
// The console port is never the protocol UART, so these writes cannot corrupt
// a frame. Enable with C3_BRIDGE_LOG in BridgeConfig.h or a -D build flag.
static void logBegin() {
  Serial.begin(C3_BRIDGE_LOG_BAUD);
  const uint32_t started = millis();
  while (!Serial && uint32_t(millis() - started) < 2000) delay(10);
  Serial.println();
  Serial.print("C3Bridge ");
  Serial.print(C3_BRIDGE_VERSION);
  Serial.print(" core ");
  Serial.println(C3_BRIDGE_CORE_VERSION);
  Serial.print("link uart1 rx=GPIO");
  Serial.print(uart_get_RxPin(1));
  Serial.print(" tx=GPIO");
  Serial.print(uart_get_TxPin(1));
  Serial.print(" baud=");
  Serial.println(BridgeSerial.baudRate());
}

static void logStatus(uint32_t linkRxBytes) {
  Serial.print("alive uptime=");
  Serial.print(millis() / 1000);
  Serial.print("s session=");
  Serial.print(bridge.session());
  Serial.print(" linkRx=");
  Serial.print(linkRxBytes);
  Serial.print(" heap=");
  Serial.print(ESP.getFreeHeap());
  Serial.print(" hci=");
  Serial.print(bridge.hciRunning() ? "on" : "off");
  // A backlog that never drains means the controller stopped accepting
  // packets while HciWrite kept returning success.
  Serial.print(" hciOut=");
  Serial.print(bridge.hciOutgoingQueued());
  Serial.print(" hciInBytes=");
  Serial.println(bridge.hciQueuedBytes());
}
#endif

void setup() {
  // Unconditional: enabling diagnostics must not also unleash IDF logging.
  esp_log_level_set("*", ESP_LOG_NONE);
#if C3_BRIDGE_LOG
  logBegin();
#endif
  BridgeSerial.setRxBufferSize(C3_LINK_RX_BUFFER);
  BridgeSerial.setTxBufferSize(C3_LINK_TX_BUFFER);
  BridgeSerial.begin(C3_LINK_BASE_BAUD, SERIAL_8N1, C3_LINK_RX_PIN, C3_LINK_TX_PIN);
  if (!bridge.begin(BridgeSerial)) {
#if C3_BRIDGE_LOG
    Serial.println("bridge.begin() FAILED, restarting in 1s");
    Serial.flush();
#endif
    // No malformed bytes are emitted onto the protocol UART on startup failure.
    delay(1000);
    ESP.restart();
  }
#if C3_BRIDGE_LOG
  Serial.println("bridge.begin() ok, serving");
#endif
  enableLoopWDT();
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    // Local startup checks passed; connectivity is not required on first boot.
    esp_ota_mark_app_valid_cancel_rollback();
  }
}

void loop() {
#if C3_BRIDGE_LOG
  // Sampled before poll() drains the buffer, so it counts bytes that actually
  // arrived over the wire rather than frames that parsed successfully.
  static uint32_t linkRxBytes = 0, nextReport = 0;
  const int pending = BridgeSerial.available();
  if (pending > 0) linkRxBytes += uint32_t(pending);
#endif
  bridge.poll();
#if C3_BRIDGE_LOG
  if (uint32_t(millis() - nextReport) >= 1000) {
    nextReport = millis();
    logStatus(linkRxBytes);
  }
#endif
  delay(1);
}
