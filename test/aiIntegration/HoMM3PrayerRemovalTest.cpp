/* GPL-2.0-or-later. Direct Prayer composition on native battle projections. */
#include "StdInc.h"
#include <chrono>
#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/HoMM3SpellPolicy.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../server/CGameHandler.h"
#include <vcmi/Environment.h>

namespace
{
class PrayerRemovalEnvironment final : public Environment
{
 std::shared_ptr<CCallback> gameCallback;
 std::shared_ptr<CBattleCallback> battleCallback;
public:
 PrayerRemovalEnvironment(std::shared_ptr<CCallback> game, std::shared_ptr<CBattleCallback> battle)
  : gameCallback(std::move(game)), battleCallback(std::move(battle)) {}
 const Services * services() const override { return LIBRARY; }
 const GameCb * game() const override { return gameCallback.get(); }
 const BattleCb * battle(const BattleID & id) const override { return battleCallback->getBattle(id).get(); }
};
}

class HoMM3PrayerRemovalNative : public BattleTestFixture
{
protected:
 CStack * ally = nullptr;
 CStack * enemy = nullptr;
 std::shared_ptr<CBattleCallback> callback;
 std::shared_ptr<CPlayerBattleCallback> view;
 std::shared_ptr<PrayerRemovalEnvironment> environment;
 std::unique_ptr<HoMM3Forecast::NativeLifecycle> lifecycle;
 void prepare(int mastery = MasteryLevel::BASIC, bool shooters = false, bool extraAlly = false, int rosterSlots = 1)
 {
  startGame(); ASSERT_FALSE(HasFatalFailure());
  const auto creature = creatureByName(shooters ? "core:archer" : "core:pikeman");
  for(auto * hero : {attackerSideHero,defenderSideHero})
  {
   const StackLocation slot(hero->id,SlotID(0));
   ASSERT_TRUE(gameHandler->changeStackType(slot,creature.toCreature()));
   ASSERT_TRUE(gameHandler->changeStackCount(slot,100,ChangeValueMode::ABSOLUTE));
   for(const auto & spell:LIBRARY->spellh->objects) hero->removeSpellFromSpellbook(spell->id);
  }
  for(auto *hero:{attackerSideHero,defenderSideHero})for(int slot=1;slot<rosterSlots;++slot)
   { ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(hero->id,SlotID(slot)),creature.toCreature(),100)); }
  if(extraAlly) { ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id,SlotID(1)),creature.toCreature(),100)); }
  startBattle(); ASSERT_FALSE(HasFatalFailure());
  for(const auto & unit:battle()->stacks)
   if(unit->unitSide()==BattleSide::ATTACKER) { if(!ally) ally=unit.get(); else unit->setPosition(BattleHex(110)); } else { if(!enemy) enemy=unit.get(); }
  ASSERT_NE(ally,nullptr); ASSERT_NE(enemy,nullptr);
  if(rosterSlots==1){ally->setPosition(BattleHex(92)); enemy->setPosition(BattleHex(shooters ? 100 : 93));}
  else for(const auto&unit:battle()->stacks)unit->setPosition(BattleHex((unit->unitSlot().getNum()+2)*17+(unit->unitSide()==BattleSide::ATTACKER?3:13)));
  teachSpell(attackerSideHero,SpellID(SpellID::PRAYER));
  attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,6,ChangeValueMode::ABSOLUTE);
  setMagicMastery(attackerSideHero,mastery);
  callback=std::make_shared<CBattleCallback>(attackerSideHero->getOwner(),nullptr);
  callback->onBattleStarted(battle()); view=callback->getBattle(battle()->getBattleID());
  environment=std::make_shared<PrayerRemovalEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
  lifecycle=std::make_unique<HoMM3Forecast::NativeLifecycle>(*view);lifecycle->attach(battle()->getBattleID());
  beginCombat();ASSERT_FALSE(HasFatalFailure());lifecycle->battleNewRound(battle()->getBattleID());
  battle()->activeStack=ally->unitId();lifecycle->ownActivation(battle()->getBattleID());
 }
 std::optional<BattleAction> choose(const HoMM3Forecast::PrefixProjection & forecast={})
 {
  const auto mana=attackerSideHero->mana;
  const auto health=ally->getAvailableHealth();
  const auto attack=ally->getAttack(false), defense=ally->getDefense(false);
  const auto speed=ally->getMovementRange();
  const auto casts=server.casts.size();
  auto selected=AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*ally,forecast,false,nullptr,lifecycle.get());
  EXPECT_EQ(attackerSideHero->mana,mana); EXPECT_EQ(ally->getAvailableHealth(),health);
  EXPECT_EQ(ally->getAttack(false),attack); EXPECT_EQ(ally->getDefense(false),defense);
  EXPECT_EQ(ally->getMovementRange(),speed); EXPECT_EQ(server.casts.size(),casts);
  return selected;
 }
};


