// arduino-stc51, cppcore=enabled. Serial/UART1 belongs exclusively to C3.
// Use the C3 firmware built for 115200 baud. No Serial.print on this link.
#include <WiFi.h>

const char ssid[] = "YOUR_SSID";
const char password[] = "YOUR_PASSWORD";
WiFiClient client;
bool networkReady = false;
int32_t networkError = 0;
uint32_t receivedBytes = 0;
bool requestSent = false;

void setup() {
  if (!ESPLink.begin()) { networkError = ESPLink.lastError(); return; }
  WiFi.begin(ssid, password);
  networkError = WiFi.lastError();
}

void loop() {
  WiFi.poll();
  networkReady = WiFi.isConnected();
  if (networkReady && !requestSent) {
    requestSent = true;
    if (client.connect("example.com", 80)) {
      client.print("GET / HTTP/1.0\r\nHost: example.com\r\n\r\n");
    }
    networkError = client.lastError();
  }
  // Keep each loop bounded so link polling is serviced frequently.
  for (uint8_t i = 0; i < 32 && client.available(); ++i) {
    if (client.read() >= 0) ++receivedBytes;
  }
}
