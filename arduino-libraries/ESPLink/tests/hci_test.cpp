#include "ESPLinkHCI.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <deque>
#include <vector>

ESPLinkClass ESPLink;
static uint32_t now = 0, currentSession = 1;
static bool online = true, dropped = false, noCompletion = false;
static bool malformedAccept = false, changeOnRead = false;
static uint8_t commandStatus = 0;
static unsigned ends = 0;
static size_t readChunk = 2;
static std::deque<uint8_t> incoming;
static std::vector<std::vector<uint8_t>> writes;
unsigned long millis() { return now; }
void delay(unsigned long ms) { now += static_cast<uint32_t>(ms); }
ESPLinkClass::~ESPLinkClass() {}
bool ESPLinkClass::begin() { online = true; return true; }
bool ESPLinkClass::ready() const { return online; }
uint32_t ESPLinkClass::session() const { return currentSession; }
int32_t ESPLinkClass::lastError() const { return c3::IoError; }
void ESPLinkClass::poll() {}
int32_t ESPLinkClass::request(uint16_t opcode, const uint8_t* data, size_t size,
                            uint8_t* reply, size_t& n, uint32_t) {
  switch (static_cast<c3::Opcode>(opcode)) {
    case c3::Opcode::HciBegin: incoming.clear(); n = 0; return c3::Ok;
    case c3::Opcode::HciEnd: ++ends; n = 0; return c3::Ok;
    case c3::Opcode::HciStatus:
      assert(n >= 12); memset(reply, 0, 12); reply[0] = 1;
      c3::put32(reply + 5, dropped ? 1 : 0); c3::put16(reply + 9, 1024);
      n = 12; return c3::Ok;
    case c3::Opcode::HciWrite:
      writes.emplace_back(data, data + size);
      if (data[0] == 1 && !noCompletion) {
        const uint8_t complete[] = {4, 0x0e, 4, 1, data[1], data[2], commandStatus};
        incoming.insert(incoming.end(), complete, complete + sizeof(complete));
      }
      assert(n >= 2); c3::put16(reply, malformedAccept ? 0 : size); n = 2; return c3::Ok;
    case c3::Opcode::HciRead: {
      assert(size == 2 && c3::get16(data) == n);
      n = std::min(n, std::min(readChunk, incoming.size()));
      for (size_t i = 0; i < n; ++i) { reply[i] = incoming.front(); incoming.pop_front(); }
      if (changeOnRead) ++currentSession;
      return c3::Ok;
    }
    default: return c3::Unsupported;
  }
}

int main() {
  ESPLinkHCI hci;
  assert(hci.begin());
  assert(writes[0] == std::vector<uint8_t>({1, 3, 12, 0}));
  const uint8_t adv[] = {2, 1, 6, 4, 9, 'S', 'T', 'C'};
  assert(hci.advertise(adv, sizeof(adv)));
  assert(writes.size() == 5);
  assert(writes[1] == std::vector<uint8_t>({1, 10, 32, 1, 0}));
  assert(writes[2] == std::vector<uint8_t>({1, 6, 32, 15, 0x40, 6, 0x40, 6, 3, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0}));
  assert(writes[3][3] == 32 && writes[3][4] == sizeof(adv));
  assert(memcmp(writes[3].data() + 5, adv, sizeof(adv)) == 0);
  assert(writes[4] == std::vector<uint8_t>({1, 10, 32, 1, 1}));
  assert(hci.stopAdvertise());
  // Discard a different opcode's completion; accept only the requested one.
  const uint8_t old[] = {4, 14, 4, 1, 0x55, 0, 0};
  incoming.insert(incoming.end(), old, old + sizeof(old));
  assert(hci.command(0x0c03));
  // Raw access preserves stream chunks, including coalesced packets.
  incoming = {4, 1, 0, 4, 2, 0}; readChunk = 1024;
  uint8_t raw[16]; assert(hci.read(raw, sizeof(raw)) == 6);
  assert(raw[3] == 4 && hci.read(raw, sizeof(raw)) == 0);
  commandStatus = 0x0c;
  assert(!hci.command(0x200a) && hci.lastError() == -268);
  commandStatus = 0; assert(hci.begin());
  noCompletion = true; now = 0xfffffff8UL;
  assert(!hci.command(0x0c03, nullptr, 0, 20));
  assert(hci.lastError() == c3::Timeout && now == 12);
  assert(!hci.healthy());
  noCompletion = false; assert(hci.begin());
  changeOnRead = true;
  assert(hci.read(raw, sizeof(raw)) == -1 && hci.lastError() == c3::LinkLost);
  const unsigned previousEnds = ends; hci.end(); assert(ends == previousEnds);
  changeOnRead = false; assert(hci.begin());
  dropped = true; now += 1000;
  assert(!hci.healthy() && hci.lastError() == c3::IoError);
  dropped = false; assert(hci.begin());
  const uint8_t invalid[] = {1, 0, 0, 2, 0};
  assert(!hci.writePacket(invalid, sizeof(invalid)) && hci.lastError() == c3::InvalidArgument);
  assert(hci.begin()); malformedAccept = true;
  assert(!hci.command(0x0c03) && hci.lastError() == c3::ProtocolError);
  malformedAccept = false; assert(hci.begin());
  noCompletion = true; incoming = {4, 14, 1, 0};
  assert(!hci.command(0x0c03) && hci.lastError() == c3::ProtocolError);
  hci.end(); assert(!hci.healthy());
  puts("ESPLink HCI: framing, byte order, fragments, beacon, errors, deadlines and sessions passed");
}