TEST_F(HoMM3PrayerRemovalNative, EnemyPrayerDispelIsChosenAndServerRemovesAllComponents)
{
 prepare(MasteryLevel::ADVANCED,true);ASSERT_FALSE(HasFatalFailure());
 attackerSideHero->removeSpellFromSpellbook(SpellID(SpellID::PRAYER));teachSpell(attackerSideHero,SpellID(SpellID::DISPEL));teachSpell(defenderSideHero,SpellID(SpellID::PRAYER));
 ASSERT_TRUE(castOn(defenderSideHero,SpellID(SpellID::PRAYER),enemy));
 const auto prefix=HoMM3Forecast::nativeShooterPrefix(*view,*lifecycle,true);ASSERT_TRUE(prefix.outcome)<<prefix.unavailable;
 const auto selected=choose();ASSERT_TRUE(selected);EXPECT_EQ(selected->spell,SpellID::DISPEL);
 ASSERT_TRUE(castAsHero(attackerSideHero,selected->spell,selected->getTarget(view.get())));
 EXPECT_FALSE(enemy->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER)))));
}
TEST_F(HoMM3PrayerRemovalNative, MissingLifecycleCannotEnableRemovalFromCallerForecast)
{
 prepare(MasteryLevel::ADVANCED,true);ASSERT_FALSE(HasFatalFailure());attackerSideHero->removeSpellFromSpellbook(SpellID(SpellID::PRAYER));teachSpell(attackerSideHero,SpellID(SpellID::DISPEL));teachSpell(defenderSideHero,SpellID(SpellID::PRAYER));ASSERT_TRUE(castOn(defenderSideHero,SpellID(SpellID::PRAYER),enemy));
 const auto validButUnbound=HoMM3Forecast::nativeShooterPrefix(*view,*lifecycle,true);ASSERT_TRUE(validButUnbound.outcome)<<validButUnbound.unavailable;
 EXPECT_FALSE(AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*ally,validButUnbound));
}
TEST_F(HoMM3PrayerRemovalNative, ScopedFreshContextRejectsPositionMutationAndWrongThread)
{
 prepare(MasteryLevel::ADVANCED,true);ASSERT_FALSE(HasFatalFailure());
 enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,1,BonusSourceID()));
 using Context=AIIntegration::Ports::HoMM3::FreshPrayerContext;
 const auto position=enemy->getPosition();
 const auto stale=Context::evaluate(*view,*lifecycle,true,[&](const Context&ctx)->std::optional<int32_t>{EXPECT_TRUE(ctx.current());enemy->setPosition(BattleHex(101));EXPECT_FALSE(ctx.current());return 42;});
 EXPECT_FALSE(stale);enemy->setPosition(position);
 const auto good=Context::evaluate(*view,*lifecycle,true,[&](const Context&ctx)->std::optional<int32_t>{bool other=true;std::thread thread([&]{other=ctx.current();});thread.join();EXPECT_FALSE(other);return 42;});EXPECT_EQ(good,std::optional<int32_t>(42));
}
TEST_F(HoMM3PrayerRemovalNative, FriendlyPrayerHasNegativeRemovalValue)
{
 prepare(MasteryLevel::ADVANCED,true);ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(castOn(attackerSideHero,SpellID(SpellID::PRAYER),ally));
 battle()->getSide(BattleSide::ATTACKER).castSpellsCount=0;attackerSideHero->removeSpellFromSpellbook(SpellID(SpellID::PRAYER));teachSpell(attackerSideHero,SpellID(SpellID::DISPEL));
 EXPECT_FALSE(choose());EXPECT_TRUE(ally->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER)))));
}
TEST_F(HoMM3PrayerRemovalNative, CureDoesNotRemovePositivePrayer)
{
 prepare(MasteryLevel::ADVANCED,true);ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(castOn(attackerSideHero,SpellID(SpellID::PRAYER),ally));
 battle()->getSide(BattleSide::ATTACKER).castSpellsCount=0;attackerSideHero->removeSpellFromSpellbook(SpellID(SpellID::PRAYER));teachSpell(attackerSideHero,SpellID(SpellID::CURE));
 EXPECT_FALSE(choose());EXPECT_TRUE(ally->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER)))));
}
TEST_F(HoMM3PrayerRemovalNative, MixedSequentialPrayerAndHasteCancellationStaysUnsupported)
{
 prepare(MasteryLevel::ADVANCED,true);ASSERT_FALSE(HasFatalFailure());attackerSideHero->removeSpellFromSpellbook(SpellID(SpellID::PRAYER));teachSpell(attackerSideHero,SpellID(SpellID::DISPEL));teachSpell(defenderSideHero,SpellID(SpellID::PRAYER));teachSpell(defenderSideHero,SpellID(SpellID::HASTE));
 ASSERT_TRUE(castOn(defenderSideHero,SpellID(SpellID::PRAYER),enemy));battle()->getSide(BattleSide::DEFENDER).castSpellsCount=0;ASSERT_TRUE(castOn(defenderSideHero,SpellID(SpellID::HASTE),enemy));EXPECT_FALSE(choose());
}
TEST_F(HoMM3PrayerRemovalNative, FreshContextRejectsEffectiveBonusAndActiveChanges)
{
 prepare(MasteryLevel::ADVANCED,true);ASSERT_FALSE(HasFatalFailure());enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,1,BonusSourceID()));
 using Context=AIIntegration::Ports::HoMM3::FreshPrayerContext;
 bool entered=false;
 const auto bonus=Context::evaluate(*view,*lifecycle,true,[&](const Context&ctx)->std::optional<int32_t>{entered=true;EXPECT_TRUE(ctx.current());enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::PRIMARY_SKILL,BonusSource::OTHER,1,BonusSourceID(),BonusSubtypeID(PrimarySkill::ATTACK)));EXPECT_FALSE(ctx.current());return 42;});EXPECT_TRUE(entered);EXPECT_FALSE(bonus);
 entered=false;const auto active=Context::evaluate(*view,*lifecycle,true,[&](const Context&ctx)->std::optional<int32_t>{entered=true;EXPECT_TRUE(ctx.current());battle()->activeStack=enemy->unitId();EXPECT_FALSE(ctx.current());return 42;});EXPECT_TRUE(entered);EXPECT_FALSE(active);
}
TEST_F(HoMM3PrayerRemovalNative, StrongPrayerUsesRemovedBuffMasteryNotDispelMastery)
{
 prepare(MasteryLevel::ADVANCED,true);ASSERT_FALSE(HasFatalFailure());attackerSideHero->removeSpellFromSpellbook(SpellID(SpellID::PRAYER));teachSpell(attackerSideHero,SpellID(SpellID::DISPEL));teachSpell(defenderSideHero,SpellID(SpellID::PRAYER));setMagicMastery(defenderSideHero,MasteryLevel::EXPERT);
 ASSERT_TRUE(castOn(defenderSideHero,SpellID(SpellID::PRAYER),enemy));const auto selected=choose();ASSERT_TRUE(selected);EXPECT_EQ(selected->spell,SpellID::DISPEL);ASSERT_TRUE(castAsHero(attackerSideHero,selected->spell,selected->getTarget(view.get())));
 EXPECT_FALSE(enemy->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER)))));
}

