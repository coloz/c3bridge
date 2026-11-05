#include "ProvisioningService.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include "network_provisioning/scheme_ble.h"
#include "network_provisioning/scheme_softap.h"

namespace {
int result(esp_err_t error) {
  if (error == ESP_OK) return c3::Ok;
  if (error == ESP_ERR_NO_MEM) return c3::NoMemory;
  if (error == ESP_ERR_INVALID_ARG) return c3::InvalidArgument;
  if (error == ESP_ERR_INVALID_STATE) return c3::Busy;
  if (error == ESP_ERR_NOT_SUPPORTED) return c3::Unsupported;
  return c3::IoError;
}
}

void ProvisioningService::event(void* context, esp_event_base_t, int32_t event, void* data) {
  auto& self = *static_cast<ProvisioningService*>(context);
  portENTER_CRITICAL(&self.lock_);
  switch (event) {
    case NETWORK_PROV_START: self.status_.state = c3::ProvState::Started; break;
    case NETWORK_PROV_WIFI_CRED_RECV:
      self.status_.state = c3::ProvState::CredentialsReceived;
      self.status_.failure = c3::ProvFailure::None;
      if (data) memcpy(self.status_.ssid, static_cast<wifi_sta_config_t*>(data)->ssid, 32);
      self.status_.ssid[32] = 0;
      break;
    case NETWORK_PROV_WIFI_CRED_FAIL:
      self.status_.state = c3::ProvState::Failed;
      self.status_.failure = data && *static_cast<network_prov_wifi_sta_fail_reason_t*>(data) == NETWORK_PROV_WIFI_STA_AUTH_ERROR
        ? c3::ProvFailure::AuthError : c3::ProvFailure::APNotFound;
      break;
    case NETWORK_PROV_WIFI_CRED_SUCCESS: self.status_.state = c3::ProvState::Success; break;
    case NETWORK_PROV_END:
      if (self.status_.state != c3::ProvState::Success && self.status_.state != c3::ProvState::Failed)
        self.status_.state = c3::ProvState::Stopped;
      break;
    case NETWORK_PROV_DEINIT: self.ended_ = true; break;
    default: break;
  }
  portEXIT_CRITICAL(&self.lock_);
}

int ProvisioningService::stop() {
  poll();
  if (!initialized_) return c3::Ok;
  // The core handles END by calling network_prov_mgr_deinit(). Do not race it
  // with a second deinit or a new init on the worker. DEINIT is the fence.
  network_prov_mgr_stop_provisioning();
  return waitForStop();
}
int ProvisioningService::waitForStop() {
  const uint32_t started = millis();
  do {
    poll();
    if (!initialized_) return c3::Ok;
    delay(1);
  } while (uint32_t(millis() - started) < 8000);
  // Keep ownership on timeout; a later DEINIT event still finishes cleanup.
  return c3::Timeout;
}
void ProvisioningService::poll() {
  portENTER_CRITICAL(&lock_);
  const bool ended = ended_;
  portEXIT_CRITICAL(&lock_);
  if (initialized_ && ended) {
    WiFi.setAutoReconnect(autoReconnect_);
    // Provisioned credentials stay in NVS; ordinary WiFi calls stay RAM-only.
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    memset(config_.pop, 0, sizeof(config_.pop));
    memset(config_.serviceKey, 0, sizeof(config_.serviceKey));
    initialized_ = false;
  }
}
bool ProvisioningService::reset() {
  if (stop() != c3::Ok) return false;
  portENTER_CRITICAL(&lock_);
  status_ = c3::ProvStatus{}; ended_ = false;
  portEXIT_CRITICAL(&lock_);
  return true;
}

int ProvisioningService::snapshot(c3::ProvStatus& status) {
  // Starting STA without connecting also makes NVS credentials available after
  // reboot or an ESPLink session reset. No provisioning manager is needed here.
  if (!WiFi.STA.begin(false)) return c3::IoError;
  wifi_config_t wifi{};
  const esp_err_t error = esp_wifi_get_config(WIFI_IF_STA, &wifi);
  if (error != ESP_OK) return result(error);
  portENTER_CRITICAL(&lock_);
  status = status_;
  portEXIT_CRITICAL(&lock_);
  status.active = initialized_;
  status.provisioned = wifi.sta.ssid[0] != 0;
  if (!status.active) { memcpy(status.ssid, wifi.sta.ssid, 32); status.ssid[32] = 0; }
  return c3::Ok;
}

