/* GPL-2.0-or-later. Source aiCheckRetreat economic arm, empty inventory or canonical Catapult.
 * homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21 ai.cpp/command.cpp.
 */
#pragma once
#include "HoMM3FailedSiege.h"
#include "../../../lib/entities/artifact/CArtifact.h"
#include <limits>
namespace AIIntegration::Ports::HoMM3
{
struct RetreatEconomicTroop
{
 int side=0,count=0,fightValue=0,goldCost=0;
 bool done=false;
};
struct RetreatEconomicResult
{
 int surrenderCost=0;
 std::array<int,2> fightValues{};
 float threshold=0,ratio=0;
 bool retreat=false;
};
inline std::optional<RetreatEconomicResult> sourceBoundedRetreatEconomics(
 const std::vector<RetreatEconomicTroop> & troops,int difficulty,int experience,int64_t gold,int artifactValue=0)
{
 if(difficulty<2 || difficulty>4 || experience<2000 || gold<0 || (artifactValue!=0 && artifactValue!=500))return std::nullopt;
 RetreatEconomicResult result;int64_t cost=0;std::array<int64_t,2> values{};
 for(const auto & troop:troops)
 {
  if(troop.side<0 || troop.side>1 || troop.count<=0 || troop.fightValue<0 || troop.goldCost<0)return std::nullopt;
  const int64_t raw=int64_t(troop.count)*troop.fightValue;
  if(raw>INT32_MAX)return std::nullopt;
  const double weighted=troop.done?double(raw):double(raw)*1.2;
  if(weighted>INT32_MAX)return std::nullopt;
  values[troop.side]+=static_cast<int>(weighted);
  if(values[troop.side]>INT32_MAX)return std::nullopt;
  if(troop.side==0){cost+=int64_t(troop.count)*troop.goldCost;if(cost>INT32_MAX)return std::nullopt;}
 }
 if(values[0]<=0 || values[1]<=0)return std::nullopt;
 const double enemy=double(values[1])*1.1;
 if(enemy>INT32_MAX)return std::nullopt;
 result.fightValues={static_cast<int>(values[0]),static_cast<int>(enemy)};
 if(int64_t(result.fightValues[0])+result.fightValues[1]>INT32_MAX)return std::nullopt;
 result.surrenderCost=static_cast<int>(cost/2);
 result.threshold=0.16f;
 if(artifactValue>0)result.threshold+=0.04f;
 result.threshold-=(4-difficulty)*0.015;
 // Original hero::m_experience is int: division happens BEFORE float conversion.
 float experienceBonus=experience/200000;
 if(experienceBonus>0.03)experienceBonus=0.03f;
 result.threshold+=experienceBonus;result.threshold-=0.06f;
 if(result.threshold>0.16)result.threshold=0.16f;
 result.ratio=float(result.fightValues[0])/float(result.fightValues[0]+result.fightValues[1]);
 result.retreat=gold>=int64_t(result.surrenderCost)+2500 && result.ratio<result.threshold;
 return result;
}
// Called ONLY after a successful admitted source two-pass forecast. No native
// surrender legality is consulted: the original cost exists even vs neutrals.
inline std::optional<BattleAction> boundedEconomicRetreat(
 const CPlayerBattleCallback & battle,const IGameInfoCallback * game)
{
 if(!game || battle.battleGetMySide()!=BattleSide::ATTACKER || battle.battleGetDefendedTown())return std::nullopt;
 const auto * hero=battle.battleGetMyHero();const auto * start=game->getStartInfo();
 if(!hero || !start || hero->exp>INT32_MAX || hero->getSecSkillLevel(SecondarySkill::DIPLOMACY)!=0
  || hero->hasBonusOfType(BonusType::SURRENDER_DISCOUNT))return std::nullopt;
 // Only empty inventory or the canonical Catapult is certified. Other
 // artifact values are never inferred from native retail price.
 if(hero->artifactsWorn.size()>1 || !hero->artifactsInBackpack.empty()
  || hero->artifactsTransitionPos.getArt())return std::nullopt;
 int artifactValue=0;
 for(const auto & [position,slot]:hero->artifactsWorn)
 {
  const auto * artifact=slot.getArt();
  if(position!=ArtifactPosition::MACH4 || slot.locked || !artifact
   || artifact->getTypeId()!=ArtifactID(3) || artifact->getType()->getPrice()!=1000)return std::nullopt;
  // Canonical Catapult has no source artifact effects; per-hero equipment
  // value is zero, player-level value starts at TEN, and the retreat
  // max(value, retailPrice/2) therefore yields max(10,1000/2)=500.
  artifactValue=500;
 }
 const auto * player=game->tryGetPlayerState(hero->getOwner());if(!player)return std::nullopt;
 std::vector<RetreatEconomicTroop> troops;
 for(const auto * unit:battle.battleGetAllStacks(false))
 {
  if(!unit->alive())continue;
  if(unit->isSummoned() || unit->isClone() || unit->getResurrected()!=0
   || unit->controllingSide()!=unit->unitSide() || !unit->unitSlot().validSlot())return std::nullopt;
  const auto value=unit->unitType()->getFightValue();
  if(value<0 || value>INT32_MAX || double(static_cast<int>(value))!=double(value))return std::nullopt;
  troops.push_back({static_cast<int>(unit->unitSide()),unit->getCount(),static_cast<int>(value),
   unit->creatureCost(),unit->moved()||unit->defended()});
 }
 const auto result=sourceBoundedRetreatEconomics(troops,static_cast<int>(start->getDifficulty()),
  static_cast<int>(hero->exp),player->resources[EGameResID::GOLD],artifactValue);
 if(!result || !result->retreat)return std::nullopt;
 return BattleAction::makeRetreat(BattleSide::ATTACKER);
}
}
