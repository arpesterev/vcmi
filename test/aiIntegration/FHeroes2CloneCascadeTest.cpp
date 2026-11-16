/* GPL-2.0-or-later. Native clone cascade versus pinned FHeroes2 damage ordering. */
#include "StdInc.h"
#include "fheroesStrength/BasicNativeStrengthOracle.h"
#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/FHeroes2SpellPolicy.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include <vcmi/Environment.h>

namespace
{
class CloneEnvironment final : public Environment
{
 std::shared_ptr<CCallback> gameCallback;
 std::shared_ptr<CBattleCallback> battleCallback;
public:
 CloneEnvironment(std::shared_ptr<CCallback> game, std::shared_ptr<CBattleCallback> battle)
  : gameCallback(std::move(game)), battleCallback(std::move(battle)) {}
 const Services * services() const override { return LIBRARY; }
 const GameCb * game() const override { return gameCallback.get(); }
 const BattleCb * battle(const BattleID & id) const override { return battleCallback->getBattle(id).get(); }
};
}

class FHeroes2CloneCascadeBattle : public BattleTestFixture
{
protected:
 SpellID damageSpell = SpellID(SpellID::MAGIC_ARROW);
 CStack * owner = nullptr;
 CStack * clone = nullptr;
 CStack * alternative = nullptr;
 CStack * reserve = nullptr;
 std::shared_ptr<CBattleCallback> callback;
 std::shared_ptr<CPlayerBattleCallback> view;
 std::shared_ptr<CloneEnvironment> environment;

 static void immune(CStack * unit)
 {
  unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
   BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 100, BonusSourceID()));
 }
 void prepare(bool withClone = true, int power = 30, SpellID selectedSpell = SpellID(SpellID::MAGIC_ARROW))
 {
  damageSpell = selectedSpell;
  startGame(); ASSERT_FALSE(HasFatalFailure());
  startBattle(); ASSERT_FALSE(HasFatalFailure());
  owner = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(70), 5);
  alternative = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(90), 11);
  reserve = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(110), 100);
  ASSERT_NE(owner,nullptr); ASSERT_NE(alternative,nullptr); ASSERT_NE(reserve,nullptr);
  immune(reserve);
  beginCombat(); ASSERT_FALSE(HasFatalFailure());
  if(withClone)
  {
   teachSpell(defenderSideHero, SpellID::CLONE);
   defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 6, ChangeValueMode::ABSOLUTE);
   ASSERT_TRUE(castOn(defenderSideHero, SpellID::CLONE, owner));
   ASSERT_GE(owner->cloneID,0);
   clone = const_cast<CStack *>(dynamic_cast<const CStack *>(battle()->battleGetUnitByID(owner->cloneID)));
   ASSERT_NE(clone,nullptr); ASSERT_TRUE(clone->alive()); ASSERT_TRUE(clone->isClone());
   ASSERT_EQ(clone->unitSide(),owner->unitSide());
  }
  for(const auto & spell : LIBRARY->spellh->objects) attackerSideHero->removeSpellFromSpellbook(spell->id);
  teachSpell(attackerSideHero, damageSpell);
  attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
  setMagicMastery(attackerSideHero, MasteryLevel::BASIC);
  callback = std::make_shared<CBattleCallback>(attackerSideHero->getOwner(), nullptr);
  callback->onBattleStarted(battle()); view = callback->getBattle(battle()->getBattleID());
  environment = std::make_shared<CloneEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
 }
 std::optional<BattleAction> choose()
 {
  const auto mana = attackerSideHero->mana;
  const auto hp = owner->getAvailableHealth();
  const auto link = owner->cloneID;
  const auto packets = server.casts.size();
  const auto selected = AIIntegration::FHeroes2SpellPolicy::choose(view,environment.get());
  EXPECT_EQ(attackerSideHero->mana,mana); EXPECT_EQ(owner->getAvailableHealth(),hp);
  EXPECT_EQ(owner->cloneID,link); EXPECT_EQ(server.casts.size(),packets);
  return selected;
 }
 std::shared_ptr<HypotheticBattle> preview(const CStack * target)
 {
  auto projected = std::make_shared<HypotheticBattle>(environment.get(),view);
  spells::BattleCast cast(projected.get(),attackerSideHero,spells::Mode::HERO,damageSpell.toSpell());
  cast.castEval(projected->getServerCallback(),spells::Target{battle::Destination(target)});
  return projected;
 }
};

