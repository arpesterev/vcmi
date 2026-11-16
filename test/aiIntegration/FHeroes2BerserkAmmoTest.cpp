/* GPL-2.0-or-later. Pinned FHeroes2 Berserk remaining-ammunition semantics. */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/FHeroes2SpellPolicy.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpellHandler.h"
#include <vcmi/Environment.h>
namespace {
class AmmoEnvironment final : public Environment {
 std::shared_ptr<CCallback> gameCb; std::shared_ptr<CBattleCallback> battleCb;
public:
 AmmoEnvironment(std::shared_ptr<CCallback> g,std::shared_ptr<CBattleCallback> b):gameCb(std::move(g)),battleCb(std::move(b)){}
 const Services * services() const override{return LIBRARY;}
 const GameCb * game() const override{return gameCb.get();}
 const BattleCb * battle(const BattleID & id) const override{return battleCb->getBattle(id).get();}
};
}
class FHeroes2BerserkAmmoBattle : public BattleTestFixture, public ::testing::WithParamInterface<bool> {
protected:
 CStack * subject=nullptr; CStack * alternative=nullptr;
 std::shared_ptr<CBattleCallback> callback; std::shared_ptr<CPlayerBattleCallback> view; std::shared_ptr<AmmoEnvironment> environment;
 void prepare(BattleSide side, SpellID selected){
  startGame(); ASSERT_FALSE(HasFatalFailure()); startBattle(); ASSERT_FALSE(HasFatalFailure());
  subject=addStack(side,CreatureID(2),BattleHex(4*17+(side==BattleSide::ATTACKER?2:14)),30);
  alternative=addStack(side,CreatureID(2),BattleHex(7*17+(side==BattleSide::ATTACKER?4:12)),20);
  ASSERT_NE(subject,nullptr); ASSERT_NE(alternative,nullptr);
  // For friendly removal, distance is measured from the opposite approach edge.
  beginCombat(); ASSERT_FALSE(HasFatalFailure());
  ASSERT_GT(subject->shots.available(),0);
  if(GetParam())subject->shots.use(subject->shots.total());
  ASSERT_TRUE(subject->isShooter()); ASSERT_EQ(AIIntegration::fheroes2IsArcher(*subject),!GetParam());
  for(const auto & spell:LIBRARY->spellh->objects)attackerSideHero->removeSpellFromSpellbook(spell->id);
  teachSpell(attackerSideHero,selected); attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,6,ChangeValueMode::ABSOLUTE); setMagicMastery(attackerSideHero,MasteryLevel::BASIC);
  callback=std::make_shared<CBattleCallback>(attackerSideHero->getOwner(),nullptr);callback->onBattleStarted(battle());view=callback->getBattle(battle()->getBattleID());
  environment=std::make_shared<AmmoEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
 }
 void seedEffect(CGHeroInstance * hero,SpellID id,CStack * unit){
  // Real round packet resets the hero cast allowance between independent setup casts.
  BattleNextRound next;next.battleID=battle()->getBattleID();server.applyPack(next);
  teachSpell(hero,id);hero->setPrimarySkill(PrimarySkill::SPELL_POWER,6,ChangeValueMode::ABSOLUTE);
  ASSERT_TRUE(castOn(hero,id,unit));
 }
 std::optional<BattleAction> choose(){
  const auto mana=attackerSideHero->mana; const auto casts=server.casts.size();
  std::vector<std::tuple<CStack *,int64_t,int,bool>> before;
  for(const auto & unit:battle()->stacks)before.emplace_back(unit.get(),unit->getAvailableHealth(),unit->shots.available(),unit->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
  const auto result=AIIntegration::FHeroes2SpellPolicy::choose(view,environment.get());
  EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(server.casts.size(),casts);
  for(const auto &[unit,hp,shots,berserk]:before){EXPECT_EQ(unit->getAvailableHealth(),hp);EXPECT_EQ(unit->shots.available(),shots);EXPECT_EQ(unit->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE),berserk);}
  return result;
 }
 void friendlyRemoval(SpellID id){
  prepare(BattleSide::ATTACKER,id);ASSERT_FALSE(HasFatalFailure());
  // Add enough opposition to keep the source conservation threshold below both choices.
  ASSERT_NE(addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(5*17+13),500),nullptr);
  seedEffect(defenderSideHero,SpellID::BERSERK,subject);seedEffect(defenderSideHero,SpellID::BERSERK,alternative);ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(subject->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
  const auto action=choose();ASSERT_TRUE(action);
  const auto * expected=GetParam()?alternative:subject;
  EXPECT_EQ(view->battleGetUnitByPos(action->target.front().hexValue),expected);
  const auto * selected=dynamic_cast<const CStack *>(view->battleGetUnitByPos(action->target.front().hexValue)); ASSERT_NE(selected,nullptr);
  ASSERT_TRUE(castOn(attackerSideHero,id,selected));EXPECT_FALSE(selected->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
 }
};
TEST_P(FHeroes2BerserkAmmoBattle,directRanksRemainingAmmunition){
 prepare(BattleSide::DEFENDER,SpellID::BERSERK);ASSERT_FALSE(HasFatalFailure());
 const double big=subject->estimateCombatValue(),small=alternative->estimateCombatValue();
 ASSERT_GT(big,small);ASSERT_LT(big/subject->getPosition().getX(),small);
 const auto action=choose();ASSERT_TRUE(action);
 const auto * expected=GetParam()?alternative:subject;
 EXPECT_EQ(view->battleGetUnitByPos(action->target.front().hexValue),expected);
 const auto * selected=dynamic_cast<const CStack *>(view->battleGetUnitByPos(action->target.front().hexValue)); ASSERT_NE(selected,nullptr);
 ASSERT_TRUE(castOn(attackerSideHero,SpellID::BERSERK,selected));EXPECT_TRUE(selected->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
}
TEST_P(FHeroes2BerserkAmmoBattle,cureRanksRemainingAmmunition){friendlyRemoval(SpellID::CURE);}
TEST_P(FHeroes2BerserkAmmoBattle,friendlyDispelRanksRemainingAmmunition){friendlyRemoval(SpellID::DISPEL);}
TEST_P(FHeroes2BerserkAmmoBattle,enemyDispelBalancesBerserkAgainstBuff){
 prepare(BattleSide::DEFENDER,SpellID::DISPEL);ASSERT_FALSE(HasFatalFailure());
 seedEffect(defenderSideHero,SpellID::BLOODLUST,subject);seedEffect(attackerSideHero,SpellID::BERSERK,subject);ASSERT_FALSE(HasFatalFailure());
 setMagicMastery(attackerSideHero,MasteryLevel::ADVANCED);
 BattleNextRound next;next.battleID=battle()->getBattleID();server.applyPack(next);
 // Restore only the candidate spell to the chooser after fixture setup.
 attackerSideHero->removeSpellFromSpellbook(SpellID::BERSERK);
 ASSERT_TRUE(subject->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
 ASSERT_LT(0.85/subject->getPosition().getX(),0.1);
 const auto action=choose();
 if(!GetParam()){EXPECT_FALSE(action);return;}
 ASSERT_TRUE(action);EXPECT_EQ(view->battleGetUnitByPos(action->target.front().hexValue),subject);
 const auto bloodlust = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLOODLUST)));
 ASSERT_TRUE(subject->hasBonus(bloodlust));
 ASSERT_TRUE(castOn(attackerSideHero,SpellID::DISPEL,subject));EXPECT_FALSE(subject->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
 EXPECT_FALSE(subject->hasBonus(bloodlust));
}
INSTANTIATE_TEST_SUITE_P(RemainingShots,FHeroes2BerserkAmmoBattle,::testing::Bool(),[](const auto & info){return info.param?"Exhausted":"Armed";});