TEST_F(HoMM3PrayerRemovalNative, ExpertMassDispelValuesBothOwnersAndRemovesAllPrayer)
{
 prepare(MasteryLevel::BASIC,true);ASSERT_FALSE(HasFatalFailure());
 // Break the prefix comparator tie without altering either Prayer's components.
 enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,1,BonusSourceID()));
 attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,1,ChangeValueMode::ABSOLUTE);
 ASSERT_TRUE(castOn(attackerSideHero,SpellID(SpellID::PRAYER),ally));
 teachSpell(defenderSideHero,SpellID(SpellID::PRAYER));setMagicMastery(defenderSideHero,MasteryLevel::EXPERT);ASSERT_TRUE(castOn(defenderSideHero,SpellID(SpellID::PRAYER),enemy));
 battle()->getSide(BattleSide::ATTACKER).castSpellsCount=0;
 attackerSideHero->removeSpellFromSpellbook(SpellID(SpellID::PRAYER));teachSpell(attackerSideHero,SpellID(SpellID::DISPEL));setMagicMastery(attackerSideHero,MasteryLevel::EXPERT);
 for(const auto * unit:{ally,enemy})ASSERT_EQ(unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER))))->size(),3);
 const auto selected=choose();ASSERT_TRUE(selected);EXPECT_EQ(selected->spell,SpellID::DISPEL);
 const auto mana=attackerSideHero->mana;const auto cost=view->battleGetSpellCost(SpellID(SpellID::DISPEL).toSpell(),attackerSideHero);
 ASSERT_TRUE(castAsHero(attackerSideHero,selected->spell,selected->getTarget(view.get())));
 EXPECT_EQ(attackerSideHero->mana,mana-cost);
 for(const auto * unit:{ally,enemy})EXPECT_FALSE(unit->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER)))));
 EXPECT_TRUE(enemy->hasBonus(Selector::type()(BonusType::STACKS_SPEED).And(Selector::sourceType()(BonusSource::OTHER))));
}
TEST_F(HoMM3PrayerRemovalNative, ExpertMassDispelDoesNotIgnoreGreaterFriendlyLoss)
{
 prepare(MasteryLevel::BASIC,true);ASSERT_FALSE(HasFatalFailure());
 // Break the prefix comparator tie without altering either Prayer's components.
 enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,1,BonusSourceID()));
 ASSERT_TRUE(castOn(attackerSideHero,SpellID(SpellID::PRAYER),ally));
 teachSpell(defenderSideHero,SpellID(SpellID::PRAYER));setMagicMastery(defenderSideHero,MasteryLevel::EXPERT);ASSERT_TRUE(castOn(defenderSideHero,SpellID(SpellID::PRAYER),enemy));
 battle()->getSide(BattleSide::ATTACKER).castSpellsCount=0;
 attackerSideHero->removeSpellFromSpellbook(SpellID(SpellID::PRAYER));teachSpell(attackerSideHero,SpellID(SpellID::DISPEL));setMagicMastery(attackerSideHero,MasteryLevel::EXPERT);
 for(const auto * unit:{ally,enemy})ASSERT_EQ(unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER))))->size(),3);
 EXPECT_FALSE(choose());
 for(const auto *unit:{ally,enemy})EXPECT_EQ(unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER))))->size(),3);
}
TEST_F(HoMM3PrayerRemovalNative, ExpertMassDispelRejectsOtherParticipantsUnsupportedEffect)
{
 prepare(MasteryLevel::BASIC,true);ASSERT_FALSE(HasFatalFailure());
 enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,1,BonusSourceID()));
 teachSpell(defenderSideHero,SpellID(SpellID::PRAYER));ASSERT_TRUE(castOn(defenderSideHero,SpellID(SpellID::PRAYER),enemy));
 teachSpell(attackerSideHero,SpellID(SpellID::HASTE));ASSERT_TRUE(castOn(attackerSideHero,SpellID(SpellID::HASTE),ally));
 battle()->getSide(BattleSide::ATTACKER).castSpellsCount=0;attackerSideHero->removeSpellFromSpellbook(SpellID(SpellID::PRAYER));attackerSideHero->removeSpellFromSpellbook(SpellID(SpellID::HASTE));teachSpell(attackerSideHero,SpellID(SpellID::DISPEL));setMagicMastery(attackerSideHero,MasteryLevel::EXPERT);
 EXPECT_FALSE(choose());
 EXPECT_TRUE(enemy->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER)))));
 EXPECT_TRUE(ally->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::HASTE)))));
}

