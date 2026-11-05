#include "WiFiProv.h"

WiFiProvClass WiFiProv;

bool WiFiProvClass::ensure() {
  if (!ESPLink.ready() && !ESPLink.begin()) { error_ = c3::LinkLost; return false; }
  if (!(ESPLink.capabilities().features & c3::Provisioning)) { error_ = c3::Unsupported; return false; }
  return true;
}
bool WiFiProvClass::command(c3::Opcode opcode) {
  if (!ensure()) return false;
  size_t size = 0;
  error_ = ESPLink.request(static_cast<uint16_t>(opcode), nullptr, 0, nullptr, size, 10000);
  return error_ == c3::Ok;
}
bool WiFiProvClass::beginProvision(prov_scheme_t scheme, wifi_prov_security_t security,
  const char* pop, const char* serviceName, const char* serviceKey, bool resetProvisioned) {
  c3::ProvConfig config;
  if ((pop && strlen(pop) >= sizeof(config.pop)) ||
      (serviceName && strlen(serviceName) >= sizeof(config.serviceName)) ||
      (serviceKey && strlen(serviceKey) >= sizeof(config.serviceKey))) {
    error_ = c3::InvalidArgument; return false;
  }
  config.scheme = scheme; config.security = security; config.reset = resetProvisioned;
  if (pop) strcpy(config.pop, pop);
  if (serviceName) strcpy(config.serviceName, serviceName);
  if (serviceKey) strcpy(config.serviceKey, serviceKey);
  if (!config.valid()) { error_ = c3::InvalidArgument; return false; }
  if (!ensure()) return false;
  uint8_t request[168]; c3::Writer w(request, sizeof(request));
  if (!c3::writeProvConfig(w, config)) { error_ = c3::InvalidArgument; return false; }
  size_t size = 0;
  error_ = ESPLink.request(static_cast<uint16_t>(c3::Opcode::WifiProvBegin), request, w.size(), nullptr, size, 15000);
  return error_ == c3::Ok;
}
bool WiFiProvClass::endProvision() { return command(c3::Opcode::WifiProvEnd); }
bool WiFiProvClass::resetProvisioned() { return command(c3::Opcode::WifiProvReset); }
bool WiFiProvClass::resetOnFailure() { return command(c3::Opcode::WifiProvRetry); }
bool WiFiProvClass::getStatus(WiFiProvStatus& status) {
  if (!ensure()) return false;
  uint8_t reply[73]; size_t size = sizeof(reply);
  error_ = ESPLink.request(static_cast<uint16_t>(c3::Opcode::WifiProvStatus), nullptr, 0, reply, size, 5000);
  if (error_ != c3::Ok) return false;
  c3::Reader r(reply, size); WiFiProvStatus snapshot;
  if (!c3::readProvStatus(r, snapshot)) { error_ = c3::ProtocolError; return false; }
  status = snapshot;
  return true;
}
bool WiFiProvClass::isProvisioned() {
  WiFiProvStatus status;
  return getStatus(status) && status.provisioned;
}
