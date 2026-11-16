/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "HoMM3Forecast/NativeShooterPrefix.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/IBattleState.h"
#include "../../../lib/json/JsonNode.h"
#include <thread>

namespace AIIntegration::Ports::HoMM3
{
// Scoped synchronous owner-thread evidence. This is not a lock or concurrency proof.
class FreshPrayerContext final
{
 struct Witness
 {
  std::vector<int64_t> fields;
  std::vector<JsonNode> states,bonuses;
  bool operator==(const Witness&)const=default;
 };
 const CPlayerBattleCallback&callback;
 const HoMM3Forecast::NativeLifecycle&lifecycle;
 const std::thread::id owner=std::this_thread::get_id();
 Witness witness;
 HoMM3Forecast::PrefixProjection forecast;
 explicit FreshPrayerContext(const CPlayerBattleCallback&view,const HoMM3Forecast::NativeLifecycle&history):callback(view),lifecycle(history){}
 std::optional<Witness> capture()const
 {
  if(std::this_thread::get_id()!=owner||!lifecycle.boundTo(callback)||!callback.battleActiveUnit())return std::nullopt;
  Witness result;
  result.fields={callback.getBattle()->getBattleID().getNum(),callback.battleGetRound(),static_cast<int>(callback.battleGetMySide()),callback.battleActiveUnit()->unitId(),callback.battleTerrainType().getNum(),callback.battleTacticDist()};
  const auto snapshot=lifecycle.snapshot();result.fields.push_back(static_cast<int>(snapshot.actingSide));result.fields.push_back(snapshot.secondPhase);
  for(const auto*unit:callback.battleGetUnitsIf([](const battle::Unit*){return true;})){
   result.fields.insert(result.fields.end(),{unit->unitId(),static_cast<int>(unit->unitSide()),static_cast<int>(unit->controllingSide()),unit->unitOwner().getNum(),unit->unitSlot().getNum(),unit->getPosition().toInt(),unit->getAvailableHealth(),unit->getMovementRange(),unit->getMaxHealth(),unit->getAttack(false),unit->getAttack(true),unit->getDefense(false),unit->getDefense(true),unit->getMinDamage(false),unit->getMaxDamage(false),unit->getMinDamage(true),unit->getMaxDamage(true),unit->getTotalAttacks(false),unit->getTotalAttacks(true),callback.battleCanShoot(unit)});
   auto copy=unit->acquireState();result.states.push_back(copy->save());
   const auto bonuses=unit->getAllBonuses(Selector::all);result.fields.push_back(bonuses->size());for(const auto&bonus:*bonuses)result.bonuses.push_back(bonus->toJsonNode());
  }
  for(const auto&obstacle:callback.battleGetAllObstacles(callback.battleGetMySide())){
   if(obstacle->obstacleType==CObstacleInstance::SPELL_CREATED||obstacle->obstacleType==CObstacleInstance::MOAT)return std::nullopt;
   result.fields.insert(result.fields.end(),{obstacle->uniqueID,obstacle->ID,obstacle->obstacleType,obstacle->pos.toInt()});
   for(const auto&tiles:{obstacle->getBlockedTiles(),obstacle->getStoppingTile(),obstacle->getAffectedTiles()}){result.fields.push_back(tiles.size());for(const auto hex:tiles)result.fields.push_back(hex.toInt());}
  }
  return result;
 }
public:
 struct OwnerResidual{bool winLikely;int32_t expectedDamage;};
public:
 ~FreshPrayerContext()=default;
 FreshPrayerContext(const FreshPrayerContext&)=delete;
 FreshPrayerContext&operator=(const FreshPrayerContext&)=delete;
private:
 static std::unique_ptr<FreshPrayerContext> build(const CPlayerBattleCallback&view,const HoMM3Forecast::NativeLifecycle&history,bool nonEasy)
 {
  auto result=std::unique_ptr<FreshPrayerContext>(new FreshPrayerContext(view,history));
  try{
   const auto before=result->capture();if(!before)return nullptr;
   result->forecast=HoMM3Forecast::nativeShooterPrefix(view,history,nonEasy);
   if(!result->forecast.outcome)return nullptr;
   const auto after=result->capture();if(!after||*after!=*before)return nullptr;
   result->witness=*before;return result;
  }catch(const HoMM3Forecast::UnsupportedSnapshot&){return nullptr;}
 }
public:
 template<class Consumer>
 static std::optional<int32_t> evaluate(const CPlayerBattleCallback&view,const HoMM3Forecast::NativeLifecycle&history,bool nonEasy,Consumer&&consume)
 {
  const auto context=build(view,history,nonEasy);if(!context)return std::nullopt;
  const auto value=consume(*context);return context->current()?value:std::nullopt;
 }
 bool current()const
 {
  if(std::this_thread::get_id()!=owner)return false;
  try{const auto now=capture();return now&&*now==witness;}catch(const HoMM3Forecast::UnsupportedSnapshot&){return false;}
 }
 std::optional<OwnerResidual> residual(HoMM3Forecast::Side side,HoMM3Forecast::UnitId id)const
 {
  if(!current()||!forecast.outcome)return std::nullopt;
  const auto&out=*forecast.outcome;
  if(out.side!=HoMM3Forecast::sourceSide(callback.battleGetMySide()))return std::nullopt;
  auto selected=out.initial.find(id),unused=out.initial.end();if(selected==unused||selected->second.order.owningSide!=side)return std::nullopt;
  const auto retained=out.health.find(id);if(retained==out.health.end()||retained->second<0||retained->second>selected->second.liveHealth||selected->second.liveHealth>INT32_MAX)return std::nullopt;
  bool win=true;
  for(const auto&[unitID,unit]:out.initial){
   const auto hp=out.health.find(unitID);if(hp==out.health.end()||hp->second<0||hp->second>unit.liveHealth)return std::nullopt;
   const auto facts=out.survivorFacts.find(unitID);if(facts==out.survivorFacts.end())return std::nullopt;
   if(unit.order.owningSide!=side&&!unit.order.immobilized&&!facts->second.siegeWeapon&&hp->second>0)win=false;
  }
  return OwnerResidual{win,static_cast<int32_t>(selected->second.liveHealth-retained->second)};
 }
};
}
