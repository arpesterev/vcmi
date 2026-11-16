// GPL-2.0-or-later. Affinity tripwire, NOT synchronization or transaction safety.
#pragma once
#include <atomic>
#include <thread>
namespace AIIntegration::Ports::HoMM3
{
class CurrentDefendThreadGuard final
{
 // Host keeps the owning network thread alive for the session lifetime;
 // std::thread::id is not a globally unique authority after a thread exits.
 const std::thread::id owner=std::this_thread::get_id();
 mutable std::atomic<bool> violated{false};
public:
 CurrentDefendThreadGuard()=default;
 CurrentDefendThreadGuard(const CurrentDefendThreadGuard &)=delete;
 CurrentDefendThreadGuard & operator=(const CurrentDefendThreadGuard &)=delete;
 CurrentDefendThreadGuard(CurrentDefendThreadGuard &&)=delete;
 CurrentDefendThreadGuard & operator=(CurrentDefendThreadGuard &&)=delete;
 bool check() const
 {
  if(std::this_thread::get_id()!=owner)
  {
   violated.store(true,std::memory_order_release);
   return false;
  }
  return !violated.load(std::memory_order_acquire);
 }
};
}
