// GPL-2.0-or-later. Observed current scalar only; NOT a forecast/no-proc certificate.
#pragma once
#include "HoMM3NativeDefendFacts.h"
#include "HoMM3CurrentDefendThreadGuard.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/battle/IBattleState.h"

namespace AIIntegration::Ports::HoMM3
{
class CurrentDefendSession;
class CurrentDefendSnapshot
{
 friend class CurrentDefendSession;
 const std::shared_ptr<const CurrentDefendThreadGuard> affinity;
 DefendUnitFacts value;
 BattleID battleId;
 int battleRound;
 TerrainId terrain;
 std::weak_ptr<const int> lifetime;
 explicit CurrentDefendSnapshot(DefendUnitFacts f,BattleID id,int round,TerrainId terrainId,std::weak_ptr<const int> token,std::shared_ptr<const CurrentDefendThreadGuard> guard) : affinity(std::move(guard)),value(std::move(f)),battleId(id),battleRound(round),terrain(terrainId),lifetime(std::move(token)) {}
public:
 bool matchesContext(const CBattleInfoCallback & view) const
 {
  if(!affinity->check()) return false;
  const bool matches=!lifetime.expired() && view.getBattle() && view.getBattle()->getBattleID()==battleId && view.battleGetRound()==battleRound && view.battleTerrainType()==terrain;
  return affinity->check() && matches;
 }
 // Immutable matching only. A hypothetical mismatch cannot revoke live evidence.
 std::optional<int32_t> bonus(const CBattleInfoCallback & view,const battle::Unit & u) const
 {
  if(!affinity->check() || !matchesContext(view)) return std::nullopt;
  const auto f=NativeDefendFacts::extract(view,u,value.identity);
  if(!f || f->id!=value.id || f->defenseProvenance!=value.defenseProvenance
   || f->stance!=value.stance || f->underlyingDefense!=value.underlyingDefense
   || f->displayedDefense!=value.displayedDefense) return std::nullopt;
  if(!affinity->check()) return std::nullopt;
  return static_cast<int32_t>(value.displayedDefense-value.underlyingDefense);
 }
};

/// Bound once to controller's authoritative callback. No caller-supplied preview
/// is accepted by capture(). Existing stance on attach never creates evidence.
class CurrentDefendSession
{
 // Immutable shared authority; foreign calls may ONLY trip its atomic flag.
 // Host still serializes object lifetimes and authoritative state access. This
 // does not make the controller's forecasts map or GUI autofight thread-safe.
 const std::shared_ptr<const CurrentDefendThreadGuard> affinity=std::make_shared<const CurrentDefendThreadGuard>();
 std::shared_ptr<CBattleCallback> callback;
 BattleID id;
 std::optional<DefendUnitFacts> pending, completed;
 BattleSide side=BattleSide::NONE;
 int round=-1;
 TerrainId observedTerrain;
 bool effectSeen=false;
 uint64_t generation=0;
 std::shared_ptr<const int> lifetime;
 bool same(const DefendUnitFacts & a,const DefendUnitFacts & b) const
 {
  return a.id==b.id && a.identity==b.identity && a.defenseProvenance==b.defenseProvenance
   && a.underlyingDefense==b.underlyingDefense;
 }
 std::shared_ptr<CPlayerBattleCallback> real() const { return callback?callback->getBattle(id):nullptr; }
public:
 CurrentDefendSession(std::shared_ptr<CBattleCallback> cb,BattleID battleId)
  : callback(std::move(cb)),id(battleId) {}
 CurrentDefendSession(const CurrentDefendSession &)=delete;
 CurrentDefendSession & operator=(const CurrentDefendSession &)=delete;
 CurrentDefendSession(CurrentDefendSession &&)=delete;
 CurrentDefendSession & operator=(CurrentDefendSession &&)=delete;
 void invalidate()
 {
  if(!affinity->check()) return;
  pending.reset();completed.reset();effectSeen=false;lifetime.reset();
 }
 void start(const BattleAction & action)
 {
  if(!affinity->check()) return;
  const bool nested=pending.has_value();
  invalidate();
  const auto view=real();
  if(nested || !view || action.actionType!=EActionType::DEFEND
   || generation==std::numeric_limits<uint64_t>::max()) return;
  const auto * u=view->battleGetUnitByID(action.stackNumber);
  if(!u || action.side!=u->unitSide()) return;
  const auto f=NativeDefendFacts::extract(*view,*u,++generation);
  if(!f || f->stance!=DefendUnitFacts::Stance::Absent) return;
  pending=f; side=action.side;round=view->battleGetRound();observedTerrain=view->battleTerrainType();
 }
 void effects(const SetStackEffect & packet)
 {
  if(!affinity->check()) return;
  // Standard ActionEnvironment::updateUnitBonuses uses toUpdate (upsert),
  // not toAdd. The observed absent-stance prestate prevents replacement ambiguity.
  if(!pending || effectSeen || packet.battleID!=id || !packet.toAdd.empty()
   || !packet.toRemove.empty() || packet.toUpdate.size()!=1
   || packet.toUpdate.front().first!=pending->id || packet.toUpdate.front().second.size()!=3)
  {invalidate();return;}
  int tag=0,zero=0,boost=0;
  for(const auto & b:packet.toUpdate.front().second)
  {
   if(!NativeDefendFacts::stanceRecord(b)) {invalidate();return;}
   if(b.type==BonusType::UNIT_DEFENDING && b.subtype==BonusSubtypeID()
    && b.valType==BonusValueType::ADDITIVE_VALUE && b.val==0) ++tag;
   else if(b.type==BonusType::PRIMARY_SKILL && b.subtype==BonusSubtypeID(PrimarySkill::DEFENSE))
   {
    if(b.valType==BonusValueType::ADDITIVE_VALUE && b.val==0) ++zero;
    else if(pending->underlyingDefense<5 && b.valType==BonusValueType::ADDITIVE_VALUE && b.val==1) ++boost;
    else if(pending->underlyingDefense>=5 && b.valType==BonusValueType::PERCENT_TO_ALL && b.val==20) ++boost;
    else {invalidate();return;}
   }
   else {invalidate();return;}
  }
  if(tag!=1 || zero!=1 || boost!=1) {invalidate();return;}
  const auto view=real();
  const auto * u=view?view->battleGetUnitByID(pending->id):nullptr;
  const auto f=u?NativeDefendFacts::extract(*view,*u,generation):std::nullopt;
  if(!f || !same(*pending,*f) || f->stance==DefendUnitFacts::Stance::Absent
   || view->battleGetRound()!=round || view->battleTerrainType()!=observedTerrain) {invalidate();return;}
  effectSeen=true;
 }
 void finish(const BattleAction & action)
 {
  if(!affinity->check()) return;
  if(!pending || !effectSeen || action.actionType!=EActionType::DEFEND
   || action.stackNumber!=pending->id || action.side!=side) {invalidate();return;}
  const auto view=real();
  const auto * u=view?view->battleGetUnitByID(pending->id):nullptr;
  const auto f=u?NativeDefendFacts::extract(*view,*u,generation):std::nullopt;
  if(!f || !same(*pending,*f) || f->stance==DefendUnitFacts::Stance::Absent
   || view->battleGetRound()!=round || view->battleTerrainType()!=observedTerrain) {invalidate();return;}
  completed=f;pending.reset();effectSeen=false;lifetime=std::make_shared<const int>(0);
 }
 std::optional<CurrentDefendSnapshot> capture()
 {
  if(!affinity->check()) return std::nullopt;
  if(pending || !completed) return std::nullopt;
  const auto view=real();
  const auto * u=view?view->battleGetUnitByID(completed->id):nullptr;
  const auto f=u?NativeDefendFacts::extract(*view,*u,generation):std::nullopt;
  if(!f || !same(*completed,*f) || f->stance!=completed->stance
   || f->displayedDefense!=completed->displayedDefense || view->battleGetRound()!=round || view->battleTerrainType()!=observedTerrain)
  {invalidate();return std::nullopt;}
  if(!affinity->check()) return std::nullopt;
  return CurrentDefendSnapshot(*f,id,round,observedTerrain,lifetime,affinity);
 }
};
}