#define PinnedPrayerRemoval NativeMassOraclePrayer
#define PinnedMassPrayer NativeMassOracle
#include "homm3MassPrayer/PinnedMass.h"
#undef PinnedMassPrayer
#undef PinnedPrayerRemoval
TEST_F(HoMM3PrayerRemovalNative, MassTargetCopiesPreserveOpponentsAndDifferFromGlobalCleanDamage)
{
 prepare(MasteryLevel::BASIC,true);ASSERT_FALSE(HasFatalFailure());
 ASSERT_TRUE(castOn(attackerSideHero,SpellID(SpellID::PRAYER),ally));
 teachSpell(defenderSideHero,SpellID(SpellID::PRAYER));setMagicMastery(defenderSideHero,MasteryLevel::ADVANCED);ASSERT_TRUE(castOn(defenderSideHero,SpellID(SpellID::PRAYER),enemy));
 using namespace AIIntegration::Ports::HoMM3;
 auto baseline=std::make_shared<HypotheticBattle>(environment.get(),view);
 auto allClean=singlePrayerRemovedCopy(environment.get(),baseline,ally->unitId());
 allClean->getForUpdate(enemy->unitId())->removeUnitBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER))));
 std::vector<int32_t>correct[2],wrong[2];NativeMassOracle::Manager oracle;
 for(int side=0;side<2;++side)
 {
  const auto*target=side==0?ally:enemy;const auto*other=side==0?enemy:ally;
  auto isolated=singlePrayerRemovedCopy(environment.get(),baseline,target->unitId());
  EXPECT_FALSE(isolated->battleGetUnitByID(target->unitId())->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER)))));
  EXPECT_EQ(isolated->battleGetUnitByID(other->unitId())->getAttack(true),other->getAttack(true));
  ASSERT_TRUE(isolated->battleGetUnitByID(other->unitId())->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER)))));
  auto facts=[&](const HypotheticBattle&board){
   PrayerRemovalFacts f;f.singlePrayerRemoved=f.cleanedBaselineKnown=f.ownerContextKnown=true;f.friendly=side==0;f.duration=f.rounds=3;f.health=10000;f.hitPoints=10;f.total=side==0?500:1500;f.expectedDamage=f.cachedIncoming=1000;f.hasWorstEnemy=true;
   const auto*victim=board.battleGetUnitByID(target->unitId());const auto*attacker=board.battleGetUnitByID(other->unitId());
   StackWithBonuses boosted(&board,victim);std::vector<Bonus>bonuses;for(const auto&b:*target->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER)))))bonuses.push_back(*b);boosted.addUnitBonus(bonuses);
   const auto damage=[&](const battle::Unit*defender){BattleAttackInfo attack(attacker,defender,0,true);const auto range=board.battleEstimateDamage(attack);return (double(range.damage.min)+range.damage.max)/2;};
   f.defenseEstimatedBefore=damage(victim);f.defenseEstimatedBoosted=damage(&boosted);f.defenseAverageDamage=int32_t(f.defenseEstimatedBefore);return f;
  };
  const auto f=facts(*isolated),bad=facts(*allClean);const auto value=singlePrayerRemovalValue(f),wrongValue=singlePrayerRemovalValue(bad);ASSERT_TRUE(value);ASSERT_TRUE(wrongValue);
  correct[side].push_back(*value);wrong[side].push_back(*wrongValue);oracle.m_armies[side].push_back({f,true,true});
 }
 const auto total=sumPrayerRemovalValues(correct[0],correct[1]);ASSERT_TRUE(total);EXPECT_EQ(*total,NativeMassOracle::evaluate(oracle));
 EXPECT_NE(total,sumPrayerRemovalValues(wrong[0],wrong[1]));
 RecordProperty("independent_sum",std::to_string(*total));RecordProperty("incorrect_global_sum",std::to_string(*sumPrayerRemovalValues(wrong[0],wrong[1])));
 for(const auto*unit:{ally,enemy})EXPECT_EQ(unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER))))->size(),3);
}
TEST_F(HoMM3PrayerRemovalNative, WholePreviewRejectsKilledParticipantAndNonPrayerBonusChange)
{
 prepare(MasteryLevel::BASIC,true);ASSERT_FALSE(HasFatalFailure());
 teachSpell(defenderSideHero,SpellID(SpellID::PRAYER));ASSERT_TRUE(castOn(defenderSideHero,SpellID(SpellID::PRAYER),enemy));
 using namespace AIIntegration::Ports::HoMM3;
 auto baseline=std::make_shared<HypotheticBattle>(environment.get(),view);
 auto valid=singlePrayerRemovedCopy(environment.get(),baseline,enemy->unitId());
 ASSERT_TRUE(onlyPrayerRemoved(environment.get(),baseline,*valid));
 int64_t damage=ally->getAvailableHealth();valid->damageUnit(ally->unitId(),damage);
 ASSERT_FALSE(valid->battleGetUnitByID(ally->unitId())->alive());EXPECT_FALSE(onlyPrayerRemoved(environment.get(),baseline,*valid));
 auto changed=singlePrayerRemovedCopy(environment.get(),baseline,enemy->unitId());
 Bonus extra(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,1,BonusSourceID());changed->getForUpdate(ally->unitId())->addUnitBonus({extra});
 EXPECT_FALSE(onlyPrayerRemoved(environment.get(),baseline,*changed));
 // An already retained corpse must remain unchanged, not disappear from admission.
 auto deadBaseline=std::make_shared<HypotheticBattle>(environment.get(),baseline);int64_t fatal=enemy->getAvailableHealth();deadBaseline->damageUnit(enemy->unitId(),fatal);
 auto deadUnchanged=std::make_shared<HypotheticBattle>(environment.get(),deadBaseline);EXPECT_TRUE(onlyPrayerRemoved(environment.get(),deadBaseline,*deadUnchanged));
 auto deadRemoved=singlePrayerRemovedCopy(environment.get(),deadBaseline,enemy->unitId());EXPECT_FALSE(onlyPrayerRemoved(environment.get(),deadBaseline,*deadRemoved));
}

