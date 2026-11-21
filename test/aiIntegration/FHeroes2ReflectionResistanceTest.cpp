/* GPL-2.0-or-later. Native reflection/resistance availability boundary. */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "lib/GameLibrary.h"
#include "lib/battle/BattleAction.h"
#include "lib/battle/BattleLayout.h"
#include "lib/battle/CObstacleInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/modding/IdentifierStorage.h"
#include "lib/modding/ModScope.h"
#include "lib/battle/CPlayerBattleCallback.h"
#include "lib/callback/AIFactory.h"
#include "lib/callback/CBattleCallback.h"
#include "lib/callback/CBattleGameInterface.h"
#include "lib/callback/CCallback.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/networkPacks/StackLocation.h"
#include "server/CGameHandler.h"
#include "server/battles/BattleProcessor.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "lib/spells/ISpellMechanics.h"
#include "lib/spells/CSpellHandler.h"
#include "lib/callback/GameRandomizer.h"
#include <vcmi/Environment.h>

namespace
{
class ReflectionDecisionCallback final : public CBattleCallback
{
public:
	explicit ReflectionDecisionCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
	std::optional<BattleAction> action;
	bool spellAction=false;
	void battleMakeSpellAction(const BattleID &, const BattleAction & value) override { action=value;spellAction=true; }
	void battleMakeUnitAction(const BattleID &, const BattleAction & value) override
	{
		if(action) throw std::logic_error("Duplicate siege action");
		action = value;
	}
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &, const BattleStateInfoForRetreat &) override
	{
		return std::nullopt;
	}
};
class ReflectionEnvironment final : public Environment
{
	std::shared_ptr<CCallback> gameCallback;
	std::shared_ptr<ReflectionDecisionCallback> callback;
public:
	ReflectionEnvironment(std::shared_ptr<CCallback> game, std::shared_ptr<ReflectionDecisionCallback> cb)
		: gameCallback(std::move(game)), callback(std::move(cb)) {}
	const Services * services() const override { return LIBRARY; }
	const GameCb * game() const override { return gameCallback.get(); }
	const BattleCb * battle(const BattleID & id) const override { return callback->getBattle(id).get(); }
};
}


class FHeroes2ReflectionResistance : public BattleTestFixture, public ::testing::WithParamInterface<int>
{
protected:
	CStack * actor=nullptr;
	CStack * enemy=nullptr;
	std::shared_ptr<ReflectionDecisionCallback> callback;
	std::shared_ptr<CPlayerBattleCallback> view;
	std::shared_ptr<ReflectionEnvironment> environment;
	void prepare(int mirror)
	{
		startGame();ASSERT_FALSE(HasFatalFailure());
		ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id,SlotID(0)),CreatureID(6).toCreature()));
		attackerSideHero->setStackCount(SlotID(0),40);
		ASSERT_TRUE(gameHandler->changeStackType(StackLocation(defenderSideHero->id,SlotID(0)),CreatureID(0).toCreature()));
		defenderSideHero->setStackCount(SlotID(0),300);
		BattleLayout layout;layout.units[BattleSide::ATTACKER][0]=BattleHex(52);layout.units[BattleSide::DEFENDER][0]=BattleHex(64);
		gameHandler->battles->startBattle(attackerSideHero,defenderSideHero,attackerSideHero->visitablePos(),attackerSideHero,defenderSideHero,layout,nullptr);
		for(const auto & unit:battle()->stacks) if(unit->alive() && unit->getPosition().isValid()) (unit->unitSide()==BattleSide::ATTACKER?actor:enemy)=unit.get();
		ASSERT_NE(actor,nullptr);ASSERT_NE(enemy,nullptr);ASSERT_EQ(battle()->battleActiveUnit(),actor);
		for(auto * unit:{actor,enemy}) unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_RESISTANCE,BonusSource::OTHER,50,BonusSourceID()));
		ASSERT_EQ(actor->magicResistance(),50);ASSERT_EQ(enemy->magicResistance(),50);
		if(mirror>0) enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,mirror,BonusSourceID()));
		for(const auto & spell:LIBRARY->spellh->objects) attackerSideHero->removeSpellFromSpellbook(spell->id);
		teachSpell(attackerSideHero,SpellID::MAGIC_ARROW);setMagicMastery(attackerSideHero,MasteryLevel::BASIC);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,30,ChangeValueMode::ABSOLUTE);
		callback=std::make_shared<ReflectionDecisionCallback>(actor->unitOwner());callback->onBattleStarted(battle());view=callback->getBattle(battle()->getBattleID());
		environment=std::make_shared<ReflectionEnvironment>(makeCallback(actor->unitOwner()),callback);
	}
	void choose()
	{
		auto policy=AIFactory::createBattleAI("FHeroes2Tactics");ASSERT_NE(policy,nullptr);
		AutocombatPreferences prefs;prefs.enableSpellsUsage=true;
		policy->playerID=actor->unitOwner();policy->initBattleInterface(environment,callback,prefs);
		policy->battleStart(battle()->getBattleID(),attackerSideHero,defenderSideHero,battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
		policy->activeStack(battle()->getBattleID(),actor);ASSERT_TRUE(callback->action);
	}
};
TEST_P(FHeroes2ReflectionResistance,publicChoicePreservesConservativeMirrorBoundary)
{
	prepare(GetParam());ASSERT_FALSE(HasFatalFailure());
	const auto mana=attackerSideHero->mana;const auto casts=server.casts.size();
	choose();ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(callback->spellAction,GetParam()==0);
	EXPECT_EQ(server.casts.size(),casts);EXPECT_EQ(attackerSideHero->mana,mana);
	if(GetParam()==0) {EXPECT_EQ(callback->action->spell,SpellID::MAGIC_ARROW);}
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),actor->unitOwner(),*callback->action));
	if(GetParam()==0) {EXPECT_EQ(server.casts.size(),casts+1);EXPECT_EQ(attackerSideHero->mana,mana-view->battleGetSpellCost(SpellID(SpellID::MAGIC_ARROW).toSpell(),attackerSideHero));}
	else {EXPECT_EQ(server.casts.size(),casts);EXPECT_EQ(attackerSideHero->mana,mana);ASSERT_EQ(callback->action->actionType,EActionType::WALK);EXPECT_EQ(actor->getPosition(),callback->action->target.front().hexValue);}
}
INSTANTIATE_TEST_SUITE_P(Mirror,FHeroes2ReflectionResistance,::testing::Values(0,50,100));

