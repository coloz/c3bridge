#include <WiFi.h>
#include <WiFiProv.h>

void setup() {
  Serial.begin(115200);
  // Use Espressif's ESP SoftAP Prov app. PoP and AP password are separate.
  // Choose unique values for each device. Existing WiFi settings are reused.
  if (!WiFiProv.beginProvision(WIFI_PROV_SCHEME_SOFTAP, WIFI_PROV_SECURITY_1,
      "c3-demo-1234", nullptr, "c3-ap-1234")) {
    Serial.print("Provisioning start error: "); Serial.println(WiFiProv.lastError());
  }
}

void loop() {
  ESPLink.poll(); WiFi.poll();
  static unsigned long last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  WiFiProvStatus status;
  if (!WiFiProv.getStatus(status)) {
    Serial.print("Status error: "); Serial.println(WiFiProv.lastError()); return;
  }
  Serial.print(status.serviceName); Serial.print(" state="); Serial.println(static_cast<int>(status.state));
  if (status.state == WiFiProvState::Failed && status.active) {
    Serial.print("Credential failure: "); Serial.println(static_cast<int>(status.failure));
    if (!WiFiProv.resetOnFailure()) Serial.println("Retry reset failed");
  }
  if (WiFi.isConnected()) { Serial.print("WiFi IP: "); Serial.println(WiFi.localIP()); }
}
