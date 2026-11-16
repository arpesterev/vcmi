/* GPL-2.0-or-later. Direct Prayer composition on native battle projections. */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/HoMM3SpellPolicy.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../server/CGameHandler.h"
#include <vcmi/Environment.h>
#include "homm3Prayer/PinnedPrayer.h"

namespace
{
class PrayerEnvironment final : public Environment
{
 std::shared_ptr<CCallback> gameCallback;
 std::shared_ptr<CBattleCallback> battleCallback;
public:
 PrayerEnvironment(std::shared_ptr<CCallback> game, std::shared_ptr<CBattleCallback> battle)
  : gameCallback(std::move(game)), battleCallback(std::move(battle)) {}
 const Services * services() const override { return LIBRARY; }
 const GameCb * game() const override { return gameCallback.get(); }
 const BattleCb * battle(const BattleID & id) const override { return battleCallback->getBattle(id).get(); }
};
}

class HoMM3PrayerBattle : public BattleTestFixture
{
protected:
 CStack * ally = nullptr;
 CStack * enemy = nullptr;
 std::shared_ptr<CBattleCallback> callback;
 std::shared_ptr<CPlayerBattleCallback> view;
 std::shared_ptr<PrayerEnvironment> environment;
 void prepare(int mastery = MasteryLevel::BASIC, bool shooters = false, bool extraAlly = false)
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
  if(extraAlly) { ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id,SlotID(1)),creature.toCreature(),100)); }
  startBattle(); ASSERT_FALSE(HasFatalFailure());
  for(const auto & unit:battle()->stacks)
   if(unit->unitSide()==BattleSide::ATTACKER) { if(!ally) ally=unit.get(); else unit->setPosition(BattleHex(110)); } else enemy=unit.get();
  ASSERT_NE(ally,nullptr); ASSERT_NE(enemy,nullptr);
  ally->setPosition(BattleHex(92)); enemy->setPosition(BattleHex(shooters ? 100 : 93));
  teachSpell(attackerSideHero,SpellID(SpellID::PRAYER));
  attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,6,ChangeValueMode::ABSOLUTE);
  setMagicMastery(attackerSideHero,mastery);
  beginCombat(); ASSERT_FALSE(HasFatalFailure());
  battle()->activeStack=ally->unitId();
  callback=std::make_shared<CBattleCallback>(attackerSideHero->getOwner(),nullptr);
  callback->onBattleStarted(battle()); view=callback->getBattle(battle()->getBattleID());
  environment=std::make_shared<PrayerEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
 }
 std::optional<BattleAction> choose(const HoMM3Forecast::PrefixProjection & forecast={})
 {
  const auto mana=attackerSideHero->mana;
  const auto health=ally->getAvailableHealth();
  const auto attack=ally->getAttack(false), defense=ally->getDefense(false);
  const auto speed=ally->getMovementRange();
  const auto casts=server.casts.size();
  auto selected=AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*ally,forecast);
  EXPECT_EQ(attackerSideHero->mana,mana); EXPECT_EQ(ally->getAvailableHealth(),health);
  EXPECT_EQ(ally->getAttack(false),attack); EXPECT_EQ(ally->getDefense(false),defense);
  EXPECT_EQ(ally->getMovementRange(),speed); EXPECT_EQ(server.casts.size(),casts);
  return selected;
 }
};

class HoMM3PrayerMastery : public HoMM3PrayerBattle, public ::testing::WithParamInterface<std::tuple<int,bool>> {};
TEST_P(HoMM3PrayerMastery, ChoosesPrayerAndNativeServerAppliesAllThreeEffects)
{
 const auto [mastery,shooters]=GetParam(); prepare(mastery,shooters); ASSERT_FALSE(HasFatalFailure());
 const auto attack=ally->getAttack(false), defense=ally->getDefense(false);
 const auto speed=ally->getMovementRange(); const auto mana=attackerSideHero->mana;
 const auto selected=choose(); ASSERT_TRUE(selected); EXPECT_EQ(selected->spell,SpellID::PRAYER);
 if(mastery != MasteryLevel::EXPERT) { ASSERT_FALSE(selected->target.empty()); }
 ASSERT_TRUE(castAsHero(attackerSideHero,selected->spell,selected->getTarget(view.get())));
 const int increment=mastery>=MasteryLevel::ADVANCED ? 4 : 2;
 EXPECT_EQ(ally->getAttack(false),attack+increment); EXPECT_EQ(ally->getDefense(false),defense+increment);
 EXPECT_EQ(ally->getMovementRange(),speed+increment); EXPECT_LT(attackerSideHero->mana,mana);
 EXPECT_EQ(server.castsOf(SpellID(SpellID::PRAYER)).size(),1u);
}
INSTANTIATE_TEST_SUITE_P(MasteryAndRole,HoMM3PrayerMastery,
 ::testing::Combine(::testing::Values(MasteryLevel::NONE,MasteryLevel::BASIC,MasteryLevel::ADVANCED,MasteryLevel::EXPERT),::testing::Bool()));

