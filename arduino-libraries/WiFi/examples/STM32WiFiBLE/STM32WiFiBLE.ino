// STM32duino 2.x/3.x, NUCLEO-F411RE:
// ESPLink UART6 TX=PA11, RX=PA12; console remains Serial/USART2.
// Flash C3 for 921600 baud. For a custom UART, bind ESPLink before WiFi/BLE.
#include <WiFi.h>
#include <BLE.h>

BLEService service("19B10000-E8F2-537E-4F6C-D104768A1214");
BLEUnsignedIntCharacteristic counter("19B10001-E8F2-537E-4F6C-D104768A1214",
                                     BLERead | BLENotify);
bool bleReady = false;
uint32_t lastUpdate = 0;
unsigned int value = 0;

void setup() {
  Serial.begin(115200);
  WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");
  // Complete network connection before starting the BLE Host.
  if (WiFi.waitForConnectResult(15000) != WL_CONNECTED) {
    Serial.println("WiFi connection failed");
    return;
  }
  if (!BLE.begin()) { Serial.println("BLE initialization failed"); return; }
  BLE.setLocalName("STM32-WiFi-BLE");
  BLE.setAdvertisedService(service);
  service.addCharacteristic(counter);
  BLE.addService(service);
  counter.writeValue(value);
  bleReady = BLE.advertise();
}

void loop() {
  WiFi.poll();
  if (bleReady) {
    BLE.poll();
    if (!BLEESPLink.healthy()) { BLE.end(); bleReady = false; return; }
    if (uint32_t(millis() - lastUpdate) >= 1000) {
      lastUpdate = millis();
      counter.writeValue(++value);
    }
  }
}
