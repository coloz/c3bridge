// SPDX-License-Identifier: MIT
#include "ESPLinkHCI.h"
#include <limits.h>

bool ESPLinkHCI::begin() {
  if (started_ && healthy()) return true;
  // End a failed controller only if it still belongs to this UART session.
  end();
  if (!link_.ready() && !link_.begin()) return fail(link_.lastError());
  session_ = link_.session();
  size_t n = 0;
  const int32_t result = link_.request(c3::Opcode::HciBegin, nullptr, 0, nullptr, n, 5000);
  if (result != c3::Ok) return fail(result);
  started_ = true; error_ = c3::Ok; eventSize_ = 0;
  if (n) return fail(c3::ProtocolError);
  if (!checkStatus()) return false;
  return command(0x0c03); // HCI Reset, wait for controller completion.
}

void ESPLinkHCI::end() {
  if (started_ && link_.ready() && link_.session() == session_) {
    size_t n = 0;
    const int32_t result = link_.request(c3::Opcode::HciEnd, nullptr, 0, nullptr, n, 2000);
    if (result != c3::Ok) error_ = result;
    else if (n) error_ = c3::ProtocolError;
  }
  started_ = false; eventSize_ = 0;
}

bool ESPLinkHCI::checkStatus() {
  uint8_t status[12]; size_t n = sizeof(status);
  const int32_t result = link_.request(c3::Opcode::HciStatus, nullptr, 0, status, n, 2000);
  if (result != c3::Ok) return fail(result);
  if (!link_.ready() || link_.session() != session_) return fail(c3::LinkLost);
  if (n != sizeof(status)) return fail(c3::ProtocolError);
  if (!status[0] || c3::get32(status + 5) || c3::get16(status + 9) < 259 || status[11])
    return fail(c3::IoError);
  lastStatus_ = millis(); return true;
}

bool ESPLinkHCI::healthy() {
  if (!started_ || error_ != c3::Ok) return false;
  link_.poll();
  if (!link_.ready() || link_.session() != session_) return fail(c3::LinkLost);
  if (uint32_t(millis() - lastStatus_) >= 1000) return checkStatus();
  return true;
}

bool ESPLinkHCI::writePacket(const uint8_t* packet, size_t size) {
  if (!healthy()) return false;
  if (!packet || size < 4 || size > c3::MaxPayload ||
      !((packet[0] == 1 && size == size_t(4) + packet[3]) ||
        (packet[0] == 2 && size >= 5 && size == size_t(5) + c3::get16(packet + 3))))
    return fail(c3::InvalidArgument);
  uint8_t reply[2]; size_t n = sizeof(reply);
  const int32_t result = link_.request(c3::Opcode::HciWrite, packet, size, reply, n, 2000);
  if (result != c3::Ok) return fail(result);
  if (!healthy()) return false;
  return (n == sizeof(reply) && c3::get16(reply) == size) || fail(c3::ProtocolError);
}

int ESPLinkHCI::read(uint8_t* bytes, size_t capacity) {
  if (!healthy()) return -1;
  if (!bytes || !capacity || capacity > c3::MaxPayload || capacity > INT_MAX) {
    fail(c3::InvalidArgument); return -1;
  }
  uint8_t request[2]; c3::put16(request, static_cast<uint16_t>(capacity));
  size_t n = capacity;
  const int32_t result = link_.request(c3::Opcode::HciRead, request, sizeof(request), bytes, n, 2000);
  if (result != c3::Ok) { fail(result); return -1; }
  if (!healthy()) return -1;
  if (n > capacity) { fail(c3::ProtocolError); return -1; }
  return static_cast<int>(n);
}

bool ESPLinkHCI::command(uint16_t opcode, const uint8_t* parameters, uint8_t size, uint32_t timeoutMs) {
  if ((size && !parameters) || !timeoutMs) return fail(c3::InvalidArgument);
  command_[0] = 1; c3::put16(command_ + 1, opcode); command_[3] = size;
  if (size) memcpy(command_ + 4, parameters, size);
  if (!writePacket(command_, size_t(size) + 4)) return false;
  const uint32_t start = millis();
  while (uint32_t(millis() - start) < timeoutMs) {
    // Request only the remainder of one H4 event. HciRead is a byte stream,
    // so fetching a whole 258-byte buffer could consume the next event too.
    const size_t target = eventSize_ < 3 ? 3 : size_t(3) + event_[2];
    const int count = read(event_ + eventSize_, target - eventSize_);
    if (count < 0) return false;
    eventSize_ += static_cast<size_t>(count);
    if (eventSize_ >= 1 && event_[0] != 4) return fail(c3::ProtocolError);
    if (eventSize_ >= 3 && eventSize_ == size_t(3) + event_[2]) {
      const size_t n = eventSize_; eventSize_ = 0;
      if (event_[1] == 0x0e) {
        if (n < 7) return fail(c3::ProtocolError);
        if (c3::get16(event_ + 4) == opcode)
          return event_[6] == 0 || fail(-256 - static_cast<int32_t>(event_[6]));
      } else if (event_[1] == 0x0f) {
        if (n != 7) return fail(c3::ProtocolError);
        if (c3::get16(event_ + 5) == opcode)
          return event_[3] == 0 || fail(-256 - static_cast<int32_t>(event_[3]));
      }
    }
    if (!count) delay(1);
  }
  return fail(c3::Timeout); // A late completion cannot satisfy a future command.
}

bool ESPLinkHCI::advertise(const uint8_t* data, uint8_t size) {
  if (size > 31 || (size && !data)) return fail(c3::InvalidArgument);
  if (!stopAdvertise()) return false;
  uint8_t parameters[15] = {};
  c3::put16(parameters, 1600); c3::put16(parameters + 2, 1600);
  parameters[4] = 3; // ADV_NONCONN_IND: no connections or scan response.
  parameters[13] = 7; // All three advertising channels.
  if (!command(0x2006, parameters, sizeof(parameters))) return false;
  uint8_t advertising[32] = {}; advertising[0] = size;
  if (size) memcpy(advertising + 1, data, size);
  if (!command(0x2008, advertising, sizeof(advertising))) return false;
  const uint8_t enabled = 1;
  return command(0x200a, &enabled, 1);
}

bool ESPLinkHCI::stopAdvertise() {
  const uint8_t enabled = 0;
  return command(0x200a, &enabled, 1);
}