TEST_F(HoMM3PrayerBattle, AlreadyPrayingUnitIsNotRecast)
{
 prepare(); ASSERT_FALSE(HasFatalFailure());
 ASSERT_TRUE(castOn(attackerSideHero,SpellID(SpellID::PRAYER),ally));
 battle()->getSide(BattleSide::ATTACKER).castSpellsCount=0;
 EXPECT_FALSE(choose());
}
TEST_F(HoMM3PrayerBattle, NoManaDoesNotChoosePrayer)
{
 prepare(); ASSERT_FALSE(HasFatalFailure()); attackerSideHero->mana=0; EXPECT_FALSE(choose());
}
TEST_F(HoMM3PrayerBattle, InactiveUnitDoesNotReceiveValue)
{
 prepare(); ASSERT_FALSE(HasFatalFailure());
 ally->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::NOT_ACTIVE,BonusSource::OTHER,1,BonusSourceID()));
 EXPECT_FALSE(choose());
}
TEST_F(HoMM3PrayerBattle, ConcurrentHasteStillAllowsPrayerAttackAndDefense)
{
 prepare(); ASSERT_FALSE(HasFatalFailure());
 teachSpell(attackerSideHero,SpellID(SpellID::HASTE));
 ASSERT_TRUE(castOn(attackerSideHero,SpellID(SpellID::HASTE),ally));
 attackerSideHero->removeSpellFromSpellbook(SpellID(SpellID::HASTE));
 battle()->getSide(BattleSide::ATTACKER).castSpellsCount=0;
 const auto selected=choose(); ASSERT_TRUE(selected); EXPECT_EQ(selected->spell,SpellID::PRAYER);
 ASSERT_TRUE(castAsHero(attackerSideHero,selected->spell,selected->getTarget(view.get())));
}
TEST_F(HoMM3PrayerBattle, ExpertMassPrayerUsesAllEligibleAllies)
{
 prepare(MasteryLevel::EXPERT,false,true); ASSERT_FALSE(HasFatalFailure());
 CStack * second=nullptr;
 for(const auto & unit:battle()->stacks) if(unit->unitSide()==BattleSide::ATTACKER && unit.get()!=ally) second=unit.get();
 ASSERT_NE(second,nullptr);
 const auto before=second->getAttack(false);
 const auto selected=choose(); ASSERT_TRUE(selected); EXPECT_EQ(selected->spell,SpellID::PRAYER);
 ASSERT_TRUE(castAsHero(attackerSideHero,selected->spell,selected->getTarget(view.get())));
 EXPECT_EQ(second->getAttack(false),before+4);
}

TEST(HoMM3PrayerComposition, SeparatelyTruncatesComponentsAndRejectsOverflow)
{
 using AIIntegration::Ports::HoMM3::prayerComponentValue;
 EXPECT_EQ(prayerComponentValue(3.8,4.7,5.9),12);
 EXPECT_EQ(prayerComponentValue(0.9,0.9,0.9),0);
 EXPECT_EQ(prayerComponentValue(INT32_MAX,0,0),INT32_MAX);
 EXPECT_FALSE(prayerComponentValue(INT32_MAX,1,0));
 EXPECT_FALSE(prayerComponentValue(-1,0,0));
 EXPECT_FALSE(prayerComponentValue(std::numeric_limits<double>::infinity(),0,0));
 EXPECT_FALSE(prayerComponentValue(std::numeric_limits<double>::quiet_NaN(),0,0));
}

TEST_F(HoMM3PrayerBattle, ConcurrentSlowKeepsNativeSpeedAndSourcePrayerValuationSeparate)
{
 prepare(); ASSERT_FALSE(HasFatalFailure());
 teachSpell(defenderSideHero,SpellID(SpellID::SLOW));
 ASSERT_TRUE(castOn(defenderSideHero,SpellID(SpellID::SLOW),ally));
 const auto oldAttack=ally->getAttack(false),oldDefense=ally->getDefense(false);
 const auto selected=choose(); ASSERT_TRUE(selected); EXPECT_EQ(selected->spell,SpellID::PRAYER);
 ASSERT_TRUE(castAsHero(attackerSideHero,selected->spell,selected->getTarget(view.get())));
 EXPECT_EQ(ally->getAttack(false),oldAttack+2); EXPECT_EQ(ally->getDefense(false),oldDefense+2);
 EXPECT_TRUE(ally->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::SLOW)))));
}
TEST_F(HoMM3PrayerBattle, SourceWinningBranchRetainsDefenseComponent)
{
 prepare(); ASSERT_FALSE(HasFatalFailure());
 HoMM3Forecast::PrefixProjection forecast; forecast.outcome.emplace();
 forecast.outcome->winLikely=true; forecast.outcome->spellsNotRequired=false;
 const auto selected=choose(forecast); ASSERT_TRUE(selected); EXPECT_EQ(selected->spell,SpellID::PRAYER);
}
TEST_F(HoMM3PrayerBattle, SourceHealingOnlyBranchSuppressesPrayer)
{
 prepare(); ASSERT_FALSE(HasFatalFailure());
 HoMM3Forecast::PrefixProjection forecast; forecast.outcome.emplace();
 forecast.outcome->winLikely=true; forecast.outcome->spellsNotRequired=true;
 EXPECT_FALSE(choose(forecast));
}
TEST_F(HoMM3PrayerBattle, CanChooseSpeedComponentBeforeAnyImmediateAttack)
{
 prepare(); ASSERT_FALSE(HasFatalFailure()); enemy->setPosition(BattleHex(98));
 const auto selected=choose(); ASSERT_TRUE(selected); EXPECT_EQ(selected->spell,SpellID::PRAYER);
}
TEST(HoMM3PrayerComposition, MatchesPinnedComponentComposition)
{
 PinnedPrayer::army enemy{nullptr,1},ours{&enemy,1};
 for(double defense:{0.0,0.9,5.3,100.1})
  for(double speed:{0.0,2.9,17.2})
   for(double attack:{0.0,0.8,6.9,1000.1})
   {
    PinnedPrayer::type_AI_spellcaster caster{defense,speed,attack};
    EXPECT_EQ(AIIntegration::Ports::HoMM3::prayerComponentValue(defense,speed,attack),caster.getPrayerValue(&ours,{1,3}));
    ours.time=2;
    EXPECT_EQ(AIIntegration::Ports::HoMM3::prayerComponentValue(defense,speed,0),caster.getPrayerValue(&ours,{1,3}));
    ours.time=1;
   }
}
