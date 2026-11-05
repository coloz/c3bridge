#include <WiFiProv.h>
#include <cassert>
#include <iostream>
#include <vector>
#include <string>

ESPLinkClass ESPLink;
static size_t calls = 0;
static c3::Opcode lastOpcode;
static std::vector<uint8_t> request, reply;
static int32_t remoteError = c3::Ok;
int32_t ESPLinkClass::request(uint16_t opcode, const uint8_t* data, size_t size,
  uint8_t* response, size_t& responseSize, uint32_t timeout) {
  ++calls; lastOpcode = static_cast<c3::Opcode>(opcode);
  assert(timeout >= 5000);
  ::request.clear(); if (size) ::request.assign(data, data + size);
  if (remoteError != c3::Ok) { responseSize = 0; return remoteError; }
  assert(reply.size() <= responseSize);
  if (!reply.empty()) memcpy(response, reply.data(), reply.size());
  responseSize = reply.size(); return c3::Ok;
}
static void invalid(prov_scheme_t scheme, wifi_prov_security_t security,
                    const char* pop, const char* name = nullptr, const char* key = nullptr) {
  const size_t before = calls;
  assert(!WiFiProv.beginProvision(scheme, security, pop, name, key));
  assert(WiFiProv.lastError() == c3::InvalidArgument && calls == before);
}
int main() {
  invalid(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SECURITY_1, nullptr);
  invalid(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SECURITY_1, "");
  invalid(static_cast<prov_scheme_t>(255), WIFI_PROV_SECURITY_1, "secret");
  invalid(WIFI_PROV_SCHEME_BLE, static_cast<wifi_prov_security_t>(2), "secret");
  invalid(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SECURITY_1, std::string(65, 'p').c_str());
  invalid(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SECURITY_1, "secret", std::string(32, 'n').c_str());
  invalid(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SECURITY_1, "secret", nullptr, "password");
  invalid(WIFI_PROV_SCHEME_SOFTAP, WIFI_PROV_SECURITY_1, "secret", nullptr, "short");
  invalid(WIFI_PROV_SCHEME_SOFTAP, WIFI_PROV_SECURITY_1, "secret", nullptr, std::string(64, 'k').c_str());
  assert(WiFiProv.beginProvision(WIFI_PROV_SCHEME_SOFTAP, WIFI_PROV_SECURITY_1,
    std::string(64, 'p').c_str(), std::string(32, 'n').c_str(), std::string(63, 'k').c_str(), true));
  assert(lastOpcode == c3::Opcode::WifiProvBegin && request.size() == 168);
  c3::Reader r(request.data(), request.size()); c3::ProvConfig config;
  assert(c3::readProvConfig(r, config) && config.reset && strlen(config.serviceKey) == 63);
  assert(WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SECURITY_0, nullptr));
  assert(request.size() == 9);
  ESPLink.caps.features = 0;
  size_t before = calls;
  assert(!WiFiProv.endProvision() && WiFiProv.lastError() == c3::Unsupported && calls == before);
  ESPLink.caps.features = c3::Provisioning;
  remoteError = c3::Busy;
  assert(!WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SECURITY_1, "secret"));
  assert(WiFiProv.lastError() == c3::Busy);
  remoteError = c3::Ok;
  assert(WiFiProv.endProvision() && lastOpcode == c3::Opcode::WifiProvEnd && request.empty());
  assert(WiFiProv.resetProvisioned() && lastOpcode == c3::Opcode::WifiProvReset);
  assert(WiFiProv.resetOnFailure() && lastOpcode == c3::Opcode::WifiProvRetry);

  // Golden response bytes, independent of the production writer.
  reply = {4, 1, 0, 1, 0, 4, 0, 't', 'e', 's', 't', 6, 0, 'P', 'R', 'O', 'V', '_', '1'};
  WiFiProvStatus status;
  assert(WiFiProv.getStatus(status) && status.state == WiFiProvState::Success);
  assert(!status.active && status.provisioned && std::string(status.ssid) == "test");
  assert(WiFiProv.isProvisioned() && WiFiProv.lastError() == c3::Ok);
  const auto good = reply;
  for (size_t n = 0; n < good.size(); ++n) {
    reply.assign(good.begin(), good.begin() + n);
    assert(!WiFiProv.getStatus(status) && WiFiProv.lastError() == c3::ProtocolError);
    assert(status.state == WiFiProvState::Success && std::string(status.ssid) == "test");
  }
  reply = good; reply.push_back(0);
  assert(!WiFiProv.getStatus(status));
  for (size_t field = 0; field < 5; ++field) {
    reply = good; reply[field] = 255;
    assert(!WiFiProv.getStatus(status) && WiFiProv.lastError() == c3::ProtocolError);
  }
  reply = good; reply[8] = 0;
  assert(!WiFiProv.getStatus(status));
  reply = good; reply[3] = 0;
  assert(!WiFiProv.isProvisioned() && WiFiProv.lastError() == c3::Ok);
  remoteError = c3::LinkLost;
  assert(!WiFiProv.isProvisioned() && WiFiProv.lastError() == c3::LinkLost);
  std::cout << "Provisioning API: validation, wire limits, capabilities, remote errors, status integrity PASS\n";
}
