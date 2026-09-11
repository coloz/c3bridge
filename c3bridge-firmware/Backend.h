#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include "BridgeTLSClient.h"
#include <NetworkServer.h>
#include "lwip/sockets.h"
#include "C3Protocol.h"
namespace backend {
constexpr size_t MaxSockets=8, MaxCertificates=8, MaxCertBytes=8192, MaxCertTotal=32768;
constexpr size_t MaxDatagram=1472, MaxTlsSockets=2;
constexpr uint32_t MaxOperationTimeout=30000;
struct Certificate {
  uint32_t handle=0;
  c3::CertKind kind=c3::CertKind::CA;
  uint8_t* bytes=nullptr;
  size_t total=0, written=0;
  uint16_t refs=0;
  bool committed=false;
};
struct Slot {
  uint32_t handle=0;
  c3::SocketKind kind=c3::SocketKind::TCP;
  NetworkClient* client=nullptr;
  NetworkServer* server=nullptr;
  int udp=-1, udpFamily=AF_UNSPEC;
  sockaddr_storage destination{}, source{};
  socklen_t destinationSize=0, sourceSize=0;
  uint8_t tx[MaxDatagram]{}, rx[MaxDatagram]{};
  uint16_t txSize=0, rxSize=0, rxOffset=0;
  bool txStarted=false;
  uint16_t udpRequestedPort=0;
  bool udpMulticast=false;
  uint32_t ca=0, cert=0, key=0, timeout=1000;
};
extern Slot sockets[MaxSockets];
extern Certificate certificates[MaxCertificates];
extern uint32_t generation;
uint32_t newHandle(unsigned index);
Slot* slot(uint32_t handle);
Certificate* certificate(uint32_t handle);
bool isClient(const Slot* s);
void dropReference(uint32_t handle);
void clearSlot(Slot& s);
void clearCertificate(Certificate& c);
void writeAddress(c3::Writer& w,const IPAddress& ip,bool unspecified=false);
bool readAddress(c3::Reader& r,IPAddress& ip,uint8_t& family);
IPAddress addressOf(const sockaddr_storage& address);
uint16_t portOf(const sockaddr_storage& address);
int resolve(const char* host,uint16_t port,int family,int kind,sockaddr_storage& address,socklen_t& size);
bool beginResolver();
int resolveHostname(const char* host,uint16_t port,int family,int kind,sockaddr_storage& address,socklen_t& size);
bool createUdp(Slot& s,int family,uint16_t localPort);
void builtinCA(NetworkClientSecure& tls);
uint32_t openSlot(c3::SocketKind kind);
int network(c3::Opcode op,c3::Reader& r,c3::Writer& w);
int security(c3::Opcode op,c3::Reader& r,c3::Writer& w);
}