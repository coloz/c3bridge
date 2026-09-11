#include "Backend.h"
#include <algorithm>
#include <new>
#include "lwip/inet.h"
#include "lwip/tcp.h"
namespace backend {
int network(c3::Opcode op,c3::Reader& r,c3::Writer& w) {
  switch(op) {
    case c3::Opcode::SocketOpen: {
      uint8_t kind=r.u8(); if(!r.done()||kind>3) return c3::InvalidArgument;
      uint32_t handle=openSlot(static_cast<c3::SocketKind>(kind));
      if(!handle) return c3::NoMemory;
      w.u32(handle); break;
    }
    case c3::Opcode::SocketConnect: {
      Slot* s=slot(r.u32()); char host[254]{}; r.string(host,sizeof(host));
      uint16_t port=r.u16(); uint32_t timeout=r.u32();
      if(!r.done()||!host[0]||!port||!timeout||timeout>MaxOperationTimeout) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      if(!isClient(s)) return c3::InvalidArgument;
      if(s->client->connected()) return c3::Busy;
      sockaddr_storage endpoint{}; socklen_t endpointSize=0;
      int resolved=resolve(host,port,AF_UNSPEC,SOCK_STREAM,endpoint,endpointSize);
      if(resolved!=c3::Ok) return resolved;
      const IPAddress ip=addressOf(endpoint);
      int connected=s->kind==c3::SocketKind::TLS?
        static_cast<BridgeTLSClient*>(s->client)->connectResolved(ip,port,host,timeout):
        s->client->connect(ip,port,timeout);
      s->client->setConnectionTimeout(s->timeout);
      if(!connected) return s->kind==c3::SocketKind::TLS?c3::SecurityError:c3::IoError;
      break;
    }
    case c3::Opcode::SocketRead: {
      Slot* s=slot(r.u32()); uint16_t maximum=r.u16();
      if(!r.done()||maximum>c3::MaxPayload-2) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      if(!isClient(s)) return c3::InvalidArgument;
      int available=s->client->available();
      size_t count=available>0?std::min<size_t>(available,maximum):0;
      uint8_t buffer[c3::MaxPayload-2];
      int n=count?s->client->read(buffer,count):0; if(n<0) n=0;
      w.u16(n); w.bytes(buffer,n); break;
    }
    case c3::Opcode::SocketWrite: {
      Slot* s=slot(r.u32()); size_t size=r.remaining();
      if(!r.ok()) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      if(!isClient(s)) return c3::InvalidArgument;
      if(!s->client->connected()) return c3::NotConnected;
      size_t accepted=0;
      if(size&&s->kind==c3::SocketKind::TCP) {
        int sent=send(s->client->fd(),r.current(),size,MSG_DONTWAIT);
        if(sent>0) accepted=sent;
        else if(sent<0&&errno!=EWOULDBLOCK&&errno!=EAGAIN) return c3::IoError;
      } else if(size) accepted=static_cast<BridgeTLSClient*>(s->client)->writeBounded(r.current(),size,s->timeout);
      // Preserve partial send count; a retransmitted RPC replays this result.
      w.u32(accepted); break;
    }
    case c3::Opcode::SocketStatus: {
      Slot* s=slot(r.u32()); if(!r.done()) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      if(!isClient(s)) return c3::InvalidArgument;
      uint8_t connected=s->client->connected(); int available=s->client->available();
      uint32_t writable=0; int fd=s->client->fd();
      if(connected&&fd>=0) {
        fd_set output; FD_ZERO(&output); FD_SET(fd,&output); timeval timeout{};
        if(select(fd+1,nullptr,&output,nullptr,&timeout)>0) writable=c3::MaxPayload-4;
      }
      w.u8(connected); w.u32(available>0?available:0); w.u32(writable);
      writeAddress(w,s->client->remoteIP(),fd<0);
      w.u16(s->client->remotePort()); w.u16(s->client->localPort());
      writeAddress(w,s->client->localIP(),fd<0); break;
    }
    case c3::Opcode::SocketClose: {
      Slot* s=slot(r.u32()); if(!r.done()) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      clearSlot(*s); break;
    }
    case c3::Opcode::SocketListen: {
      Slot* s=slot(r.u32()); uint16_t port=r.u16(); uint8_t backlog=r.u8();
      if(!r.done()||!port||!backlog||backlog>MaxSockets) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      if(s->kind!=c3::SocketKind::Server) return c3::InvalidArgument;
      if(s->server) return c3::Busy;
      s->server=new(std::nothrow) NetworkServer(port,backlog); if(!s->server) return c3::NoMemory;
      s->server->begin();
      if(!*s->server) { delete s->server; s->server=nullptr; return c3::IoError; }
      break;
    }
    case c3::Opcode::SocketAccept: {
      Slot* s=slot(r.u32()); if(!r.done()) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      if(s->kind!=c3::SocketKind::Server||!s->server) return c3::InvalidArgument;
      if(!s->server->hasClient()) { w.u32(0); break; }
      uint32_t handle=openSlot(c3::SocketKind::TCP); if(!handle) return c3::NoMemory;
      Slot* child=slot(handle); *child->client=s->server->accept();
      if(!child->client->connected()) { clearSlot(*child); w.u32(0); break; }
      child->client->setConnectionTimeout(child->timeout); w.u32(handle); break;
    }
    case c3::Opcode::SocketOption: {
      Slot* s=slot(r.u32()); uint8_t option=r.u8(); uint32_t value=r.u32();
      if(!r.done()) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      if(!isClient(s)) return c3::InvalidArgument;
      if(option==2) {
        if(!value||value>MaxOperationTimeout) return c3::InvalidArgument;
        s->timeout=value; s->client->setConnectionTimeout(value); break;
      }
      if(s->client->fd()<0) return c3::NotConnected;
      int ivalue=static_cast<int>(value),result=-1;
      if(option==1&&value<=1) result=setsockopt(s->client->fd(),IPPROTO_TCP,TCP_NODELAY,&ivalue,sizeof(ivalue));
      else if(option>=3&&option<=5&&value>0&&value<=86400) {
        int enable=1; setsockopt(s->client->fd(),SOL_SOCKET,SO_KEEPALIVE,&enable,sizeof(enable));
        int key=option==3?TCP_KEEPIDLE:option==4?TCP_KEEPINTVL:TCP_KEEPCNT;
        result=setsockopt(s->client->fd(),IPPROTO_TCP,key,&ivalue,sizeof(ivalue));
      } else return c3::InvalidArgument;
      if(result<0) return c3::IoError;
      break;
    }
    case c3::Opcode::SocketClearRx: {
      Slot* s=slot(r.u32()); if(!r.done()) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      if(!isClient(s)) return c3::InvalidArgument;
      size_t remaining=std::min<int>(65536,std::max<int>(0,s->client->available()));
      uint8_t discard[256];
      while(remaining) {
        int n=s->client->read(discard,std::min(remaining,sizeof(discard))); if(n<=0) break;
        remaining-=n;
      }
      break;
    }
    case c3::Opcode::UdpBind: {
      Slot* s=slot(r.u32()); uint16_t port=r.u16(); IPAddress group; uint8_t family=0;
      bool valid=readAddress(r,group,family); if(!valid||!r.done()) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      if(s->kind!=c3::SocketKind::UDP) return c3::InvalidArgument;
      if((family==4&&(group[0]&0xf0)!=0xe0)||(family==6&&group[0]!=0xff)) return c3::InvalidArgument;
      if(!createUdp(*s,family==6?AF_INET6:AF_INET,port)) return c3::IoError;
      int joined=0;
      if(family==4) {
        ip_mreq membership{}; membership.imr_multiaddr.s_addr=static_cast<uint32_t>(group);
        membership.imr_interface.s_addr=htonl(INADDR_ANY);
        joined=setsockopt(s->udp,IPPROTO_IP,IP_ADD_MEMBERSHIP,&membership,sizeof(membership));
      } else if(family==6) {
        ipv6_mreq membership{};
        for(unsigned i=0;i<16;++i) reinterpret_cast<uint8_t*>(&membership.ipv6mr_multiaddr)[i]=group[i];
        membership.ipv6mr_interface=group.zone();
        joined=setsockopt(s->udp,IPPROTO_IPV6,IPV6_JOIN_GROUP,&membership,sizeof(membership));
      }
      if(joined<0) { close(s->udp); s->udp=-1; return c3::IoError; }
      s->udpMulticast=family!=0;
      s->rxSize=s->rxOffset=0; break;
    }
    case c3::Opcode::UdpTxBegin: {
      Slot* s=slot(r.u32()); char host[254]{}; r.string(host,sizeof(host)); uint16_t port=r.u16();
      if(!r.done()||!host[0]||!port) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      if(s->kind!=c3::SocketKind::UDP) return c3::InvalidArgument;
      s->txSize=0; s->txStarted=false;
      int result=resolve(host,port,AF_UNSPEC,SOCK_DGRAM,s->destination,s->destinationSize);
      if(result!=c3::Ok) return result;
      if(s->udp>=0&&s->udpFamily!=s->destination.ss_family) {
        if(s->udpRequestedPort||s->udpMulticast) return c3::Unsupported;
        close(s->udp); s->udp=-1; // Keep the independently staged received datagram.
      }
      if(s->udp<0&&!createUdp(*s,s->destination.ss_family,0)) return c3::IoError;
      s->txStarted=true; break;
    }
    case c3::Opcode::UdpTxData: {
      Slot* s=slot(r.u32()); size_t count=r.remaining();
      if(!r.ok()) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      if(s->kind!=c3::SocketKind::UDP||!s->txStarted) return c3::InvalidArgument;
      if(count>MaxDatagram-s->txSize) return c3::BufferTooSmall;
      memcpy(s->tx+s->txSize,r.current(),count); s->txSize+=count; w.u16(count); break;
    }
    case c3::Opcode::UdpTxEnd: {
      Slot* s=slot(r.u32()); if(!r.done()) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      if(s->kind!=c3::SocketKind::UDP||!s->txStarted||s->udp<0) return c3::InvalidArgument;
      int sent=sendto(s->udp,s->tx,s->txSize,MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&s->destination),s->destinationSize);
      s->txStarted=false;
      if(sent!=s->txSize) return c3::IoError;
      break;
    }
    case c3::Opcode::UdpRxBegin: {
      Slot* s=slot(r.u32()); if(!r.done()) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      if(s->kind!=c3::SocketKind::UDP||s->udp<0) return c3::InvalidArgument;
      uint8_t probe; sockaddr_storage incoming{}; socklen_t incomingSize=sizeof(incoming);
      int length=recvfrom(s->udp,&probe,1,MSG_PEEK|MSG_DONTWAIT|MSG_TRUNC,reinterpret_cast<sockaddr*>(&incoming),&incomingSize);
      if(length<0&&errno!=EWOULDBLOCK&&errno!=EAGAIN) return c3::IoError;
      if(length<0) { w.u16(0); writeAddress(w,IPAddress(),true); w.u16(0); break; }
      if(length>MaxDatagram) {
        recvfrom(s->udp,&probe,1,MSG_DONTWAIT,nullptr,nullptr);
        return c3::BufferTooSmall;
      }
      s->sourceSize=sizeof(s->source);
      int received=recvfrom(s->udp,s->rx,sizeof(s->rx),MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&s->source),&s->sourceSize);
      if(received<0) return c3::IoError;
      s->rxSize=received; s->rxOffset=0;
      w.u16(s->rxSize); writeAddress(w,addressOf(s->source)); w.u16(portOf(s->source)); break;
    }
    case c3::Opcode::UdpRead: {
      Slot* s=slot(r.u32()); uint16_t maximum=r.u16();
      if(!r.done()||maximum>c3::MaxPayload-2) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      if(s->kind!=c3::SocketKind::UDP) return c3::InvalidArgument;
      uint16_t count=std::min<uint16_t>(maximum,s->rxSize-s->rxOffset);
      w.u16(count); w.bytes(s->rx+s->rxOffset,count); s->rxOffset+=count; break;
    }
    default: return c3::Unsupported;
  }
  return w.ok()?c3::Ok:c3::BufferTooSmall;
}
}