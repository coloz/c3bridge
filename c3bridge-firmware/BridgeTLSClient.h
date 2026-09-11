#pragma once
#include <NetworkClientSecure.h>
// Preserve accepted-byte accounting even when a later TLS record fails.
class BridgeTLSClient : public NetworkClientSecure {
 public:
  int connectResolved(const IPAddress& ip,uint16_t port,const char* hostname,uint32_t timeoutMs) {
    _timeout=timeoutMs;
    return NetworkClientSecure::connect(ip,port,hostname,_CA_cert,_cert,_private_key);
  }
  size_t writeBounded(const uint8_t* data,size_t size,uint32_t timeoutMs) {
    if(!data||!size||!connected()) return 0;
    size_t accepted=0;
    uint32_t started=millis();
    while(accepted<size) {
      int ret=mbedtls_ssl_write(&sslclient->ssl_ctx,data+accepted,size-accepted);
      if(ret>0) { accepted+=static_cast<size_t>(ret); continue; }
      if((ret!=MBEDTLS_ERR_SSL_WANT_READ&&ret!=MBEDTLS_ERR_SSL_WANT_WRITE)||
         uint32_t(millis()-started)>=timeoutMs) {
        // Discard any pending record before its source buffer can be reused.
        stop(); return accepted;
      }
      delay(1);
    }
    return accepted;
  }
};