// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <new>

// FIFO of complete packets, with a two-byte little-endian length per packet.
// The owner must serialize every operation; this class has no RTOS dependency.
class HciPacketBuffer {
 public:
  HciPacketBuffer() = default;
  ~HciPacketBuffer() { delete[] data_; }
  HciPacketBuffer(const HciPacketBuffer&) = delete;
  HciPacketBuffer& operator=(const HciPacketBuffer&) = delete;

  bool begin(size_t capacity) {
    if (capacity < 3) return false;
    if (data_) return capacity == capacity_;
    uint8_t* data = new (std::nothrow) uint8_t[capacity];
    if (!data) return false;
    data_ = data;
    capacity_ = capacity;
    return true;
  }

  void reset() { head_ = tail_ = used_ = queued_ = 0; }
  size_t capacity() const { return capacity_; }
  size_t usedBytes() const { return used_; }
  size_t queuedBytes() const { return queued_; }

  bool push(const uint8_t* packet, size_t size) {
    if (!data_ || !packet || !size || size > UINT16_MAX) return false;
    const size_t available = capacity_ - used_;
    if (available < 2 || size > available - 2) return false;
    const uint8_t length[2] = {static_cast<uint8_t>(size),
                               static_cast<uint8_t>(size >> 8)};
    copyIn(head_, length, sizeof(length));
    head_ = advance(head_, sizeof(length));
    copyIn(head_, packet, size);
    head_ = advance(head_, size);
    used_ += size + sizeof(length);
    queued_ += size;
    return true;
  }

  bool pop(uint8_t* packet, size_t capacity, uint16_t& size) {
    if (!packet || used_ < 2) return false;
    uint8_t length[2];
    copyOut(tail_, length, sizeof(length));
    const size_t count = size_t(length[0]) | (size_t(length[1]) << 8);
    if (!count || count > capacity || count > used_ - 2) return false;
    const size_t start = advance(tail_, sizeof(length));
    copyOut(start, packet, count);
    tail_ = advance(start, count);
    used_ -= count + sizeof(length);
    queued_ -= count;
    size = static_cast<uint16_t>(count);
    return true;
  }

 private:
  size_t advance(size_t offset, size_t count) const {
    const size_t untilEnd = capacity_ - offset;
    return count < untilEnd ? offset + count : count - untilEnd;
  }
  void copyIn(size_t offset, const uint8_t* source, size_t size) {
    const size_t first = size < capacity_ - offset ? size : capacity_ - offset;
    memcpy(data_ + offset, source, first);
    if (size > first) memcpy(data_, source + first, size - first);
  }
  void copyOut(size_t offset, uint8_t* destination, size_t size) const {
    const size_t first = size < capacity_ - offset ? size : capacity_ - offset;
    memcpy(destination, data_ + offset, first);
    if (size > first) memcpy(destination + first, data_, size - first);
  }
  uint8_t* data_ = nullptr;
  size_t capacity_ = 0, head_ = 0, tail_ = 0, used_ = 0, queued_ = 0;
};