int ProvisioningService::start(const c3::ProvConfig& config, bool hciRunning) {
  if (initialized_ || (config.scheme == c3::ProvScheme::BLE && hciRunning)) return c3::Busy;
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return c3::Busy;
  if (!WiFi.STA.begin(false)) return c3::IoError;
  // Register after STA.begin(), which installs the core's provisioning handler.
  // This service has static lifetime, matching the default event loop.
  if (!handler_) {
    const esp_err_t error = esp_event_handler_instance_register(NETWORK_PROV_EVENT,
      ESP_EVENT_ANY_ID, event, this, &handler_);
    if (error != ESP_OK) return result(error);
  }
  if (config.reset) {
    const esp_err_t error = network_prov_mgr_reset_wifi_provisioning();
    if (error != ESP_OK) return result(error);
  }
  wifi_config_t wifi{};
  esp_err_t error = esp_wifi_get_config(WIFI_IF_STA, &wifi);
  if (error != ESP_OK) return result(error);
  config_ = config;
  if (!config_.serviceName[0]) {
    uint8_t mac[6]{};
    error = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (error != ESP_OK) return result(error);
    snprintf(config_.serviceName, sizeof(config_.serviceName), "PROV_%02X%02X%02X", mac[3], mac[4], mac[5]);
  }
  portENTER_CRITICAL(&lock_);
  status_ = c3::ProvStatus{}; status_.scheme = config_.scheme;
  memcpy(status_.serviceName, config_.serviceName, sizeof(status_.serviceName));
  ended_ = false;
  portEXIT_CRITICAL(&lock_);
  if (wifi.sta.ssid[0]) {
    // Existing credentials: reconnect, without starting a provisioning service.
    memset(config_.pop, 0, sizeof(config_.pop));
    memset(config_.serviceKey, 0, sizeof(config_.serviceKey));
    return WiFi.begin() == WL_CONNECT_FAILED ? c3::IoError : c3::Ok;
  }
  autoReconnect_ = WiFi.getAutoReconnect();
  WiFi.setAutoReconnect(false); // The manager owns connection attempts until stop().
  network_prov_mgr_config_t manager{};
  manager.scheme = config.scheme == c3::ProvScheme::BLE ? network_prov_scheme_ble : network_prov_scheme_softap;
  // Never FREE_BLE/BTDM: releasing memory prevents subsequent ArduinoBLE use.
  manager.scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE;
  manager.app_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE;
  error = network_prov_mgr_init(manager);
  if (error != ESP_OK) {
    WiFi.setAutoReconnect(autoReconnect_);
    memset(config_.pop, 0, sizeof(config_.pop));
    memset(config_.serviceKey, 0, sizeof(config_.serviceKey));
    return result(error);
  }
  initialized_ = true;
  error = network_prov_mgr_start_provisioning(static_cast<network_prov_security_t>(config.security),
    config_.pop[0] ? config_.pop : nullptr, config_.serviceName,
    config_.serviceKey[0] ? config_.serviceKey : nullptr);
  if (error != ESP_OK) {
    // Failed start leaves an idle manager: no END event will trigger core cleanup.
    network_prov_mgr_deinit();
    const int cleanup = waitForStop();
    if (cleanup != c3::Ok) return cleanup;
  }
  return result(error);
}

int ProvisioningService::execute(c3::Opcode op, c3::Reader& r, c3::Writer& w, bool hciRunning) {
  poll();
  if (op == c3::Opcode::WifiProvBegin) {
    c3::ProvConfig config;
    if (!c3::readProvConfig(r, config)) return c3::InvalidArgument;
    return start(config, hciRunning);
  }
  if (!r.done()) return c3::InvalidArgument;
  switch (op) {
    case c3::Opcode::WifiProvEnd: return stop();
    case c3::Opcode::WifiProvStatus: {
      c3::ProvStatus status;
      const int error = snapshot(status);
      if (error != c3::Ok) return error;
      return c3::writeProvStatus(w, status) ? c3::Ok : c3::BufferTooSmall;
    }
    case c3::Opcode::WifiProvReset: {
      if (initialized_) return c3::Busy;
      if (!WiFi.STA.begin(false)) return c3::IoError;
      const int error = result(network_prov_mgr_reset_wifi_provisioning());
      if (error == c3::Ok) reset();
      return error;
    }
    case c3::Opcode::WifiProvRetry: {
      if (!initialized_) return c3::NotConnected;
      const int error = result(network_prov_mgr_reset_wifi_sm_state_on_failure());
      if (error == c3::Ok) {
        portENTER_CRITICAL(&lock_);
        status_.state = c3::ProvState::Started; status_.failure = c3::ProvFailure::None;
        memset(status_.ssid, 0, sizeof(status_.ssid));
        portEXIT_CRITICAL(&lock_);
      }
      return error;
    }
    default: return c3::Unsupported;
  }
}
