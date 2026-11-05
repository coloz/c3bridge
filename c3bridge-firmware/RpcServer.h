#pragma once
#include <Arduino.h>
#include "C3Protocol.h"
#include "C3Services.h"
#include "RequestLedger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

class RpcServer {
 public:
  bool begin(HardwareSerial& serial);
  void poll();
  // Diagnostics only. Reading these never touches the protocol UART.
  uint32_t session() const { return session_; }
  bool hciRunning() const { return services_.hciRunning(); }
  uint32_t hciQueuedBytes() { return services_.hciQueuedBytes(); }
  uint32_t hciOutgoingQueued() const { return services_.hciOutgoingQueued(); }
 private:
  static void workerThunk(void* context);
  void worker();
  void onFrame(const c3::Frame& frame);
  void transmit(const c3::Frame& frame);
  void queueReply(const c3::Frame& frame);
  void completeReset();
  c3::Frame helloReply() const;
  HardwareSerial* serial_ = nullptr;
  c3::Decoder decoder_;
  C3Services services_;
  SemaphoreHandle_t lock_ = nullptr;
  QueueHandle_t jobs_ = nullptr;
  QueueHandle_t outgoing_ = nullptr;
  TaskHandle_t worker_ = nullptr;
  uint32_t bootId_ = 0, session_ = 0, hostNonce_ = 0, nextNonce_ = 0;
  uint32_t lastRxMs_ = 0, operationStarted_ = 0;
  bool resetRequested_ = false, executing_ = false;
  RequestLedger ledger_;
  c3::Frame cached_;
};