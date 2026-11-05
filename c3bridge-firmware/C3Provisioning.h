// SPDX-License-Identifier: MIT
// Shared, dependency-free provisioning wire contract. Keep both copies identical.
#pragma once
#include "C3Protocol.h"

namespace c3 {
enum class ProvScheme : uint8_t { SoftAP = 0, BLE = 1 };
enum class ProvSecurity : uint8_t { None = 0, Security1 = 1 };
enum class ProvState : uint8_t {
  Idle = 0, Started = 1, CredentialsReceived = 2, Failed = 3,
  Success = 4, Stopped = 5
};
enum class ProvFailure : uint8_t { None = 0, AuthError = 1, APNotFound = 2 };

struct ProvConfig {
  ProvScheme scheme = ProvScheme::BLE;
  ProvSecurity security = ProvSecurity::Security1;
  bool reset = false;
  char pop[65] = {}, serviceName[33] = {}, serviceKey[64] = {};
  bool valid() const {
    if (scheme != ProvScheme::BLE && scheme != ProvScheme::SoftAP) return false;
    if (security != ProvSecurity::None && security != ProvSecurity::Security1) return false;
    if (security == ProvSecurity::Security1 && !pop[0]) return false;
    if (scheme == ProvScheme::BLE && (strlen(serviceName) > 31 || serviceKey[0])) return false;
    return !serviceKey[0] || strlen(serviceKey) >= 8;
  }
};
struct ProvStatus {
  ProvState state = ProvState::Idle;
  ProvScheme scheme = ProvScheme::BLE;
  bool active = false, provisioned = false;
  ProvFailure failure = ProvFailure::None;
  char ssid[33] = {}, serviceName[33] = {};
};

// Reject embedded NULs instead of silently accepting a shortened credential.
inline bool readProvString(Reader& r, char* out, size_t capacity) {
  const size_t n = r.u16();
  if (!r.ok() || n >= capacity || n > r.remaining()) return false;
  if (!r.bytes(out, n) || memchr(out, 0, n)) return false;
  out[n] = 0;
  return true;
}
inline bool readProvConfig(Reader& r, ProvConfig& config) {
  config.scheme = static_cast<ProvScheme>(r.u8());
  config.security = static_cast<ProvSecurity>(r.u8());
  const uint8_t reset = r.u8(); config.reset = reset != 0;
  return reset <= 1 && readProvString(r, config.pop, sizeof(config.pop)) &&
    readProvString(r, config.serviceName, sizeof(config.serviceName)) &&
    readProvString(r, config.serviceKey, sizeof(config.serviceKey)) && r.done() && config.valid();
}
inline bool writeProvConfig(Writer& w, const ProvConfig& config) {
  return w.u8(static_cast<uint8_t>(config.scheme)) && w.u8(static_cast<uint8_t>(config.security)) &&
    w.u8(config.reset) && w.string(config.pop) && w.string(config.serviceName) && w.string(config.serviceKey);
}
inline bool writeProvStatus(Writer& w, const ProvStatus& status) {
  return w.u8(static_cast<uint8_t>(status.state)) && w.u8(static_cast<uint8_t>(status.scheme)) &&
    w.u8(status.active) && w.u8(status.provisioned) && w.u8(static_cast<uint8_t>(status.failure)) &&
    w.string(status.ssid) && w.string(status.serviceName);
}
inline bool readProvStatus(Reader& r, ProvStatus& status) {
  const uint8_t state = r.u8(), scheme = r.u8(), active = r.u8(), provisioned = r.u8(), failure = r.u8();
  if (state > 5 || scheme > 1 || active > 1 || provisioned > 1 || failure > 2) return false;
  status.state = static_cast<ProvState>(state); status.scheme = static_cast<ProvScheme>(scheme);
  status.active = active != 0; status.provisioned = provisioned != 0;
  status.failure = static_cast<ProvFailure>(failure);
  return readProvString(r, status.ssid, sizeof(status.ssid)) &&
    readProvString(r, status.serviceName, sizeof(status.serviceName)) && r.done();
}
} // namespace c3
