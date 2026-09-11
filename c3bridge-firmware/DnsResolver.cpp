#include "Backend.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
namespace backend {
namespace {
struct DnsJob { char host[254]; uint16_t port; int family, kind; };
struct DnsResult { int status; sockaddr_storage address; socklen_t size; };
QueueHandle_t requests=nullptr, results=nullptr;
bool outstanding=false; // Accessed only by the single service worker.
void dnsWorker(void*) {
  DnsJob job;
  for(;;) {
    if(xQueueReceive(requests,&job,portMAX_DELAY)!=pdTRUE) continue;
    DnsResult reply{}; reply.status=c3::NotFound;
    char service[6]; snprintf(service,sizeof(service),"%u",job.port);
    addrinfo hints{}; hints.ai_family=job.family; hints.ai_socktype=job.kind;
    addrinfo* entries=nullptr;
    if(getaddrinfo(job.host,service,&hints,&entries)==0&&entries) {
      const addrinfo* selected=entries;
      while(selected&&selected->ai_addrlen>sizeof(reply.address)) selected=selected->ai_next;
      if(selected) {
        memcpy(&reply.address,selected->ai_addr,selected->ai_addrlen);
        reply.size=selected->ai_addrlen; reply.status=c3::Ok;
      }
    }
    if(entries) freeaddrinfo(entries);
    // Timed-out callers leave this one owned slot available for the late result.
    xQueueSend(results,&reply,portMAX_DELAY);
  }
}
}
bool beginResolver() {
  requests=xQueueCreate(1,sizeof(DnsJob)); results=xQueueCreate(1,sizeof(DnsResult));
  return requests&&results&&xTaskCreate(dnsWorker,"c3-dns",4096,nullptr,1,nullptr)==pdPASS;
}
int resolveHostname(const char* host,uint16_t port,int family,int kind,sockaddr_storage& address,socklen_t& size) {
  DnsResult reply{};
  if(outstanding) {
    if(xQueueReceive(results,&reply,0)!=pdTRUE) return c3::Busy;
    outstanding=false; // Discard the previous operation's late result.
  }
  DnsJob job{};
  if(strlen(host)>=sizeof(job.host)) return c3::InvalidArgument;
  strcpy(job.host,host); job.port=port; job.family=family; job.kind=kind;
  if(xQueueSend(requests,&job,0)!=pdTRUE) return c3::Busy;
  outstanding=true;
  if(xQueueReceive(results,&reply,pdMS_TO_TICKS(10000))!=pdTRUE) return c3::Timeout;
  outstanding=false;
  if(reply.status==c3::Ok) { address=reply.address; size=reply.size; }
  return reply.status;
}
}