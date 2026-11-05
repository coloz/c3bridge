// SPDX-License-Identifier: MIT
#pragma once
#include <ESPLink.h>
#include <C3Provisioning.h>

using prov_scheme_t = c3::ProvScheme;
using wifi_prov_security_t = c3::ProvSecurity;
using WiFiProvState = c3::ProvState;
using WiFiProvFailure = c3::ProvFailure;
using WiFiProvStatus = c3::ProvStatus;
static constexpr prov_scheme_t WIFI_PROV_SCHEME_BLE = prov_scheme_t::BLE;
static constexpr prov_scheme_t WIFI_PROV_SCHEME_SOFTAP = prov_scheme_t::SoftAP;
static constexpr wifi_prov_security_t WIFI_PROV_SECURITY_0 = wifi_prov_security_t::None;
static constexpr wifi_prov_security_t WIFI_PROV_SECURITY_1 = wifi_prov_security_t::Security1;

// ESPLink provisioning facade; no ESP-IDF headers are needed on the host MCU.
// This is not a drop-in replacement for ESP32's scheme-handler/UUID overload.
class WiFiProvClass {
 public:
  // Security 1 requires a nonempty, device-specific proof of possession (PoP).
  // Empty serviceName selects PROV_<last 3 MAC bytes>. serviceKey is SoftAP-only.
  // Existing credentials are reused unless resetProvisioned is explicitly true.
  bool beginProvision(prov_scheme_t scheme, wifi_prov_security_t security,
                      const char* pop, const char* serviceName = nullptr,
                      const char* serviceKey = nullptr, bool resetProvisioned = false);
  bool endProvision();
  bool getStatus(WiFiProvStatus& status);
  bool isProvisioned(); // Check lastError() to distinguish false from a link error.
  bool resetProvisioned(); // Stop provisioning first. Erases ESP32 WiFi settings.
  bool resetOnFailure(); // Accept new credentials after an authentication/AP failure.
  int32_t lastError() const { return error_; }
 private:
  bool ensure();
  bool command(c3::Opcode opcode);
  int32_t error_ = c3::Ok;
};
extern WiFiProvClass WiFiProv;