class FHeroes2ReflectionOutcomes : public FHeroes2ReflectionResistance {};
TEST_P(FHeroes2ReflectionOutcomes,nativeCastSeparatesReflectionFromDeterministicPreview)
{
	prepare(50);ASSERT_FALSE(HasFatalFailure());
	const auto mana=attackerSideHero->mana;const auto ownHP=actor->getAvailableHealth(),enemyHP=enemy->getAvailableHealth();const auto packets=server.casts.size();
	auto preview=std::make_shared<HypotheticBattle>(environment.get(),view);
	spells::BattleCast cast(preview.get(),attackerSideHero,spells::Mode::HERO,SpellID(SpellID::MAGIC_ARROW).toSpell());
	cast.castEval(preview->getServerCallback(),spells::Target{battle::Destination(enemy)});
	EXPECT_EQ(preview->battleGetUnitByID(enemy->unitId())->getAvailableHealth(),enemyHP-310);
	EXPECT_EQ(preview->battleGetUnitByID(actor->unitId())->getAvailableHealth(),ownHP);
	EXPECT_EQ(actor->getAvailableHealth(),ownHP);EXPECT_EQ(enemy->getAvailableHealth(),enemyHP);EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(server.casts.size(),packets);
	// This forced legal cast diagnoses the engine; the guarded AI never selects it.
	gameHandler->randomizer->setSeed(GetParam());
	const auto action=BattleAction::makeHeroSpellcast(BattleSide::ATTACKER,battle::Target{battle::Destination(enemy)},SpellID::MAGIC_ARROW);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),actor->unitOwner(),action));
	ASSERT_EQ(server.casts.size(),packets+1);EXPECT_EQ(attackerSideHero->mana,mana-view->battleGetSpellCost(SpellID(SpellID::MAGIC_ARROW).toSpell(),attackerSideHero));
	const auto & packet=server.casts.back().announcement;
	EXPECT_EQ(packet.reflectedCres.size(),GetParam()==0?0u:1u);
	EXPECT_EQ(packet.resistedCres.size(),GetParam()==4?0u:1u);
	RecordProperty("seed",GetParam());
	RecordProperty("mana_spent",mana-attackerSideHero->mana);
	RecordProperty("reflected",static_cast<int>(packet.reflectedCres.size()));RecordProperty("resisted",static_cast<int>(packet.resistedCres.size()));
	RecordProperty("own_damage",ownHP-actor->getAvailableHealth());RecordProperty("enemy_damage",enemyHP-enemy->getAvailableHealth());
	if(packet.reflectedCres.empty()) {EXPECT_EQ(actor->getAvailableHealth(),ownHP);}
	else {EXPECT_TRUE(packet.reflectedCres.contains(enemy->unitId()));EXPECT_EQ(enemy->getAvailableHealth(),enemyHP);}
	const auto * recipient=packet.reflectedCres.empty()?enemy:actor;const auto original=packet.reflectedCres.empty()?enemyHP:ownHP;
	if(packet.resistedCres.contains(recipient->unitId())) {EXPECT_EQ(recipient->getAvailableHealth(),original);}
	else {EXPECT_EQ(recipient->getAvailableHealth(),original-310);}
}
INSTANTIATE_TEST_SUITE_P(Seed,FHeroes2ReflectionOutcomes,::testing::Values(0,2,4));
