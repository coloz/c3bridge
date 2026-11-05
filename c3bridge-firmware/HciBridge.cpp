#include "HciBridge.h"
// Prevent initArduino() from releasing controller memory before setup().
#include "esp32-hal-alloc-ble-mem.h"
#include "esp_bt.h"
HciBridge* HciBridge::instance_=nullptr;

bool HciBridge::begin() {
  if(!controllerLock_) controllerLock_=xSemaphoreCreateMutex();
  const bool incomingReady=incoming_.begin(RxStorageBytes);
  if(!outgoing_) outgoing_=xQueueCreate(C3_HCI_TX_DEPTH,sizeof(HciPacket));
  if(!controllerLock_||!incomingReady||!outgoing_) return false;
  xSemaphoreTake(controllerLock_,portMAX_DELAY);
  if(active_&&!fault_) { xSemaphoreGive(controllerLock_); return true; }
  active_=false;
  if(esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_ENABLED) esp_bt_controller_disable();
  if(esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_INITED) esp_bt_controller_deinit();
  portENTER_CRITICAL(&statsLock_);
  incoming_.reset(); xQueueReset(outgoing_);
  overflows_=0; invalid_=0; fault_=false;
  portEXIT_CRITICAL(&statsLock_);
  instance_=this;
  esp_bt_controller_config_t cfg=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  cfg.hci_tl_type=ESP_BT_CTRL_HCI_TL_VHCI;
  esp_err_t err=esp_bt_controller_init(&cfg);
  if(err==ESP_OK) err=esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if(err==ESP_OK) {
    static const esp_vhci_host_callback_t callbacks={sendReady,receivePacket};
    active_=true; err=esp_vhci_host_register_callback(&callbacks);
  }
  if(err!=ESP_OK) {
    active_=false;
    if(esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_ENABLED) esp_bt_controller_disable();
    if(esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_INITED) esp_bt_controller_deinit();
  }
  xSemaphoreGive(controllerLock_);
  return err==ESP_OK;
}
void HciBridge::end() {
  if(!controllerLock_) return;
  xSemaphoreTake(controllerLock_,portMAX_DELAY);
  active_=false;
  if(esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_ENABLED) esp_bt_controller_disable();
  if(esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_INITED) esp_bt_controller_deinit();
  portENTER_CRITICAL(&statsLock_);
  incoming_.reset();
  if(outgoing_) xQueueReset(outgoing_);
  overflows_=0; invalid_=0; fault_=false;
  portEXIT_CRITICAL(&statsLock_);
  xSemaphoreGive(controllerLock_);
  // Do not release controller memory: subsequent BLE.begin() must work.
}
bool HciBridge::enqueue(const uint8_t* data,size_t size) {
  if(!active_||fault_||!data||!size||size>C3_HCI_MAX_PACKET) return false;
  size_t expected=0;
  if(data[0]==0x01&&size>=4) expected=4U+data[3];
  else if(data[0]==0x02&&size>=5) expected=5U+data[3]+(size_t(data[4])<<8);  
  if(size!=expected) { invalid_=invalid_+1; return false; }
  HciPacket packet{}; packet.size=size; memcpy(packet.bytes,data,size);
  return xQueueSend(outgoing_,&packet,0)==pdTRUE;
}
bool HciBridge::receive(HciPacket& packet) {
  portENTER_CRITICAL(&statsLock_);
  bool ok=incoming_.pop(packet.bytes,sizeof(packet.bytes),packet.size);
  portEXIT_CRITICAL(&statsLock_);
  return ok;
}
uint32_t HciBridge::queuedBytes() {
  portENTER_CRITICAL(&statsLock_);
  uint32_t value=static_cast<uint32_t>(incoming_.queuedBytes());
  portEXIT_CRITICAL(&statsLock_);
  return value;
}
void HciBridge::poll() {
  if(!controllerLock_||xSemaphoreTake(controllerLock_,0)!=pdTRUE) return;
  if(active_&&!fault_&&esp_vhci_host_check_send_available()) {
    HciPacket packet{};
    if(xQueueReceive(outgoing_,&packet,0)==pdTRUE) {
      // Scheduler remains active; this mutex is not an interrupt critical section.
      esp_vhci_host_send_packet(packet.bytes,packet.size);
    }
  }
  xSemaphoreGive(controllerLock_);
}
void HciBridge::sendReady() {}
int HciBridge::receivePacket(uint8_t* data,uint16_t size) {
  HciBridge* b=instance_;
  if(!b||!b->active_) return -1;
  if(!data||!size||size>C3_HCI_MAX_PACKET) { b->invalid_=b->invalid_+1; b->fault_=true; return -1; }
  portENTER_CRITICAL(&b->statsLock_);
  bool ok=b->incoming_.push(data,size);
  if(!ok) { b->overflows_=b->overflows_+1; b->fault_=true; }
  portEXIT_CRITICAL(&b->statsLock_);
  return ok?0:-1;
}
