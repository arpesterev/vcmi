/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "NativeOrdinaryMixed.h"
#include "../../../../lib/callback/IGameInfoCallback.h"
#include "../../../../lib/CPlayerState.h"
#include "../../../../lib/mapObjects/CGTownInstance.h"
#include "../HoMM3NativeDefendFacts.h"
#include "../../../../lib/networkPacks/PacksForClientBattle.h"

namespace HoMM3Forecast
{
inline std::optional<std::vector<OrderUnit>> nativeHistoryRoster(const CPlayerBattleCallback & callback)
{
 auto native=callback.battleGetUnitsIf([](const battle::Unit *){return true;});
 std::sort(native.begin(),native.end(),[](const auto *a,const auto *b){return a->unitSide()!=b->unitSide()?a->unitSide()<b->unitSide():a->unitSlot()<b->unitSlot();});
 std::array<int,2> next{},last{-1,-1};std::vector<OrderUnit> result;
 for(const auto * unit:native)
 {
  if(!unit->unitSlot().validSlot() || unit->isGhost() || unit->isClone() || unit->isSummoned() || unit->isTurret()
   || unit->hasBonusOfType(BonusType::SIEGE_WEAPON) || unit->controllingSide()!=unit->unitSide()
   || (unit->unitSide()!=BattleSide::ATTACKER && unit->unitSide()!=BattleSide::DEFENDER)) return std::nullopt;
  const int side=static_cast<int>(unit->unitSide());
  if(last[side]==unit->unitSlot().getNum())return std::nullopt;
  last[side]=unit->unitSlot().getNum();OrderUnit row;
  row.id=unit->unitId();row.owningSide=static_cast<Side>(side);row.sourceIndex=next[side]++;row.immobilized=!unit->alive();
  result.push_back(row);
 }
 return result;
}
inline bool sameNativeAction(const BattleAction & a,const BattleAction & b)
{
 if(a.side!=b.side||a.stackNumber!=b.stackNumber||a.actionType!=b.actionType||a.spell!=b.spell||a.target.size()!=b.target.size())return false;
 for(size_t i=0;i<a.target.size();++i)if(a.target[i].unitValue!=b.target[i].unitValue||a.target[i].hexValue!=b.target[i].hexValue)return false;
 return true;
}
class NativeMixedHistory
{
 const CPlayerBattleCallback & callback;
 OrdinarySourceHistory state;
 std::optional<BattleAction> staged,pending;
 std::optional<UnitId> target;
 std::optional<BattleStackAttacked> announced;
 unsigned attacks=0,counters=0;
 bool moved=false,announcedRanged=false;
 static std::optional<OrdinarySourceHistory::CompletedAction> kind(EActionType action)
 {
  using A=OrdinarySourceHistory::CompletedAction;
  switch(action){case EActionType::WALK:return A::MOVE;case EActionType::WALK_AND_ATTACK:return A::MELEE;
   case EActionType::SHOOT:return A::SHOOT;case EActionType::WAIT:return A::WAIT;case EActionType::DEFEND:return A::DEFEND;default:return std::nullopt;}
 }
public:
 explicit NativeMixedHistory(const CPlayerBattleCallback & value):callback(value){}
 void invalidate(){state.invalidate();staged.reset();pending.reset();target.reset();announced.reset();}
 bool fresh(bool certified)
 {
  invalidate();if(!certified||callback.battleGetRound()!=0)return false;
  try{const NativeShooterModel ordinary(callback);(void)ordinary;}catch(const UnsupportedSnapshot &){return false;}
  for(const auto * unit:callback.battleGetUnitsIf([](const battle::Unit *){return true;}))
   if(!AIIntegration::Ports::HoMM3::NativeExchangeFacts::ordinaryMelee(*unit))return false;
  const auto roster=nativeHistoryRoster(callback);return roster&&state.beginFresh(*roster);
 }
 bool ready()const{return state.known()&&!pending&&!announced;}
 const OrdinarySourceHistory & source()const{return state;}
 bool boundTo(const CPlayerBattleCallback & value)const{return &callback==&value;}
 void beforeOwnChoice(const IGameInfoCallback * game = nullptr)
 {
  if(!ready())return;
  // Original hero pre-AI retreat/spell valuation can write order before the live leaf.
  if(callback.battleGetHeroInfo(callback.battleGetMySide()).details)
  {
   const auto * hero=callback.battleGetMyHero();
   const auto * player=hero && game ? game->tryGetPlayerState(hero->getOwner()) : nullptr;
   // aiCheckRetreat reaches simulateCombat only after finding an owned tavern.
   // Re-evaluate public own-town state each activation; acquiring/building a
   // tavern revokes this certificate immediately. Unknown ownership fails closed.
   bool retreatCannotSimulate=player!=nullptr;
   if(player)for(const auto * town:player->getTowns())
    if(!town || town->getOwner()!=hero->getOwner() || town->hasBuilt(BuildingID::TAVERN))
     retreatCannotSimulate=false;
   // Spell valuation is independently excluded, including native no-book grants.
   if(!hero || hero->getOwner()!=callback.sideToPlayer(callback.battleGetMySide())
    || hero->hasSpellbook() || hero->canCastSpells() || !retreatCannotSimulate)
    state.unknownOrderEvent();
  }
  staged.reset();
 }
 bool stage(const OrdinarySourceHistory & candidate,const BattleAction & action)
 {
  if(!ready()||!callback.battleActiveUnit()||callback.battleActiveUnit()->unitId()!=action.stackNumber)return false;
  state=candidate;staged=action;return true;
 }
 void start(const BattleAction & action)
 {
  if(callback.battleTacticDist()>0 || action.actionType==EActionType::END_TACTIC_PHASE)return;
  if(!state.known())return;
  if(pending||!kind(action.actionType)||!callback.battleActiveUnit()||callback.battleActiveUnit()->unitId()!=action.stackNumber){invalidate();return;}
  const auto roster=nativeHistoryRoster(callback);
  if(!roster){invalidate();return;}
  for(const auto & unit:*roster){const auto * known=state.find(unit.id);if(!known||known->immobilized!=unit.immobilized||known->side!=unit.owningSide||known->sourceIndex!=unit.sourceIndex){invalidate();return;}}
  // No certificate identifies the opposing planner or a replaced local decision.
  // Possible order writes may change living ranks, never already-dead ranks.
  if(!staged||!sameNativeAction(*staged,action))state.unknownOrderEvent();
  staged.reset();pending=action;attacks=counters=0;moved=false;announced.reset();target.reset();
  if(action.actionType==EActionType::SHOOT||action.actionType==EActionType::WALK_AND_ATTACK)
  {
   const auto targets=action.getTarget(&callback);if(targets.empty()){invalidate();return;}
   const auto & destination=targets.back();const auto * unit=destination.unitValue?destination.unitValue:callback.battleGetUnitByPos(destination.hexValue,false);
   if(!unit||!unit->alive()){invalidate();return;}target=unit->unitId();
  }
 }
 void attack(const BattleAttack & attack)
 {
  if(!state.known())return;
  if(!pending||announced||!target||attack.bsa.size()!=1||(attack.flags&~(BattleAttack::SHOT|BattleAttack::COUNTER))){invalidate();return;}
  const auto & damage=attack.bsa.front();
  if(damage.flags&~BattleStackAttacked::KILLED){invalidate();return;}
  if(attack.counter())
  {
   if(pending->actionType!=EActionType::WALK_AND_ATTACK||attack.shot()||!attacks||++counters>1||attack.stackAttacking!=*target||damage.stackAttacked!=pending->stackNumber){invalidate();return;}
  }
  else if(++attacks>1||attack.stackAttacking!=pending->stackNumber||damage.stackAttacked!=*target
   ||attack.shot()!=(pending->actionType==EActionType::SHOOT)){invalidate();return;}
  if(damage.attackerID!=attack.stackAttacking){invalidate();return;}
  announced=damage;announcedRanged=attack.shot();
 }
 void damage(const std::vector<BattleStackAttacked> & damage,bool ranged)
 {
  if(!state.known())return;
  if(!pending||!announced||damage.size()!=1||ranged!=announcedRanged
   ||damage.front().stackAttacked!=announced->stackAttacked||damage.front().attackerID!=announced->attackerID
   ||damage.front().flags!=announced->flags||damage.front().damageAmount!=announced->damageAmount){invalidate();return;}
  announced.reset();
 }
 void movement(const battle::Unit * unit,const BattleHexArray & path,bool teleport)
 {
  if(!state.known())return;
  if(!pending||!unit||unit->unitId()!=pending->stackNumber||teleport
   ||(pending->actionType!=EActionType::WALK&&pending->actionType!=EActionType::WALK_AND_ATTACK)){invalidate();return;}
  auto last=unit->getPosition();
  for(const auto hex:path){if(hex==last)continue;if(!hex.isAvailable()||BattleHex::mutualPosition(last,hex)==BattleHex::NONE){invalidate();return;}last=hex;moved=true;}
 }
 void finish(const BattleAction & action)
 {
  if(callback.battleTacticDist()>0 || action.actionType==EActionType::END_TACTIC_PHASE)return;
  if(!state.known())return;
  if(!pending||!sameNativeAction(*pending,action)||announced
   ||((action.actionType==EActionType::SHOOT||action.actionType==EActionType::WALK_AND_ATTACK)&&attacks!=1)
   ||(action.actionType==EActionType::WALK&&!moved)){invalidate();return;}
  const auto roster=nativeHistoryRoster(callback);const auto actionKind=kind(action.actionType);
  if(!roster||!actionKind||!state.completeOrdinary(*actionKind,*roster)){invalidate();return;}
  pending.reset();target.reset();staged.reset();
 }
 void effects(const SetStackEffect & effect)
 {
  if(!state.known())return;
  if(!pending||pending->actionType!=EActionType::DEFEND||!effect.toAdd.empty()||!effect.toRemove.empty()
   ||effect.toUpdate.size()!=1||effect.toUpdate.front().first!=pending->stackNumber||effect.toUpdate.front().second.size()!=3){invalidate();return;}
  int tag=0,zero=0,boost=0;
  for(const auto & bonus:effect.toUpdate.front().second)
  {
   if(!AIIntegration::Ports::HoMM3::NativeDefendFacts::stanceRecord(bonus)){invalidate();return;}
   if(bonus.type==BonusType::UNIT_DEFENDING&&bonus.subtype==BonusSubtypeID()&&bonus.valType==BonusValueType::ADDITIVE_VALUE&&bonus.val==0)++tag;
   else if(bonus.type==BonusType::PRIMARY_SKILL&&bonus.subtype==BonusSubtypeID(PrimarySkill::DEFENSE))
   {
    if(bonus.valType==BonusValueType::ADDITIVE_VALUE&&bonus.val==0)++zero;
    else if((bonus.valType==BonusValueType::ADDITIVE_VALUE&&bonus.val==1)||(bonus.valType==BonusValueType::PERCENT_TO_ALL&&bonus.val==20))++boost;
    else {invalidate();return;}
   }
   else {invalidate();return;}
  }
  if(tag!=1||zero!=1||boost!=1)invalidate();
 }
 void newRound(){if(pending||announced)invalidate();}
};
}