TEST_F(HoMM3PrayerRemovalNative, FourteenStackMassUsesSingleSharedFreshContext)
{
 prepare(MasteryLevel::BASIC,true,false,7);ASSERT_FALSE(HasFatalFailure());
 attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,1,ChangeValueMode::ABSOLUTE);
 for(const auto&unit:battle()->stacks)if(unit->unitSide()==BattleSide::ATTACKER){battle()->getSide(BattleSide::ATTACKER).castSpellsCount=0;ASSERT_TRUE(castOn(attackerSideHero,SpellID(SpellID::PRAYER),unit.get()));}
 for(const auto&unit:battle()->stacks)if(unit->unitSide()==BattleSide::DEFENDER)unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,1,BonusSourceID()));
 teachSpell(defenderSideHero,SpellID(SpellID::PRAYER));setMagicMastery(defenderSideHero,MasteryLevel::EXPERT);ASSERT_TRUE(castOn(defenderSideHero,SpellID(SpellID::PRAYER),enemy));
 ASSERT_EQ(battle()->stacks.size(),14);
 for(const auto&unit:battle()->stacks)ASSERT_EQ(unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER))))->size(),3);
 battle()->getSide(BattleSide::ATTACKER).castSpellsCount=0;attackerSideHero->removeSpellFromSpellbook(SpellID(SpellID::PRAYER));teachSpell(attackerSideHero,SpellID(SpellID::DISPEL));setMagicMastery(attackerSideHero,MasteryLevel::EXPERT);
 const auto prefix=HoMM3Forecast::nativeShooterPrefix(*view,*lifecycle,false);ASSERT_TRUE(prefix.outcome)<<prefix.unavailable;
 const auto start=std::chrono::steady_clock::now();const auto selected=choose();
 RecordProperty("choose_microseconds",std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count()));
 ASSERT_TRUE(selected);ASSERT_EQ(selected->spell,SpellID::DISPEL);
 ASSERT_TRUE(castAsHero(attackerSideHero,selected->spell,selected->getTarget(view.get())));
 for(const auto&unit:battle()->stacks)EXPECT_FALSE(unit->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER)))));
}

