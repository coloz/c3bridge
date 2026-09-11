#include "Backend.h"
#include <new>
namespace backend {
int security(c3::Opcode op,c3::Reader& r,c3::Writer& w) {
  switch(op) {
    case c3::Opcode::CertBegin: {
      uint8_t kind=r.u8(); uint32_t total=r.u32();
      if(!r.done()||kind>2||!total||total>MaxCertBytes) return c3::InvalidArgument;
      size_t used=0; for(auto& c:certificates) used+=c.total;
      if(used+total>MaxCertTotal) return c3::NoMemory;
      for(unsigned i=0;i<MaxCertificates;++i) {
        Certificate& c=certificates[i]; if(c.handle) continue;
        c.bytes=static_cast<uint8_t*>(malloc(total+1)); if(!c.bytes) return c3::NoMemory;
        c.handle=newHandle(i); c.kind=static_cast<c3::CertKind>(kind); c.total=total;
        c.written=0; c.committed=false; c.refs=0; c.bytes[total]=0;
        w.u32(c.handle); return c3::Ok;
      }
      return c3::NoMemory;
    }
    case c3::Opcode::CertWrite: {
      Certificate* c=certificate(r.u32()); uint32_t offset=r.u32(); size_t size=r.remaining();
      if(!r.ok()) return c3::InvalidArgument;
      if(!c) return c3::StaleHandle;
      if(c->committed||offset!=c->written||size>c->total-c->written) return c3::InvalidArgument;
      memcpy(c->bytes+offset,r.current(),size); c->written+=size; break;
    }
    case c3::Opcode::CertCommit: {
      Certificate* c=certificate(r.u32()); if(!r.done()) return c3::InvalidArgument;
      if(!c) return c3::StaleHandle;
      if(c->written!=c->total||memchr(c->bytes,0,c->total)) return c3::InvalidArgument;
      c->bytes[c->total]=0; c->committed=true; break;
    }
    case c3::Opcode::CertDelete: {
      Certificate* c=certificate(r.u32()); if(!r.done()) return c3::InvalidArgument;
      if(!c) return c3::StaleHandle;
      if(c->refs) return c3::Busy;
      clearCertificate(*c); break;
    }
    case c3::Opcode::SocketTLS: {
      Slot* s=slot(r.u32()); uint32_t caId=r.u32(),certId=r.u32(),keyId=r.u32();
      uint8_t insecure=r.u8(); uint32_t timeout=r.u32();
      if(!r.done()||insecure>1||!timeout||timeout>MaxOperationTimeout||bool(certId)!=bool(keyId)) return c3::InvalidArgument;
      if(!s) return c3::StaleHandle;
      if(s->kind!=c3::SocketKind::TLS) return c3::InvalidArgument;
      if(s->client->connected()) return c3::Busy;
      Certificate* ca=caId?certificate(caId):nullptr;
      Certificate* cert=certId?certificate(certId):nullptr;
      Certificate* key=keyId?certificate(keyId):nullptr;
      if((caId&&(!ca||!ca->committed||ca->kind!=c3::CertKind::CA))||
         (certId&&(!cert||!cert->committed||cert->kind!=c3::CertKind::Certificate))||
         (keyId&&(!key||!key->committed||key->kind!=c3::CertKind::PrivateKey))) return c3::StaleHandle;
      auto* tls=new(std::nothrow) BridgeTLSClient(); if(!tls) return c3::NoMemory;
      if(insecure) tls->setInsecure();
      else if(ca) tls->setCACert(reinterpret_cast<const char*>(ca->bytes));
      else builtinCA(*tls);
      if(cert) tls->setCertificate(reinterpret_cast<const char*>(cert->bytes));
      if(key) tls->setPrivateKey(reinterpret_cast<const char*>(key->bytes));
      tls->setHandshakeTimeout((timeout+999)/1000); tls->setConnectionTimeout(s->timeout);
      delete s->client;
      dropReference(s->ca); dropReference(s->cert); dropReference(s->key);
      s->client=tls; s->ca=caId; s->cert=certId; s->key=keyId;
      if(ca) ++ca->refs;
      if(cert) ++cert->refs;
      if(key) ++key->refs;
      break;
    }
    default: return c3::Unsupported;
  }
  return w.ok()?c3::Ok:c3::BufferTooSmall;
}
}