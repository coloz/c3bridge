// Requires a large-RAM/code arduino-stc51 C++ profile; see README-STC51.md.
// This is a non-connectable BLE beacon, not ArduinoBLE GATT.
// Serial/UART1 is dedicated to C3 at 115200 baud. Do not print to it.
#include <ESPLinkHCI.h>

ESPLinkHCI controller;
bool advertising = false;
int32_t controllerError = 0;
const uint8_t advertisement[] = {
  2, 0x01, 0x06, // General discoverable, BR/EDR unsupported.
  6, 0x09, 'S', 'T', 'C', '5', '1' // Complete local name.
};

void setup() {
  advertising = controller.begin() && controller.advertise(advertisement, sizeof(advertisement));
  controllerError = controller.lastError();
}

void loop() {
  ESPLink.poll();
  if (advertising && !controller.healthy()) {
    advertising = false;
    controllerError = controller.lastError();
    controller.end();
  }
}
