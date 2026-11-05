#include "RpcServer.h"
#include "BridgeConfig.h"
#include "esp_random.h"

namespace {
uint32_t randomNonzero() { uint32_t value; do { value = esp_random(); } while (!value); return value; }
}

bool RpcServer::begin(HardwareSerial& serial) {
  serial_ = &serial;
  bootId_ = randomNonzero();
  lock_ = xSemaphoreCreateMutex();
  jobs_ = xQueueCreate(1, sizeof(c3::Frame));
  outgoing_ = xQueueCreate(3, sizeof(c3::Frame));
  if (!lock_ || !jobs_ || !outgoing_ || !services_.begin()) return false;
  lastRxMs_ = millis();
  return xTaskCreate(workerThunk, "c3-services", 12288, this, 2, &worker_) == pdPASS;
}

void RpcServer::transmit(const c3::Frame& frame) {
  uint8_t wire[c3::MaxWire];
  const size_t size = c3::encode(frame, wire, sizeof(wire));
  if (size) serial_->write(wire, size);
  if (frame.type == c3::Type::Response && frame.opcode == static_cast<uint16_t>(c3::Opcode::Restart) && frame.status == c3::Ok) {
    serial_->flush(); delay(100); ESP.restart();
  }
}

void RpcServer::queueReply(const c3::Frame& frame) {
  // The latest result stays cached even if the UART TX queue is saturated.
  // A host retry fetches the cached result without repeating the operation.
  xQueueSend(outgoing_, &frame, pdMS_TO_TICKS(20));
}

c3::Frame RpcServer::helloReply() const {
  c3::Frame reply;
  reply.type = c3::Type::HelloReply;
  reply.session = session_;
  reply.id = hostNonce_;
  c3::Writer out(reply.payload, sizeof(reply.payload));
  out.u16(c3::MaxPayload);
  out.u32(C3Services::Features);
  out.u32(bootId_);
  out.u8(C3Services::MaxSockets);
  out.u8(C3Services::MaxBleConnections);
  out.u16(C3_HCI_MAX_PACKET);
  out.string(C3_BRIDGE_VERSION);
  reply.size = out.size();
  return reply;
}

void RpcServer::onFrame(const c3::Frame& frame) {
  c3::Frame reply;
  bool send = false;
  xSemaphoreTake(lock_, portMAX_DELAY);
  if (frame.type == c3::Type::Hello && frame.session == 0 && frame.id &&
      frame.opcode == 0 && frame.size == 0 && frame.status == c3::Ok) {
    lastRxMs_ = millis();
    if (session_ && ledger_.mayReplayHello(frame.id, hostNonce_) && !resetRequested_) {
      reply = helloReply();
      send = true;
    } else {
      nextNonce_ = frame.id;
      resetRequested_ = true;
      xTaskNotifyGive(worker_);
    }
  } else if (frame.session && frame.session == session_ && !resetRequested_) {
    lastRxMs_ = millis();
    reply.type = c3::Type::Response;
    reply.session = session_;
    reply.id = frame.id;
    reply.opcode = frame.opcode;
    if (frame.type == c3::Type::Ping && !frame.size) {
      reply.type = c3::Type::Pong;
      send = true;
    } else if (frame.type == c3::Type::Request && frame.id && frame.status == 0) {
      switch (ledger_.classify(frame)) {
        case RequestLedger::Decision::Conflict: reply.status = c3::ProtocolError; break;
        case RequestLedger::Decision::Expired: reply.status = c3::ResultExpired; break;
        case RequestLedger::Decision::Busy: reply.status = c3::Busy; break;
        case RequestLedger::Decision::Pending: reply.type = c3::Type::Ack; break;
        case RequestLedger::Decision::Replay: reply = cached_; break;
        case RequestLedger::Decision::Fresh:
          ledger_.accept(frame);
          if (xQueueSend(jobs_, &frame, 0) == pdTRUE) {
            reply.type = c3::Type::Ack;
            xTaskNotifyGive(worker_);
          } else {
            reply.status = c3::Busy;
            cached_ = reply;
            ledger_.complete();
          }
          break;
      }
      send = true;
    }
    // Response ACK is intentionally not acknowledged (no ACK loops).
  } else if (frame.type == c3::Type::Request) {
    reply.type = c3::Type::Response;
    reply.session = frame.session;
    reply.id = frame.id;
    reply.opcode = frame.opcode;
    reply.status = c3::LinkLost;
    send = true;
  }
  xSemaphoreGive(lock_);
  if (send) transmit(reply);
}

void RpcServer::completeReset() {
  // Only this worker owns network objects. Never close them from the UART task.
  xSemaphoreTake(lock_, portMAX_DELAY);
  executing_ = true; operationStarted_ = millis();
  xSemaphoreGive(lock_);
  services_.reset();
  xSemaphoreTake(lock_, portMAX_DELAY);
  executing_ = false;
  hostNonce_ = nextNonce_;
  nextNonce_ = 0;
  session_ = hostNonce_ ? randomNonzero() : 0;
  ledger_.reset();
  resetRequested_ = false;
  xQueueReset(jobs_);
  xQueueReset(outgoing_);
  const c3::Frame reply = helloReply();
  xSemaphoreGive(lock_);
  if (reply.session) queueReply(reply);
}

void RpcServer::workerThunk(void* context) { static_cast<RpcServer*>(context)->worker(); }

void RpcServer::worker() {
  for (;;) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    const bool reset = resetRequested_;
    xSemaphoreGive(lock_);
    if (reset) { completeReset(); continue; }
    services_.poll();
    c3::Frame request;
    if (xQueueReceive(jobs_, &request, pdMS_TO_TICKS(10)) != pdTRUE) continue;
    c3::Frame response;
    response.type = c3::Type::Response;
    response.session = request.session;
    response.id = request.id;
    response.opcode = request.opcode;
    xSemaphoreTake(lock_, portMAX_DELAY);
    if (resetRequested_ || request.session != session_) {
      xSemaphoreGive(lock_);
      continue;
    }
    executing_ = true; operationStarted_ = millis();
    xSemaphoreGive(lock_);
    services_.execute(request, response);
    xSemaphoreTake(lock_, portMAX_DELAY);
    executing_ = false;
    const bool abandoned = resetRequested_ || response.session != session_;
    if (!abandoned) {
      cached_ = response;
      ledger_.complete();
    }
    xSemaphoreGive(lock_);
    if (!abandoned) queueReply(response);
  }
}

void RpcServer::poll() {
  // One task owns UART read/write; arbitrary logging never uses this stream.
  c3::Frame frame;
  size_t budget = C3_LINK_RX_BUFFER;
  while (budget-- && serial_->available()) {
    const int byte = serial_->read();
    if (byte >= 0 && decoder_.feed(static_cast<uint8_t>(byte), frame)) onFrame(frame);
  }
  while (xQueueReceive(outgoing_, &frame, 0) == pdTRUE) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    const bool current = frame.session == session_ && !resetRequested_;
    xSemaphoreGive(lock_);
    if (current) transmit(frame);
  }
  services_.pollController();
  xSemaphoreTake(lock_, portMAX_DELAY);
  const bool stalled = executing_ && uint32_t(millis() - operationStarted_) > 120000U;
  if (session_ && !resetRequested_ && uint32_t(millis() - lastRxMs_) > 15000U) {
    nextNonce_ = 0;
    resetRequested_ = true;
    xTaskNotifyGive(worker_);
  }
  xSemaphoreGive(lock_);
  if (stalled) ESP.restart();
}
