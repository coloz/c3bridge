#pragma once
#include <Arduino.h>
#include "C3Provisioning.h"
#include "network_provisioning/manager.h"

// Control APIs belong to the RPC worker. Arduino-ESP32 deinitializes the manager
// on NETWORK_PROV_END; our SDK event handler observes DEINIT as a cleanup fence.
class ProvisioningService {
 public:
  int execute(c3::Opcode op, c3::Reader& r, c3::Writer& w, bool hciRunning);
  void poll();
  bool reset();
  bool active() const { return initialized_; }
  bool ownsBLE() const { return initialized_ && config_.scheme == c3::ProvScheme::BLE; }
 private:
  static void event(void* context, esp_event_base_t base, int32_t event, void* data);
  int start(const c3::ProvConfig& config, bool hciRunning);
  int stop();
  int waitForStop();
  int snapshot(c3::ProvStatus& status);
  bool initialized_ = false, ended_ = false, autoReconnect_ = true;
  esp_event_handler_instance_t handler_ = nullptr;
  c3::ProvConfig config_;
  c3::ProvStatus status_;
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
};
