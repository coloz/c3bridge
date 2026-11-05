#pragma once
#include <Arduino.h>
#include "C3Protocol.h"
#include "HciBridge.h"

class C3Services {
 public:
  bool begin();
  void reset();
  void execute(const c3::Frame& request, c3::Frame& response);
  void pollController() { hci_.poll(); }
  uint32_t hciOutgoingQueued() const { return hci_.outgoingQueued(); }
  uint32_t hciQueuedBytes() { return hci_.queuedBytes(); }
  bool hciRunning() const { return hci_.running(); }
  static constexpr uint8_t MaxSockets = 8;
  static constexpr uint8_t MaxBleConnections = 1;
  static constexpr uint32_t Features = c3::WiFi | c3::TCP | c3::UDP | c3::TLS |
                                     c3::HCI | c3::IPv6 | c3::StationAP;
 private:
  HciBridge hci_;
};