TEST_F(FHeroes2CloneCascadeBattle, ownerCascadeCountsOnlyOneDestructionBonusForCompetingTarget)
{
 prepare(); ASSERT_FALSE(HasFatalFailure());
 auto projected = preview(owner);
 ASSERT_FALSE(projected->battleGetUnitByID(owner->unitId())->alive());
 ASSERT_FALSE(projected->battleGetUnitByID(clone->unitId())->alive());
 ASSERT_TRUE(owner->alive()); ASSERT_TRUE(clone->alive());
 // Pinned spellDamageValue: owner strength + linked clone strength + ONE owner's bonus.
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).usesSourceScale());
 double enemyStrength = 0, weightedSpeed = 0;
 for(const auto * unit : view->battleGetAllStacks())
  if(unit->alive() && view->battleGetOwner(unit) != attackerSideHero->getOwner())
  {
   enemyStrength += FHeroes2StrengthFixture::basicStrength(*unit);
   weightedSpeed += FHeroes2StrengthFixture::basicStrength(*unit) * unit->getMovementRange();
  }
 const auto sourceLethal = [&](const CStack & unit, double linkedStrength)
 {
  // Independent input-level transcription of pinned damageHeuristic lethal branch.
  const int speed = unit.moved() || unit.defended() || unit.hasBonusOfType(BonusType::NOT_ACTIVE)
   ? 0 : unit.getMovementRange();
  return FHeroes2StrengthFixture::basicStrength(unit) + linkedStrength
   + enemyStrength * (speed > weightedSpeed / enemyStrength ? 0.07 : 0.035);
 };
 const double ownerSource = sourceLethal(*owner,FHeroes2StrengthFixture::basicStrength(*clone));
 const double alternativeSource = sourceLethal(*alternative,0);
 const double oldCascade = sourceLethal(*owner,0) + sourceLethal(*clone,0);
 ASSERT_LT(ownerSource,alternativeSource);
 ASSERT_GT(oldCascade,alternativeSource) << "Fixture must distinguish source one-bonus ranking from old two-bonus ranking";
 const auto selected = choose(); ASSERT_TRUE(selected); ASSERT_FALSE(selected->target.empty());
 EXPECT_EQ(view->battleGetUnitByPos(selected->target.front().hexValue),alternative) << "selectedId=" << view->battleGetUnitByPos(selected->target.front().hexValue)->unitId() << " ownerId=" << owner->unitId();
}

TEST_F(FHeroes2CloneCascadeBattle, actualServerOwnerDeathRemovesLinkedClone)
{
 prepare(); ASSERT_FALSE(HasFatalFailure());
 const int cloneId=owner->cloneID;
 auto projected=preview(owner);
 ASSERT_FALSE(projected->battleGetUnitByID(owner->unitId())->alive());
 ASSERT_FALSE(projected->battleGetUnitByID(cloneId)->alive());
 ASSERT_TRUE(castAsHero(attackerSideHero,damageSpell,owner));
 EXPECT_FALSE(owner->alive()); EXPECT_FALSE(clone->alive()); EXPECT_TRUE(clone->isGhost() || clone->ghostPending);
 EXPECT_EQ(owner->cloneID,-1); EXPECT_TRUE(alternative->alive());
 EXPECT_FALSE(server.castsOf(damageSpell).empty());
}

TEST_F(FHeroes2CloneCascadeBattle, directCloneHitRetainsItsOwnLethalValueAndLeavesOwnerAlive)
{
 prepare(true,0); ASSERT_FALSE(HasFatalFailure());
 immune(owner); immune(alternative);
 const auto selected=choose(); ASSERT_TRUE(selected); ASSERT_FALSE(selected->target.empty());
 EXPECT_EQ(view->battleGetUnitByPos(selected->target.front().hexValue),clone);
 ASSERT_TRUE(castAsHero(attackerSideHero,damageSpell,clone));
 EXPECT_FALSE(clone->alive()); EXPECT_TRUE(owner->alive()); EXPECT_EQ(owner->cloneID,-1);
}

TEST_F(FHeroes2CloneCascadeBattle, nonlethalOwnerPreviewDoesNotRemoveClone)
{
 prepare(true,0); ASSERT_FALSE(HasFatalFailure());
 auto projected=preview(owner);
 EXPECT_TRUE(projected->battleGetUnitByID(owner->unitId())->alive());
 EXPECT_TRUE(projected->battleGetUnitByID(clone->unitId())->alive());
 EXPECT_EQ(owner->cloneID,clone->unitId());
 ASSERT_TRUE(castAsHero(attackerSideHero,damageSpell,owner));
 EXPECT_TRUE(owner->alive()); EXPECT_TRUE(clone->alive());
}

