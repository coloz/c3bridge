#include <WiFi.h>
#include <WiFiProv.h>

// CI1306: default ESPLink UART is Serial2, 921600 baud (TX/RX crossed + GND).
// Replace with a different PoP for each device; enter it in Espressif's ESP BLE Prov app.
const char* proofOfPossession = "c3-demo-1234";

void setup() {
  Serial.begin(115200);
  if (!WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SECURITY_1, proofOfPossession)) {
    Serial.print("Provisioning start error: "); Serial.println(WiFiProv.lastError());
  }
}

void loop() {
  ESPLink.poll();
  WiFi.poll();
  static unsigned long last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  WiFiProvStatus status;
  if (!WiFiProv.getStatus(status)) {
    Serial.print("Status error: "); Serial.println(WiFiProv.lastError()); return;
  }
  Serial.print(status.serviceName); Serial.print(" state="); Serial.print(static_cast<int>(status.state));
  Serial.print(" active="); Serial.println(status.active);
  if (status.state == WiFiProvState::Failed && status.active) {
    Serial.print("Credential failure: "); Serial.println(static_cast<int>(status.failure));
    if (!WiFiProv.resetOnFailure()) Serial.println("Retry reset failed");
    // Submit corrected credentials again from the phone.
  }
  if (WiFi.isConnected()) {
    Serial.print("WiFi IP: "); Serial.println(WiFi.localIP());
  }
  // BLE.begin() is available once status.active is false. If already running,
  // call BLE.end() before starting BLE provisioning. Conflicts return c3::Busy.
  // To forget WiFi: WiFiProv.endProvision(); WiFiProv.resetProvisioned();
}
