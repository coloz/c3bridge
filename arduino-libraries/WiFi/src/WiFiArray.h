// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <new>

namespace espwifi {
// A small, checked storage owner for cores without STL containers. Operations
// report allocation failure; copying must be explicit so callers can handle it.
template<class T> class Array {
 public:
  Array() = default;
  ~Array() { clear(); }
  Array(const Array&) = delete;
  Array& operator=(const Array&) = delete;
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  T* data() { return data_; }
  const T* data() const { return data_; }
  T* begin() { return data_; }
  const T* begin() const { return data_; }
  T* end() { return size_ ? data_ + size_ : data_; }
  const T* end() const { return size_ ? data_ + size_ : data_; }
  T& operator[](size_t i) { return data_[i]; }
  const T& operator[](size_t i) const { return data_[i]; }
  void clear() {
    destroyStorage(); data_ = nullptr; size_ = capacity_ = 0;
  }
  bool reserve(size_t count) {
    if (count <= capacity_) return true;
    if (count > static_cast<size_t>(-1) / sizeof(T)) return false;
    // Explicit allocation avoids variable new[] cookies and LLVM overflow
    // intrinsics unavailable in arduino-stc51's C++ lowering toolchain.
    T* replacement = static_cast<T*>(::operator new(count * sizeof(T), std::nothrow));
    if (!replacement) return false;
    for (size_t i = 0; i < count; ++i) new (replacement + i) T();
    for (size_t i = 0; i < size_; ++i) replacement[i] = data_[i];
    destroyStorage(); data_ = replacement; capacity_ = count;
    return true;
  }
  bool resize(size_t count) {
    if (!reserve(count)) return false;
    for (size_t i = count; i < size_; ++i) data_[i] = T();
    size_ = count; return true;
  }
  bool push_back(const T& value) {
    // Keep an aliased element alive while reserve replaces the storage.
    T copy = value;
    if (size_ == static_cast<size_t>(-1) || !reserve(size_ + 1)) return false;
    data_[size_++] = copy; return true;
  }
  T* erase(T* at) {
    const size_t index = static_cast<size_t>(at - data_);
    for (size_t i = index; i + 1 < size_; ++i) data_[i] = data_[i + 1];
    data_[--size_] = T();
    return data_ + index;
  }
  bool copyFrom(const Array& other) {
    if (this == &other) return true;
    Array replacement;
    if (!replacement.resize(other.size_)) return false;
    for (size_t i = 0; i < other.size_; ++i) replacement[i] = other.data_[i];
    swap(replacement); return true;
  }
  void swap(Array& other) {
    T* p = data_; data_ = other.data_; other.data_ = p;
    size_t n = size_; size_ = other.size_; other.size_ = n;
    n = capacity_; capacity_ = other.capacity_; other.capacity_ = n;
  }
 private:
  void destroyStorage() {
    for (size_t i = 0; i < capacity_; ++i) data_[i].~T();
    ::operator delete(data_);
  }
  T* data_ = nullptr;
  size_t size_ = 0, capacity_ = 0;
};
}
