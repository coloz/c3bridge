#include "BridgeConfig.h"
#include "RpcServer.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

HardwareSerial BridgeSerial(1);
RpcServer bridge;

void setup() {
  esp_log_level_set("*", ESP_LOG_NONE);
  BridgeSerial.setRxBufferSize(C3_LINK_RX_BUFFER);
  BridgeSerial.setTxBufferSize(C3_LINK_TX_BUFFER);
  BridgeSerial.begin(C3_LINK_BASE_BAUD, SERIAL_8N1, C3_LINK_RX_PIN, C3_LINK_TX_PIN);
  if (!bridge.begin(BridgeSerial)) {
    // No malformed bytes are emitted onto the protocol UART on startup failure.
    delay(1000);
    ESP.restart();
  }
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
  bridge.poll();
  delay(1);
}