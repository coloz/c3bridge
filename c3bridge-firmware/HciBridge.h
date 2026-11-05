#pragma once
#include <Arduino.h>
#include "BridgeConfig.h"
#include "HciPacketBuffer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
struct HciPacket { uint16_t size; uint8_t bytes[C3_HCI_MAX_PACKET]; };
class HciBridge {
 public:
  bool begin();
  void end();
  bool enqueue(const uint8_t* data,size_t size);
  bool receive(HciPacket& packet);
  void poll();
  bool running() const { return active_; }
  bool failed() const { return fault_; }
  uint32_t overflowCount() const { return overflows_; }
  uint32_t invalidCount() const { return invalid_; }
  uint32_t queuedBytes();
  // Controller-bound backlog. A non-draining value means the controller has
  // stopped accepting packets even though HciWrite kept reporting success.
  uint32_t outgoingQueued() const {
    return outgoing_ ? uint32_t(uxQueueMessagesWaiting(outgoing_)) : 0;
  }
 private:
  static void sendReady();
  static int receivePacket(uint8_t* data,uint16_t size);
  static HciBridge* instance_;
  static_assert(C3_HCI_RX_DEPTH > 0 && C3_HCI_MAX_PACKET > 0 &&
                C3_HCI_MAX_PACKET <= UINT16_MAX, "Invalid HCI receive capacity");
  // Preserve the former slot queue's byte budget, but charge actual packet sizes.
  static constexpr size_t RxStorageBytes =
      size_t(C3_HCI_RX_DEPTH) * (size_t(C3_HCI_MAX_PACKET) + sizeof(uint16_t));
  HciPacketBuffer incoming_;
  QueueHandle_t outgoing_=nullptr;
  SemaphoreHandle_t controllerLock_=nullptr;
  portMUX_TYPE statsLock_=portMUX_INITIALIZER_UNLOCKED;
  volatile bool active_=false,fault_=false;
  volatile uint32_t overflows_=0,invalid_=0;
};