TEST_F(FHeroes2CloneCascadeBattle, noncloneTargetRankingIsUnchanged)
{
 prepare(false); ASSERT_FALSE(HasFatalFailure());
 const auto selected=choose(); ASSERT_TRUE(selected); ASSERT_FALSE(selected->target.empty());
 EXPECT_EQ(view->battleGetUnitByPos(selected->target.front().hexValue),alternative) << "selectedId=" << view->battleGetUnitByPos(selected->target.front().hexValue)->unitId() << " ownerId=" << owner->unitId();
 ASSERT_TRUE(castAsHero(attackerSideHero,damageSpell,alternative));
 EXPECT_FALSE(alternative->alive()); EXPECT_TRUE(owner->alive());
}

class FHeroes2CloneCascadeBySpell : public FHeroes2CloneCascadeBattle,
 public ::testing::WithParamInterface<SpellID> {};
TEST_P(FHeroes2CloneCascadeBySpell, ranksAndExecutesOwnerCascade)
{
 prepare(true,30,GetParam()); ASSERT_FALSE(HasFatalFailure());
 auto projected = preview(owner);
 ASSERT_FALSE(projected->battleGetUnitByID(owner->unitId())->alive());
 ASSERT_FALSE(projected->battleGetUnitByID(clone->unitId())->alive());
 ASSERT_TRUE(owner->alive()); ASSERT_TRUE(clone->alive());
 // Pinned spellDamageValue: owner strength + linked clone strength + ONE owner's bonus.
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).usesSourceScale());
 double enemyStrength = 0, weightedSpeed = 0;
 for(const auto * unit : view->battleGetAllStacks())
  if(unit->alive() && view->battleGetOwner(unit) != attackerSideHero->getOwner())
  {
   enemyStrength += FHeroes2StrengthFixture::basicStrength(*unit);
   weightedSpeed += FHeroes2StrengthFixture::basicStrength(*unit) * unit->getMovementRange();
  }
 const auto sourceLethal = [&](const CStack & unit, double linkedStrength)
 {
  // Independent input-level transcription of pinned damageHeuristic lethal branch.
  const int speed = unit.moved() || unit.defended() || unit.hasBonusOfType(BonusType::NOT_ACTIVE)
   ? 0 : unit.getMovementRange();
  return FHeroes2StrengthFixture::basicStrength(unit) + linkedStrength
   + enemyStrength * (speed > weightedSpeed / enemyStrength ? 0.07 : 0.035);
 };
 const double ownerSource = sourceLethal(*owner,FHeroes2StrengthFixture::basicStrength(*clone));
 const double alternativeSource = sourceLethal(*alternative,0);
 const double oldCascade = sourceLethal(*owner,0) + sourceLethal(*clone,0);
 ASSERT_LT(ownerSource,alternativeSource);
 ASSERT_GT(oldCascade,alternativeSource) << "Fixture must distinguish source one-bonus ranking from old two-bonus ranking";
 const auto selected = choose(); ASSERT_TRUE(selected); ASSERT_FALSE(selected->target.empty());
 EXPECT_EQ(view->battleGetUnitByPos(selected->target.front().hexValue),alternative) << "selectedId=" << view->battleGetUnitByPos(selected->target.front().hexValue)->unitId() << " ownerId=" << owner->unitId();
 ASSERT_TRUE(castAsHero(attackerSideHero,damageSpell,owner));
 EXPECT_FALSE(owner->alive()); EXPECT_FALSE(clone->alive());
 EXPECT_EQ(owner->cloneID,-1); EXPECT_FALSE(server.castsOf(damageSpell).empty());
}
INSTANTIATE_TEST_SUITE_P(OtherDirectSpells,FHeroes2CloneCascadeBySpell,
 ::testing::Values(SpellID(SpellID::ICE_BOLT),SpellID(SpellID::LIGHTNING_BOLT),SpellID(SpellID::IMPLOSION)),
 [](const ::testing::TestParamInfo<SpellID> & info)
 {
  if(info.param == SpellID::ICE_BOLT) return std::string("IceBolt");
  if(info.param == SpellID::LIGHTNING_BOLT) return std::string("LightningBolt");
  return std::string("Implosion");
 });
