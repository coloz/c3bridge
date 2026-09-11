#include "WiFiArray.h"
#include <cassert>
#include <cstdio>

static bool failAllocation = false;
void* operator new(size_t n, const std::nothrow_t&) noexcept {
  if (failAllocation) return nullptr;
  try { return ::operator new(n); } catch (...) { return nullptr; }
}

struct Item {
  static int live;
  int value = 0;
  Item() { ++live; }
  Item(const Item& other) : value(other.value) { ++live; }
  Item& operator=(const Item& other) { value = other.value; return *this; }
  ~Item() { --live; }
};
int Item::live = 0;

int main() {
  {
    espwifi::Array<Item> a, snapshot;
    assert(a.empty() && a.begin() == a.end());
    Item one; one.value = 42;
    assert(a.push_back(one));
    assert(a.push_back(a[0])); // Aliased growth preserves the source.
    assert(a.size() == 2 && a[1].value == 42);
    assert(snapshot.copyFrom(a));
    a[0].value = 7;
    assert(snapshot[0].value == 42);
    failAllocation = true;
    assert(!a.push_back(one) && a.size() == 2 && a[0].value == 7);
    assert(!snapshot.copyFrom(a) && snapshot[0].value == 42);
    assert(!a.resize(10) && a.size() == 2);
    failAllocation = false;
    assert(a.erase(a.begin()) == a.begin());
    assert(a.size() == 1 && a[0].value == 42);
    assert(a.resize(2) && a[1].value == 0);
    a.swap(snapshot);
    assert(a.size() == 2 && a[0].value == 42);
    a.clear();
    assert(a.empty() && a.begin() == a.end());
    assert(a.copyFrom(a));
    assert(!a.reserve(static_cast<size_t>(-1)));
  }
  assert(Item::live == 0);
  puts("WiFi storage: ownership, aliasing, snapshots, OOM and overflow passed");
}
