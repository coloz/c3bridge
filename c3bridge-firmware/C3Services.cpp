#include "C3Services.h"
#include "Backend.h"
#include "BridgeConfig.h"
#include <algorithm>
#include <time.h>
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
namespace {
HciPacket pendingHci;
size_t hciOffset=0;
bool haveHci=false;
char timeServers[2][254]={"pool.ntp.org","time.google.com"};
bool flag(uint8_t v) { return v<=1; }
}
bool C3Services::begin() {
  backend::generation=esp_random()&0x00ffffff;
  WiFi.persistent(false);
  configTime(0,0,timeServers[0],timeServers[1]);
  return backend::beginResolver();
}
void C3Services::reset() {
  if(!provisioning_.reset()) { ESP.restart(); return; }
  for(auto& s:backend::sockets) backend::clearSlot(s);
  for(auto& c:backend::certificates) backend::clearCertificate(c);
  hci_.end(); haveHci=false; hciOffset=0;
  WiFi.scanDelete(); WiFi.disconnect(false,false); WiFi.mode(WIFI_OFF);
}
void C3Services::execute(const c3::Frame& request,c3::Frame& response) {
  using namespace backend;
  c3::Reader r(request.payload,request.size);
  c3::Writer w(response.payload,sizeof(response.payload));
  const auto op=static_cast<c3::Opcode>(request.opcode);
  int status=c3::Ok;
  auto run=[&]() -> int {
    provisioning_.poll();
    if(request.opcode>=0x0130&&request.opcode<=0x0134)
      return provisioning_.execute(op,r,w,hci_.running());
    // Provisioning owns WiFi configuration and scans until its transport stops.
    if(provisioning_.active()) {
      switch(op) {
        case c3::Opcode::WifiMode: case c3::Opcode::WifiBegin:
        case c3::Opcode::WifiDisconnect: case c3::Opcode::WifiConfig:
        case c3::Opcode::WifiScanStart: case c3::Opcode::WifiScanDelete:
        case c3::Opcode::WifiAPBegin: case c3::Opcode::WifiAPConfig:
        case c3::Opcode::WifiAPStop: case c3::Opcode::WifiReconnect:
        case c3::Opcode::WifiAutoReconnect: case c3::Opcode::WifiSetOption:
          return c3::Busy;
        default: break;
      }
    }
    if(request.opcode>=0x0200&&request.opcode<0x0240) return network(op,r,w);
    if(request.opcode>=0x0240&&request.opcode<0x0300) return security(op,r,w);
    switch(op) {
      case c3::Opcode::Echo: w.bytes(request.payload,request.size); break;
      case c3::Opcode::SystemInfo:
        if(!r.done()) return c3::InvalidArgument;
        w.string(C3_BRIDGE_VERSION); w.u32(Features); w.u32(ESP.getFreeHeap());
        w.u32(ESP.getMinFreeHeap()); w.u32(millis()); break;
      case c3::Opcode::Restart:
        if(!r.done()) return c3::InvalidArgument;
        break; // UART task restarts only after this response drains.
      case c3::Opcode::WifiMode: {
        uint8_t mode=r.u8();
        if(!r.done()||mode>WIFI_AP_STA) return c3::InvalidArgument;
        if(!WiFi.mode(static_cast<wifi_mode_t>(mode))) return c3::IoError;
        break;
      }
      case c3::Opcode::WifiBegin: {
        char ssid[33]{},password[65]{}; uint8_t bssid[6]{};
        r.string(ssid,sizeof(ssid)); r.string(password,sizeof(password));
        int32_t channel=r.i32(); uint8_t has=r.u8(); r.bytes(bssid,6);
        if(!r.done()||!ssid[0]||channel<0||channel>14||!flag(has)) return c3::InvalidArgument;
        WiFi.enableIPv6(true);
        if(WiFi.begin(ssid,password[0]?password:nullptr,channel,has?bssid:nullptr)==WL_CONNECT_FAILED) return c3::IoError;
        break;
      }
      case c3::Opcode::WifiDisconnect: {
        uint8_t erase=r.u8(),off=r.u8();
        if(!r.done()||!flag(erase)||!flag(off)) return c3::InvalidArgument;
        if(!WiFi.disconnect(off,erase)) return c3::IoError;
        break;
      }
      case c3::Opcode::WifiStatus: {
        if(!r.done()) return c3::InvalidArgument;
        uint8_t mac[6]{},bssid[6]{}; WiFi.macAddress(mac); WiFi.BSSID(bssid);
        w.u8(WiFi.status()); w.u8(WiFi.getMode()); w.i32(WiFi.RSSI()); w.u8(WiFi.channel());
        w.bytes(mac,6); w.bytes(bssid,6); w.string(WiFi.SSID().c_str()); w.string(WiFi.getHostname());
        writeAddress(w,WiFi.localIP()); writeAddress(w,WiFi.gatewayIP()); writeAddress(w,WiFi.subnetMask());
        writeAddress(w,WiFi.dnsIP(0)); writeAddress(w,WiFi.dnsIP(1)); break;
      }
      case c3::Opcode::WifiConfig:
      case c3::Opcode::WifiAPConfig: {
        IPAddress ip,gw,mask,dns1,dns2; uint8_t a=0,b=0,c=0,d=0,e=0;
        bool valid=readAddress(r,ip,a)&&readAddress(r,gw,b)&&readAddress(r,mask,c);
        if(op==c3::Opcode::WifiConfig) valid=valid&&readAddress(r,dns1,d)&&readAddress(r,dns2,e);
        if(!valid||!r.done()) return c3::InvalidArgument;
        if(a==6||b==6||c==6||d==6||e==6) return c3::Unsupported;
        bool success=op==c3::Opcode::WifiConfig?WiFi.config(ip,gw,mask,dns1,dns2):WiFi.softAPConfig(ip,gw,mask);
        if(!success) return c3::IoError;
        break;
      }
      case c3::Opcode::WifiDNS: {
        IPAddress dns1,dns2; uint8_t a=0,b=0;
        if(!readAddress(r,dns1,a)||!readAddress(r,dns2,b)||!r.done()) return c3::InvalidArgument;
        if(!WiFi.setDNS(dns1,dns2)) return c3::IoError;
        break;
      }
      case c3::Opcode::WifiScanStart: {
        uint8_t hidden=r.u8(),passive=r.u8(); uint16_t dwell=r.u16(); uint8_t channel=r.u8();
        if(!r.done()||!flag(hidden)||!flag(passive)||!dwell||dwell>1500||channel>14) return c3::InvalidArgument;
        if(WiFi.scanComplete()==WIFI_SCAN_RUNNING) return c3::Busy;
        WiFi.scanDelete();
        if(WiFi.scanNetworks(true,hidden,passive,dwell,channel)==WIFI_SCAN_FAILED) return c3::IoError;
        break;
      }
      case c3::Opcode::WifiScanStatus:
        if(!r.done()) return c3::InvalidArgument;
        w.i32(WiFi.scanComplete()); break;
      case c3::Opcode::WifiScanResult: {
        uint16_t index=r.u16(); if(!r.done()) return c3::InvalidArgument;
        int count=WiFi.scanComplete(); if(count<0||index>=count) return c3::NotFound;
        auto* info=static_cast<const wifi_ap_record_t*>(WiFi.getScanInfoByIndex(index));
        if(!info) return c3::NotFound;
        char ssid[33]{}; memcpy(ssid,info->ssid,32);
        w.string(ssid); w.i32(info->rssi); w.u8(info->authmode); w.u8(info->primary);
        w.bytes(info->bssid,6); w.u8(ssid[0]==0); break;
      }
      case c3::Opcode::WifiScanDelete:
        if(!r.done()) return c3::InvalidArgument;
        WiFi.scanDelete(); break;
      case c3::Opcode::WifiAPBegin: {
        char ssid[33]{},password[65]{};
        r.string(ssid,sizeof(ssid)); r.string(password,sizeof(password));
        uint8_t channel=r.u8(),hidden=r.u8(),maxClients=r.u8();
        if(!r.done()||!ssid[0]||channel<1||channel>14||!flag(hidden)||!maxClients||maxClients>10||
           (password[0]&&strlen(password)<8)) return c3::InvalidArgument;
        if(!WiFi.softAP(ssid,password[0]?password:nullptr,channel,hidden,maxClients)) return c3::IoError;
        break;
      }
      case c3::Opcode::WifiAPStop: {
        uint8_t off=r.u8(); if(!r.done()||!flag(off)) return c3::InvalidArgument;
        if(!WiFi.softAPdisconnect(off)) return c3::IoError;
        break;
      }
      case c3::Opcode::WifiAPStatus: {
        if(!r.done()) return c3::InvalidArgument;
        uint8_t mac[6]{}; WiFi.softAPmacAddress(mac);
        w.u8(WiFi.softAPgetStationNum()); w.string(WiFi.softAPSSID().c_str()); w.bytes(mac,6);
        writeAddress(w,WiFi.softAPIP()); break;
      }
      case c3::Opcode::WifiHostname: {
        char name[64]{}; r.string(name,sizeof(name));
        if(!r.done()||!name[0]) return c3::InvalidArgument;
        if(!WiFi.setHostname(name)) return c3::IoError;
        break;
      }
      case c3::Opcode::WifiReconnect:
        if(!r.done()) return c3::InvalidArgument;
        if(!WiFi.reconnect()) return c3::IoError;
        break;
      case c3::Opcode::WifiAutoReconnect: {
        uint8_t enable=r.u8(); if(!r.done()||!flag(enable)) return c3::InvalidArgument;
        WiFi.setAutoReconnect(enable); break;
      }
      case c3::Opcode::WifiSetOption: {
        uint8_t option=r.u8(); int32_t value=r.i32(); if(!r.done()) return c3::InvalidArgument;
        bool success=false;
        if(option==1&&value>=0&&value<=2) success=WiFi.setSleep(static_cast<wifi_ps_type_t>(value));
        else if(option==2&&value>=-4&&value<=84) success=WiFi.setTxPower(static_cast<wifi_power_t>(value));
        else if(option==3&&value>=0&&value<=1) success=WiFi.setAutoReconnect(value);
        else return c3::InvalidArgument;
        if(!success) return c3::IoError;
        break;
      }
      case c3::Opcode::WifiGetOption: {
        uint8_t option=r.u8(); if(!r.done()) return c3::InvalidArgument;
        if(option==1) w.i32(WiFi.getSleep());
        else if(option==2) w.i32(WiFi.getTxPower());
        else if(option==3) w.i32(WiFi.getAutoReconnect());
        else return c3::InvalidArgument;
        break;
      }
      case c3::Opcode::DnsResolve: {
        char host[254]{}; r.string(host,sizeof(host)); uint8_t family=r.u8();
        if(!r.done()||!host[0]||(family!=0&&family!=4&&family!=6)) return c3::InvalidArgument;
        sockaddr_storage address{}; socklen_t size=0;
        int result=resolve(host,0,family==4?AF_INET:family==6?AF_INET6:AF_UNSPEC,SOCK_STREAM,address,size);
        if(result!=c3::Ok) return result;
        writeAddress(w,addressOf(address)); break;
      }
      case c3::Opcode::TimeConfig: {
        int32_t gmt=r.i32(),daylight=r.i32(); char server1[254]{},server2[254]{};
        r.string(server1,sizeof(server1)); r.string(server2,sizeof(server2));
        if(!r.done()||!server1[0]||gmt<-86400||gmt>86400||daylight<-86400||daylight>86400) return c3::InvalidArgument;
        esp_sntp_stop();
        memcpy(timeServers[0],server1,sizeof(server1));
        memcpy(timeServers[1],server2,sizeof(server2));
        configTime(gmt,daylight,timeServers[0],timeServers[1][0]?timeServers[1]:nullptr); break;
      }
      case c3::Opcode::TimeGet: {
        if(!r.done()) return c3::InvalidArgument;
        uint64_t now=static_cast<uint64_t>(time(nullptr)); w.u32(now); w.u32(now>>32); break;
      }
      case c3::Opcode::HciBegin:
        if(!r.done()) return c3::InvalidArgument;
        if(provisioning_.ownsBLE()) return c3::Busy;
        if(!hci_.begin()) return c3::IoError;
        haveHci=false; hciOffset=0; break;
      case c3::Opcode::HciEnd:
        if(!r.done()) return c3::InvalidArgument;
        if(provisioning_.ownsBLE()) return c3::Busy;
        hci_.end(); haveHci=false; hciOffset=0; break;
      case c3::Opcode::HciWrite:
        if(!hci_.running()) return c3::NotConnected;
        if(hci_.failed()) return c3::IoError;
        if(!hci_.enqueue(request.payload,request.size)) return c3::WouldBlock;
        w.u16(request.size); break;
      case c3::Opcode::HciRead: {
        uint16_t maximum=r.u16(); if(!r.done()||maximum>c3::MaxPayload) return c3::InvalidArgument;
        if(!hci_.running()) return c3::NotConnected;
        if(hci_.failed()) return c3::IoError;
        while(w.size()<maximum) {
          if(!haveHci) { if(!hci_.receive(pendingHci)) break; haveHci=true; hciOffset=0; }
          size_t count=std::min<size_t>(maximum-w.size(),pendingHci.size-hciOffset);
          w.bytes(pendingHci.bytes+hciOffset,count); hciOffset+=count;
          if(hciOffset==pendingHci.size) haveHci=false;
        }
        break;
      }
      case c3::Opcode::HciStatus:
        if(!r.done()) return c3::InvalidArgument;
        w.u8(hci_.running()); w.u32(hci_.queuedBytes()+(haveHci?pendingHci.size-hciOffset:0));
        w.u32(hci_.overflowCount()); w.u16(C3_HCI_MAX_PACKET); w.u8(hci_.failed()); break;
      default: return c3::Unsupported;
    }
    return w.ok()?c3::Ok:c3::BufferTooSmall;
  };
  status=run();
  response.status=status; response.size=status==c3::Ok?w.size():0;
}
