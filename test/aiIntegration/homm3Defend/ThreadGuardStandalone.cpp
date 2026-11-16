// GPL-2.0-or-later. Standalone affinity smoke, not a race detector.
#include "../../../AI/Integration/Ports/HoMM3CurrentDefendThreadGuard.h"
#include <memory>
#include <stdexcept>
#include <iostream>
using AIIntegration::Ports::HoMM3::CurrentDefendThreadGuard;
#define CHECK(x) do {if(!(x)) throw std::runtime_error(#x);} while(false)
int main()
{
 for(int i=0;i<100;++i)
 {
  auto guard=std::make_shared<const CurrentDefendThreadGuard>();
  CHECK(guard->check());CHECK(guard->check());
  bool accepted=true;
  std::thread other([&]{accepted=guard->check();});other.join();
  CHECK(!accepted);CHECK(!guard->check());CHECK(!guard->check());
  auto copy=guard;CHECK(!copy->check());
 }
 std::cout<<"100 affinity/revocation iterations passed\n";
}
