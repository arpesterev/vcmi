/* GPL-2.0-or-later. Pinned friendly-secondary penalty on native breath geometry. */
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
class BreathDecisionCallback final : public CBattleCallback
{
public:
	explicit BreathDecisionCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
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
class BreathEnvironment final : public Environment
{
	std::shared_ptr<CCallback> gameCallback;
	std::shared_ptr<BreathDecisionCallback> callback;
public:
	BreathEnvironment(std::shared_ptr<CCallback> game, std::shared_ptr<BreathDecisionCallback> cb)
		: gameCallback(std::move(game)), callback(std::move(cb)) {}
	const Services * services() const override { return LIBRARY; }
	const GameCb * game() const override { return gameCallback.get(); }
	const BattleCb * battle(const BattleID & id) const override { return callback->getBattle(id).get(); }
};
}


class FHeroes2BreathPriority : public BattleTestFixture, public ::testing::WithParamInterface<bool>
{
protected:
	CStack * actor=nullptr;
	CStack * risky=nullptr;
	CStack * safe=nullptr;
	CStack * secondary=nullptr;
	std::shared_ptr<BreathDecisionCallback> callback;
	std::shared_ptr<CPlayerBattleCallback> view;
	void prepare(bool friendly)
	{
		startGame();ASSERT_FALSE(HasFatalFailure());
		ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id,SlotID(0)),CreatureID(6).toCreature()));
		attackerSideHero->setStackCount(SlotID(0),100);
		ASSERT_TRUE(gameHandler->changeStackType(StackLocation(defenderSideHero->id,SlotID(0)),CreatureID(2).toCreature()));
		defenderSideHero->setStackCount(SlotID(0),100);
		BattleLayout layout;layout.units[BattleSide::ATTACKER][0]=BattleHex(90);layout.units[BattleSide::DEFENDER][0]=BattleHex(91);
		gameHandler->battles->startBattle(attackerSideHero,defenderSideHero,attackerSideHero->visitablePos(),attackerSideHero,defenderSideHero,layout,nullptr);
		for(const auto & unit:battle()->stacks) if(unit->alive() && unit->getPosition().isValid()) (unit->unitSide()==BattleSide::ATTACKER?actor:risky)=unit.get();
		ASSERT_NE(actor,nullptr);ASSERT_NE(risky,nullptr);ASSERT_EQ(battle()->battleActiveUnit(),actor);
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,-static_cast<int>(actor->getMovementRange()),BonusSourceID()));
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::TWO_HEX_ATTACK_BREATH,BonusSource::OTHER,1,BonusSourceID()));
		ASSERT_EQ(actor->getMovementRange(),0);
		safe=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(73),100);
		secondary=addStack(friendly?BattleSide::ATTACKER:BattleSide::DEFENDER,CreatureID(0),BattleHex(92),50);
		ASSERT_NE(safe,nullptr);ASSERT_NE(secondary,nullptr);
		callback=std::make_shared<BreathDecisionCallback>(actor->unitOwner());callback->onBattleStarted(battle());view=callback->getBattle(battle()->getBattleID());
		const auto splash=view->getAttackedBattleUnits(actor,risky,risky->getPosition(),false,actor->getPosition());
		ASSERT_TRUE(std::find(splash.begin(),splash.end(),secondary)!=splash.end());
		const auto safeSplash=view->getAttackedBattleUnits(actor,safe,safe->getPosition(),false,actor->getPosition());
		ASSERT_TRUE(safeSplash.empty());
		ASSERT_EQ(battle()->battleActiveUnit(),actor);
	}
	void choose()
	{
		auto policy=AIFactory::createBattleAI("FHeroes2Tactics");ASSERT_NE(policy,nullptr);
		auto environment=std::make_shared<BreathEnvironment>(makeCallback(actor->unitOwner()),callback);
		AutocombatPreferences prefs;prefs.enableSpellsUsage=false;
		policy->playerID=actor->unitOwner();policy->initBattleInterface(environment,callback,prefs);
		policy->battleStart(battle()->getBattleID(),attackerSideHero,defenderSideHero,battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
		policy->activeStack(battle()->getBattleID(),actor);ASSERT_TRUE(callback->action);
	}
};
TEST_P(FHeroes2BreathPriority,secondaryOwnershipChangesActualPrimaryChoice)
{
	prepare(GetParam());ASSERT_FALSE(HasFatalFailure());
	const auto secondaryHP=secondary->getAvailableHealth();const auto safeHP=safe->getAvailableHealth();const auto riskyHP=risky->getAvailableHealth();
	choose();ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(callback->action->actionType,EActionType::WALK_AND_ATTACK);
	RecordProperty("chosen",callback->action->toString());
	const auto * expected=GetParam()?safe:risky;
	EXPECT_EQ(view->battleGetUnitByPos(callback->action->target.at(1).hexValue),expected);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),actor->unitOwner(),*callback->action));
	RecordProperty("secondary_damage",secondaryHP-secondary->getAvailableHealth());
	RecordProperty("safe_damage",safeHP-safe->getAvailableHealth());RecordProperty("risky_damage",riskyHP-risky->getAvailableHealth());
	EXPECT_EQ(actor->getPosition(),BattleHex(90));
	if(GetParam()) {EXPECT_EQ(secondary->getAvailableHealth(),secondaryHP);EXPECT_LT(safe->getAvailableHealth(),safeHP);}
	else {EXPECT_LT(secondary->getAvailableHealth(),secondaryHP);EXPECT_LT(risky->getAvailableHealth(),riskyHP);}
}
INSTANTIATE_TEST_SUITE_P(Secondary,FHeroes2BreathPriority,::testing::Bool(),[](const auto & info){return info.param?"Friendly":"Hostile";});
