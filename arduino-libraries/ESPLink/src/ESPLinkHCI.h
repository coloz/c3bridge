// SPDX-License-Identifier: MIT
#pragma once
#include "ESPLink.h"

// Small HCI controller access for STC and other cores without an ArduinoBLE
// Host. Byte-oriented and endian-independent. Exclusively owns the controller:
// never use together with BLE.begin() or another ESPLinkHCI instance.
// No GATT, connections, pairing or automatic command/event handling is implied.
class ESPLinkHCI {
 public:
  explicit ESPLinkHCI(ESPLinkClass& link = ESPLink) : link_(link) {}
  ESPLinkHCI(const ESPLinkHCI&) = delete;
  ESPLinkHCI& operator=(const ESPLinkHCI&) = delete;
  bool begin();
  void end();
  bool healthy();
  int32_t lastError() const { return error_; }
  // Write one complete H4 command/ACL packet. Retries belong to ESPLink only.
  bool writePacket(const uint8_t* packet, size_t size);
  // Read raw H4 stream bytes (possibly part of, or multiple packets). Caller
  // owns packet assembly. 0 means no queued bytes; -1 means error.
  int read(uint8_t* bytes, size_t capacity);
  // Wait for Command Complete/Status for this opcode. Other events are
  // discarded. Use only before scanning/connections, or implement an event
  // dispatcher on top of read/writePacket. Timeout invalidates this wrapper.
  bool command(uint16_t opcode, const uint8_t* parameters = nullptr,
               uint8_t size = 0, uint32_t timeoutMs = 2000);
  // Legacy non-connectable advertisement, at 1 second intervals, public address.
  // data is a sequence of AD structures, up to 31 bytes. GATT is not started.
  bool advertise(const uint8_t* data, uint8_t size);
  bool stopAdvertise();
 private:
  ESPLinkClass& link_;
  uint32_t session_ = 0, lastStatus_ = 0;
  int32_t error_ = c3::NotConnected;
  bool started_ = false;
  uint8_t command_[259] = {};
  uint8_t event_[258] = {};
  size_t eventSize_ = 0;
  bool checkStatus();
  bool fail(int32_t error) { error_ = error; return false; }
};
