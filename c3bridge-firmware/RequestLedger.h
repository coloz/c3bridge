// SPDX-License-Identifier: MIT
#pragma once
#include "C3Protocol.h"
// Production request admission logic; no Arduino/RTOS dependency.
class RequestLedger {
 public:
  enum class Decision { Fresh, Pending, Replay, Expired, Conflict, Busy };
  Decision classify(const c3::Frame& f) const {
    if(!f.id||f.size>c3::MaxPayload) return Decision::Conflict;
    if(f.id==lastId_) {
      if(f.opcode!=opcode_||f.size!=size_||c3::crc32c(f.payload,f.size)!=hash_||
         (f.size&&memcmp(f.payload,payload_,f.size))) return Decision::Conflict;
      if(pending_) return Decision::Pending;
      return complete_?Decision::Replay:Decision::Expired;
    }
    if(f.id<lastId_) return Decision::Expired;
    return pending_?Decision::Busy:Decision::Fresh;
  }
  bool accept(const c3::Frame& f) {
    if(f.size>c3::MaxPayload||classify(f)!=Decision::Fresh) return false;
    lastId_=f.id; opcode_=f.opcode; size_=f.size;
    hash_=c3::crc32c(f.payload,f.size);
    if(f.size) memcpy(payload_,f.payload,f.size);
    pending_=true; complete_=false;
    return true;
  }
  void complete() { if(lastId_) { pending_=false; complete_=true; } }
  void reset() {
    lastId_=hash_=0; opcode_=size_=0; pending_=complete_=false;
    memset(payload_,0,sizeof(payload_));
  }
  bool seenRequest() const { return lastId_!=0; }
  bool mayReplayHello(uint32_t nonce,uint32_t currentNonce) const {
    return nonce&&nonce==currentNonce&&!seenRequest();
  }
 private:
  uint32_t lastId_=0,hash_=0;
  uint16_t opcode_=0,size_=0;
  bool pending_=false,complete_=false;
  uint8_t payload_[c3::MaxPayload]{};
};