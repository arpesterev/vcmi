/* GPL-2.0-or-later. Explicit native Clone ammunition projection, not Heroes II spawn parity. */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/FHeroes2SpellPolicy.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpellHandler.h"
#include <vcmi/Environment.h>
namespace {
class CloneAmmoEnvironment final : public Environment {
 std::shared_ptr<CCallback> gameCb;std::shared_ptr<CBattleCallback> battleCb;
public:
 CloneAmmoEnvironment(std::shared_ptr<CCallback> g,std::shared_ptr<CBattleCallback> b):gameCb(std::move(g)),battleCb(std::move(b)){}
 const Services * services() const override{return LIBRARY;}
 const GameCb * game() const override{return gameCb.get();}
 const BattleCb * battle(const BattleID & id) const override{return battleCb->getBattle(id).get();}
};
}
class FHeroes2CloneAmmoBattle : public BattleTestFixture,public ::testing::WithParamInterface<bool> {};
TEST_P(FHeroes2CloneAmmoBattle,valuesOriginalAmmunitionWithoutChangingNativeCloneStock){
 startGame();ASSERT_FALSE(HasFatalFailure());startBattle();ASSERT_FALSE(HasFatalFailure());
 auto * owner=addStack(BattleSide::ATTACKER,CreatureID(2),BattleHex(4*17+5),22);
 auto * alternative=addStack(BattleSide::ATTACKER,CreatureID(2),BattleHex(7*17+5),20);
 ASSERT_NE(owner,nullptr);ASSERT_NE(alternative,nullptr);
 ASSERT_NE(addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(5*17+13),200),nullptr);
 beginCombat();ASSERT_FALSE(HasFatalFailure());
 const auto totalShots=owner->shots.total();ASSERT_GT(totalShots,1);
 owner->shots.use(GetParam()?totalShots:totalShots/2);
 const auto remaining=owner->shots.available();ASSERT_LT(remaining,totalShots);
 ASSERT_EQ(AIIntegration::fheroes2IsArcher(*owner),!GetParam());
 // Pinned H2 would copy remaining shots and classify an exhausted original as ground.
 // Native H3 Clone still creates an armed unit; valuation uses the pre-cast target.
 const double large=owner->estimateCombatValue(),small=alternative->estimateCombatValue();
 ASSERT_GT(large,small);ASSERT_LT(large*0.33,small);
 for(const auto & spell:LIBRARY->spellh->objects)attackerSideHero->removeSpellFromSpellbook(spell->id);
 teachSpell(attackerSideHero,SpellID::CLONE);attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,6,ChangeValueMode::ABSOLUTE);setMagicMastery(attackerSideHero,MasteryLevel::BASIC);
 auto callback=std::make_shared<CBattleCallback>(attackerSideHero->getOwner(),nullptr);callback->onBattleStarted(battle());auto view=callback->getBattle(battle()->getBattleID());
 auto environment=std::make_shared<CloneAmmoEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
 const auto mana=attackerSideHero->mana;const auto packets=server.casts.size();const auto count=battle()->stacks.size();const auto health=owner->getAvailableHealth();
 ASSERT_LT(owner->cloneID,0);
 auto preview=std::make_shared<HypotheticBattle>(environment.get(),view);
 spells::BattleCast cast(preview.get(),attackerSideHero,spells::Mode::HERO,SpellID(SpellID::CLONE).toSpell());
 cast.castEval(preview->getServerCallback(),spells::Target{battle::Destination(owner)});
 const battle::Unit * projectedClone=nullptr;
 for(const auto * unit:preview->battleGetUnitsIf([](const battle::Unit * u){return u->alive()&&u->isClone();})){ASSERT_EQ(projectedClone,nullptr);projectedClone=unit;}
 ASSERT_NE(projectedClone,nullptr);const auto * cloneState=dynamic_cast<const battle::CUnitState *>(projectedClone);ASSERT_NE(cloneState,nullptr);
 EXPECT_EQ(cloneState->shots.available(),cloneState->shots.total());EXPECT_EQ(cloneState->shots.total(),totalShots);
 EXPECT_TRUE(AIIntegration::fheroes2IsArcher(*projectedClone));EXPECT_TRUE(projectedClone->isShooter());EXPECT_EQ(projectedClone->getCount(),owner->getCount());
 const auto * projectedOwner=dynamic_cast<const battle::CUnitState *>(preview->battleGetUnitByID(owner->unitId()));ASSERT_NE(projectedOwner,nullptr);EXPECT_EQ(projectedOwner->shots.available(),remaining);
 const auto action=AIIntegration::FHeroes2SpellPolicy::choose(view,environment.get());ASSERT_TRUE(action);EXPECT_EQ(action->spell,SpellID::CLONE);
 const auto * expected=GetParam()?alternative:owner;
 ASSERT_EQ(view->battleGetUnitByPos(action->target.front().hexValue),expected)<<"Pinned Mirror Image values the original remaining ammunition";
 EXPECT_EQ(owner->shots.available(),remaining);EXPECT_EQ(owner->getAvailableHealth(),health);EXPECT_LT(owner->cloneID,0);EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(server.casts.size(),packets);EXPECT_EQ(battle()->stacks.size(),count);
 const auto * selected=dynamic_cast<const CStack *>(view->battleGetUnitByPos(action->target.front().hexValue));ASSERT_NE(selected,nullptr);ASSERT_TRUE(castOn(attackerSideHero,SpellID::CLONE,selected));
 EXPECT_EQ(server.casts.size(),packets+1);EXPECT_LT(attackerSideHero->mana,mana);ASSERT_GE(selected->cloneID,0);
 const auto * realClone=dynamic_cast<const CStack *>(battle()->battleGetUnitByID(selected->cloneID));ASSERT_NE(realClone,nullptr);EXPECT_TRUE(realClone->alive());EXPECT_TRUE(realClone->isClone());
 EXPECT_EQ(realClone->shots.available(),totalShots);EXPECT_EQ(realClone->shots.total(),totalShots);EXPECT_TRUE(AIIntegration::fheroes2IsArcher(*realClone));EXPECT_EQ(realClone->getCount(),selected->getCount());
 EXPECT_EQ(owner->shots.available(),remaining);EXPECT_EQ(owner->getAvailableHealth(),health);
}
INSTANTIATE_TEST_SUITE_P(OriginalAmmo,FHeroes2CloneAmmoBattle,::testing::Bool(),[](const auto & info){return info.param?"Exhausted":"PartiallySpent";});
