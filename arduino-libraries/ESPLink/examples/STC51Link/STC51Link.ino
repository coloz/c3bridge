// arduino-stc51: select cppcore=enabled and a device with sufficient RAM.
// Serial is the ESP32-C3 link; do not print diagnostics to this UART.
#include <ESPLink.h>

bool linkReady = false;
uint32_t lastPing = 0;
int32_t linkError = 0;

void setup() {
  linkReady = ESPLink.begin(Serial, 115200);
  linkError = ESPLink.lastError();
}

void loop() {
  ESPLink.poll();
  if (uint32_t(millis() - lastPing) >= 1000) {
    lastPing = millis();
    linkReady = ESPLink.ready() && ESPLink.ping();
    linkError = ESPLink.lastError();
  }
}
