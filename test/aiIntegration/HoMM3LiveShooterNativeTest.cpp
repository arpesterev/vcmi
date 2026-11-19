/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/HoMM3TacticsBattleAI.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../server/CGameHandler.h"
namespace {
namespace F=HoMM3Forecast;
class LiveDecisionCallback final : public CBattleCallback
{
public:
 explicit LiveDecisionCallback(PlayerColor p):CBattleCallback(p,nullptr){}
 std::optional<BattleAction> action;int units=0,spells=0;
 void battleMakeUnitAction(const BattleID &,const BattleAction &a) override{++units;action=a;}
 void battleMakeSpellAction(const BattleID &,const BattleAction &a) override{++spells;action=a;}
};
class LiveEnvironment final : public Environment
{
 std::shared_ptr<CCallback> gameCb;std::shared_ptr<LiveDecisionCallback> battleCb;
public:
 LiveEnvironment(std::shared_ptr<CCallback> g,std::shared_ptr<LiveDecisionCallback>b):gameCb(std::move(g)),battleCb(std::move(b)){}
 const Services * services()const override{return LIBRARY;}
 const GameCb * game()const override{return gameCb.get();}
 const BattleCb * battle(const BattleID&id)const override{return battleCb->getBattle(id).get();}
};
class HoMM3LiveShooterNative : public BattleTestFixture
{
protected:
 std::shared_ptr<LiveDecisionCallback> callback;std::shared_ptr<LiveEnvironment> environment;
 std::shared_ptr<CPlayerBattleCallback> view;std::unique_ptr<F::NativeLifecycle> life;
 std::unique_ptr<AIIntegration::HoMM3TacticsBattleAI> controller;
 CStack *actor=nullptr;std::array<CStack*,2> enemies{};
 void prepare(bool mixed=false,bool spells=false)
 {
  startGame();ASSERT_FALSE(HasFatalFailure());
  for(int side=0;side<2;++side){auto*h=side?defenderSideHero:attackerSideHero;
   h->setPrimarySkill(PrimarySkill::ATTACK,0,ChangeValueMode::ABSOLUTE);h->setPrimarySkill(PrimarySkill::DEFENSE,0,ChangeValueMode::ABSOLUTE);
   const auto type=creatureByName(side?(mixed?"core:pikeman":"core:woodElf"):"core:archer");
   ASSERT_TRUE(gameHandler->changeStackType(StackLocation(h->id,SlotID(0)),type.toCreature()));
   ASSERT_TRUE(gameHandler->changeStackCount(StackLocation(h->id,SlotID(0)),side?40:1,ChangeValueMode::ABSOLUTE));
   if(side)
   {
    ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(h->id,SlotID(1)),type.toCreature(),40));
   }
  }
  startBattle();ASSERT_FALSE(HasFatalFailure());battle()->obstacles.clear();
  for(const auto &u:battle()->stacks){if(u->unitSide()==BattleSide::ATTACKER){actor=u.get();u->setPosition(BattleHex(87));}
   else{enemies.at(u->unitSlot().getNum())=u.get();u->setPosition(BattleHex(82+17*u->unitSlot().getNum()));}}
  ASSERT_NE(actor,nullptr);const auto id=battle()->getBattleID();const auto player=attackerSideHero->getOwner();
  callback=std::make_shared<LiveDecisionCallback>(player);callback->onBattleStarted(battle());view=callback->getBattle(id);
  environment=std::make_shared<LiveEnvironment>(makeCallback(player),callback);
  life=std::make_unique<F::NativeLifecycle>(*view);life->attach(id);
  controller=std::make_unique<AIIntegration::HoMM3TacticsBattleAI>();controller->playerID=player;
  AutocombatPreferences preferences;preferences.enableSpellsUsage=spells;controller->initBattleInterface(environment,callback,preferences);
  controller->battleStart(id,nullptr,nullptr,int3(),nullptr,nullptr,BattleSide::ATTACKER,false);
  battle()->nextRound();life->battleNewRound(id);controller->battleNewRound(id);
  battle()->activeStack=actor->unitId();life->ownActivation(id);
 }
 void choose(){controller->activeStack(battle()->getBattleID(),actor);}
 void matches(const BattleAction &expected)
 {ASSERT_TRUE(callback->action);EXPECT_EQ(callback->action->toString(),expected.toString());EXPECT_EQ(callback->units,1);EXPECT_EQ(callback->spells,0);}
};
}
TEST_F(HoMM3LiveShooterNative, ActualActiveIsNotForecastFirstActor)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 const auto policy=F::captureNativeShooterPolicy(*view,environment.get());ASSERT_TRUE(policy);
 const auto facts=F::captureNativeOrdinaryFacts(*view,*life,*policy,F::Side::ATTACKER,false);ASSERT_TRUE(facts);
 const auto forecast=F::simulateOrdinaryTwoPass(*facts);ASSERT_TRUE(forecast);ASSERT_FALSE(forecast->order.empty());
 ASSERT_NE(forecast->order.front(),actor->unitId());
 const auto shot=F::chooseNativeOrdinaryShooter(*view,*life,environment.get(),*actor);ASSERT_TRUE(shot);
 const auto legacy=AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view,*actor);
 ASSERT_FALSE(shot->target.empty());ASSERT_FALSE(legacy.target.empty());
 EXPECT_NE(shot->target.front().hexValue,legacy.target.front().hexValue)<<"fixture must expose source-vs-legacy tie behavior";
 EXPECT_EQ(shot->stackNumber,actor->unitId());choose();matches(*shot);
}
TEST_F(HoMM3LiveShooterNative, EasyActualHumanOwnershipControlsKillsOnly)
{
 prepare();ASSERT_FALSE(HasFatalFailure());gameState()->getStartInfo()->difficulty=0;
 auto &settings=gameState()->getStartInfo()->playerInfos.at(attackerSideHero->getOwner());
 for(bool human:{false,true}){
  settings.connectedPlayerIDs.clear();if(human)settings.connectedPlayerIDs.insert(PlayerConnectionID::FIRST_HUMAN);
  const auto policy=F::captureNativeShooterPolicy(*view,environment.get());ASSERT_TRUE(policy);EXPECT_EQ(policy->difficulty,0);EXPECT_EQ(policy->human[0],human);
  const auto facts=F::captureNativeOrdinaryFacts(*view,*life,*policy,F::Side::ATTACKER,false);ASSERT_TRUE(facts);
  const auto decision=F::chooseCapturedOrdinaryShooter(*facts,actor->unitId());ASSERT_TRUE(decision);EXPECT_EQ(decision->source.killsOnly,human);
  const auto shot=F::chooseNativeOrdinaryShooter(*view,*life,environment.get(),*actor);ASSERT_TRUE(shot);
  callback->action.reset();callback->units=callback->spells=0;choose();matches(*shot);
 }
}
TEST_F(HoMM3LiveShooterNative, UnsupportedMixedRosterUsesExistingChooser)
{
 prepare(true);ASSERT_FALSE(HasFatalFailure());
 ASSERT_FALSE(F::chooseNativeOrdinaryShooter(*view,*life,environment.get(),*actor));
 const auto expected=AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view,*actor);choose();matches(expected);
}
TEST_F(HoMM3LiveShooterNative, SpellPrecedesAdmittedExactShot)
{
 prepare(false,true);ASSERT_FALSE(HasFatalFailure());
 teachSpell(attackerSideHero,SpellID::MAGIC_ARROW);attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,20,ChangeValueMode::ABSOLUTE);attackerSideHero->mana=100;
 ASSERT_TRUE(F::chooseNativeOrdinaryShooter(*view,*life,environment.get(),*actor));
 const auto spell=AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*actor);ASSERT_TRUE(spell);
 choose();ASSERT_TRUE(callback->action);EXPECT_EQ(callback->spells,1);EXPECT_EQ(callback->units,0);EXPECT_EQ(callback->action->spell,spell->spell);
}
