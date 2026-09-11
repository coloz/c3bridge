// STC32G144K246 with arduino-stc51 cppcore=enabled, clock=12m.
// Serial/UART1 TX=P3.1, RX=P3.0. C3 firmware must use 115200 baud.
// BLE is a non-connectable beacon; this sketch does not provide GATT.
#include <WiFi.h>
#include <ESPLinkHCI.h>

ESPLinkHCI controller;
bool wifiReady = false, beaconReady = false;
int32_t error = 0;
const uint8_t advertisement[] = {2, 1, 6, 6, 9, 'S', 'T', 'C', '5', '1'};

void setup() {
  if (!ESPLink.begin()) { error = ESPLink.lastError(); return; }
  WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");
  wifiReady = WiFi.waitForConnectResult(15000) == WL_CONNECTED;
  if (!wifiReady) { error = WiFi.lastError(); return; }
  beaconReady = controller.begin() && controller.advertise(advertisement, sizeof(advertisement));
  error = controller.lastError();
}

void loop() {
  WiFi.poll();
  wifiReady = WiFi.isConnected();
  if (beaconReady && !controller.healthy()) {
    beaconReady = false;
    error = controller.lastError();
    controller.end();
  }
}
