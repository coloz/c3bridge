#include "Backend.h"
#include <new>
#include "lwip/netdb.h"
#include "lwip/inet.h"
extern const uint8_t builtinCaStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t builtinCaEnd[] asm("_binary_x509_crt_bundle_end");
namespace backend {
Slot sockets[MaxSockets];
Certificate certificates[MaxCertificates];
uint32_t generation=1;
uint32_t newHandle(unsigned index) {
  generation=(generation+1)&0x00ffffff;
  if (!generation) generation=1;
  return (generation<<8)|(index+1);
}
Slot* slot(uint32_t handle) {
  unsigned i=handle&255U;
  if (!i||i>MaxSockets) return nullptr;
  return sockets[i-1].handle==handle ? &sockets[i-1] : nullptr;
}
Certificate* certificate(uint32_t handle) {
  unsigned i=handle&255U;
  if (!i||i>MaxCertificates) return nullptr;
  return certificates[i-1].handle==handle ? &certificates[i-1] : nullptr;
}
bool isClient(const Slot* s) { return s&&(s->kind==c3::SocketKind::TCP||s->kind==c3::SocketKind::TLS); }
void dropReference(uint32_t handle) { auto* c=certificate(handle); if(c&&c->refs) --c->refs; }
void clearSlot(Slot& s) {
  if(s.client) { s.client->stop(); delete s.client; }
  if(s.server) { s.server->end(); delete s.server; }
  if(s.udp>=0) close(s.udp);
  dropReference(s.ca); dropReference(s.cert); dropReference(s.key);
  s=Slot{};
}
void clearCertificate(Certificate& c) {
  if(c.bytes) {
    volatile uint8_t* ptr=c.bytes;
    for(size_t i=0;i<=c.total;++i) ptr[i]=0;
    free(c.bytes);
  }
  c=Certificate{};
}
void writeAddress(c3::Writer& w,const IPAddress& ip,bool unspecified) {
  uint8_t bytes[16]{};
  bool ipv6=ip.type()==IPv6;
  for(size_t i=0;i<(ipv6?16U:4U);++i) bytes[i]=ip[i];
  w.u8(unspecified?0:ipv6?6:4); w.u8(ipv6?ip.zone():0); w.bytes(bytes,16);
}
bool readAddress(c3::Reader& r,IPAddress& ip,uint8_t& family) {
  family=r.u8(); uint8_t zone=r.u8(), bytes[16]{}; r.bytes(bytes,16);
  if(!r.ok()||(family!=0&&family!=4&&family!=6)) return false;
  if(family!=6) {
    if(zone) return false;
    for(unsigned i=family==0?0:4;i<16;++i) if(bytes[i]) return false;
  }
  ip=family==6?IPAddress(IPv6,bytes,zone):IPAddress(bytes);
  return true;
}
IPAddress addressOf(const sockaddr_storage& address) {
  if(address.ss_family==AF_INET6) {
    auto* p=reinterpret_cast<const sockaddr_in6*>(&address);
    return IPAddress(IPv6,reinterpret_cast<const uint8_t*>(&p->sin6_addr),p->sin6_scope_id);
  }
  if(address.ss_family==AF_INET) {
    auto* p=reinterpret_cast<const sockaddr_in*>(&address);
    return IPAddress(reinterpret_cast<const uint8_t*>(&p->sin_addr));
  }
  return IPAddress();
}
uint16_t portOf(const sockaddr_storage& address) {
  if(address.ss_family==AF_INET6) return ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);
  if(address.ss_family==AF_INET) return ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
  return 0;
}
int resolve(const char* host,uint16_t port,int family,int kind,sockaddr_storage& address,socklen_t& size) {
  // Match Arduino IPAddress's numeric zone convention (%0 maps to zone 1).
  IPAddress literal;
  if(literal.fromString(host)) {
    const int actual=literal.type()==IPv6?AF_INET6:AF_INET;
    if(family!=AF_UNSPEC&&family!=actual) return c3::NotFound;
    memset(&address,0,sizeof(address));
    if(actual==AF_INET6) {
      auto* v6=reinterpret_cast<sockaddr_in6*>(&address);
      v6->sin6_family=AF_INET6; v6->sin6_port=htons(port); v6->sin6_scope_id=literal.zone();
      for(unsigned i=0;i<16;++i) reinterpret_cast<uint8_t*>(&v6->sin6_addr)[i]=literal[i];
      size=sizeof(*v6);
    } else {
      auto* v4=reinterpret_cast<sockaddr_in*>(&address);
      v4->sin_family=AF_INET; v4->sin_port=htons(port); v4->sin_addr.s_addr=static_cast<uint32_t>(literal);
      size=sizeof(*v4);
    }
    return c3::Ok;
  }
  return resolveHostname(host,port,family,kind,address,size);
}
bool createUdp(Slot& s,int family,uint16_t localPort) {
  if(s.udp>=0) close(s.udp);
  s.udp=socket(family,SOCK_DGRAM,IPPROTO_UDP); s.udpFamily=family;
  if(s.udp<0) return false;
  int enabled=1;
  setsockopt(s.udp,SOL_SOCKET,SO_REUSEADDR,&enabled,sizeof(enabled));
  setsockopt(s.udp,SOL_SOCKET,SO_BROADCAST,&enabled,sizeof(enabled));
  fcntl(s.udp,F_SETFL,O_NONBLOCK);
  sockaddr_storage local{}; socklen_t size;
  if(family==AF_INET6) {
    auto* v6=reinterpret_cast<sockaddr_in6*>(&local);
    v6->sin6_family=AF_INET6; v6->sin6_port=htons(localPort); size=sizeof(*v6);
  } else {
    auto* v4=reinterpret_cast<sockaddr_in*>(&local);
    v4->sin_family=AF_INET; v4->sin_port=htons(localPort); v4->sin_addr.s_addr=htonl(INADDR_ANY); size=sizeof(*v4);
  }
  if(bind(s.udp,reinterpret_cast<sockaddr*>(&local),size)<0) { close(s.udp); s.udp=-1; return false; }
  s.udpRequestedPort=localPort;
  return true;
}
void builtinCA(NetworkClientSecure& tls) { tls.setCACertBundle(builtinCaStart,builtinCaEnd-builtinCaStart); }
uint32_t openSlot(c3::SocketKind kind) {
  size_t tlsCount=0;
  for(auto& s:sockets) if(s.handle&&s.kind==c3::SocketKind::TLS) ++tlsCount;
  if(kind==c3::SocketKind::TLS&&tlsCount>=MaxTlsSockets) return 0;
  for(unsigned i=0;i<MaxSockets;++i) {
    Slot& s=sockets[i]; if(s.handle) continue; s.kind=kind;
    if(kind==c3::SocketKind::TCP) s.client=new(std::nothrow) NetworkClient();
    if(kind==c3::SocketKind::TLS) {
      auto* tls=new(std::nothrow) BridgeTLSClient();
      if(tls) { builtinCA(*tls); tls->setHandshakeTimeout(15); }
      s.client=tls;
    }
    if((kind==c3::SocketKind::TCP||kind==c3::SocketKind::TLS)&&!s.client) return 0;
    s.handle=newHandle(i); if(s.client) s.client->setConnectionTimeout(s.timeout);
    return s.handle;
  }
  return 0;
}
}