TEST_F(HoMM3PrayerRemovalNative, ModifiedDispelExtraEffectMustReject)
{
 prepare(MasteryLevel::ADVANCED,true);ASSERT_FALSE(HasFatalFailure());
 attackerSideHero->removeSpellFromSpellbook(SpellID(SpellID::PRAYER));teachSpell(attackerSideHero,SpellID(SpellID::DISPEL));teachSpell(defenderSideHero,SpellID(SpellID::PRAYER));
 ASSERT_TRUE(castOn(defenderSideHero,SpellID(SpellID::PRAYER),enemy));
 auto * spell=SpellID(SpellID::DISPEL).toSpell();
 spells::BattleCast cast(view.get(),attackerSideHero,spells::Mode::HERO,spell);
 const auto mechanics=spell->battleMechanics(&cast);
 auto &effects=const_cast<CSpell::LevelInfo &>(spell->getLevelInfo(mechanics->getEffectLevel())).battleEffects;
 struct Restore{JsonNode &target;JsonNode before;~Restore(){target=before;LIBRARY->spellh->afterLoadFinalization();}}restore{effects,effects};
 effects["zunsupported"]["type"].String()="timed";
 auto &bonus=effects["zunsupported"]["bonus"]["extraMirror"];
 bonus["type"].String()="MAGIC_MIRROR";bonus["val"].Integer()=1;bonus["duration"].String()="N_TURNS";
 LIBRARY->spellh->afterLoadFinalization();
 auto projected=std::make_shared<HypotheticBattle>(environment.get(),view);
 spells::BattleCast preview(projected.get(),attackerSideHero,spells::Mode::HERO,spell);
 preview.castEval(projected->getServerCallback(),spells::Target{battle::Destination(enemy)});
 auto * changed=projected->battleGetUnitByID(enemy->unitId());ASSERT_NE(changed,nullptr);
 ASSERT_FALSE(changed->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER)))));
 ASSERT_TRUE(changed->hasBonusOfType(BonusType::MAGIC_MIRROR));
 const auto mana=attackerSideHero->mana;const auto packets=server.casts.size();
 const auto selected=choose();
 if(selected)std::cerr<<"MODDED_DISPEL_SELECTED "<<selected->toString()<<"\n";
 EXPECT_FALSE(selected)<<"Prayer-only domain must reject Dispel adding another effect";
 EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(server.casts.size(),packets);
 EXPECT_FALSE(enemy->hasBonusOfType(BonusType::MAGIC_MIRROR));
 EXPECT_TRUE(enemy->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::PRAYER)))));
}
