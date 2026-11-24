/* GPL-2.0-or-later. HoMM3 bounded moat approach on native VCMI geometry. */
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
#include <vcmi/Environment.h>

namespace
{
class HoMM3MoatCallback final : public CBattleCallback
{
public:
	explicit HoMM3MoatCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
	std::optional<BattleAction> action;
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
class HoMM3MoatEnvironment final : public Environment
{
	std::shared_ptr<CCallback> gameCallback;
	std::shared_ptr<HoMM3MoatCallback> callback;
public:
	HoMM3MoatEnvironment(std::shared_ptr<CCallback> game, std::shared_ptr<HoMM3MoatCallback> cb)
		: gameCallback(std::move(game)), callback(std::move(cb)) {}
	const Services * services() const override { return LIBRARY; }
	const GameCb * game() const override { return gameCallback.get(); }
	const BattleCb * battle(const BattleID & id) const override { return callback->getBattle(id).get(); }
};
}

class HoMM3MoatApproach : public BattleTestFixture, public ::testing::WithParamInterface<bool>
{
protected:
	bool keepAlive = false;
	CStack * actor = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<HoMM3MoatCallback> callback;
	std::shared_ptr<CPlayerBattleCallback> view;
	void prepare(int origin, int target, bool wide, int wideCount = 10)
	{
		startGame(); ASSERT_FALSE(HasFatalFailure());
		const auto * town = findFirst<CGTownInstance>(); ASSERT_NE(town,nullptr);
		if(!town->hasBuilt(BuildingID::FORT)) { ASSERT_TRUE(gameHandler->buildStructure(town->id,BuildingID::FORT,true)); }
		ASSERT_TRUE(gameHandler->buildStructure(town->id,BuildingID::CITADEL,true));
		ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id,SlotID(0)),CreatureID(wide?10:6).toCreature()));
		attackerSideHero->setStackCount(SlotID(0),wide?wideCount:40);
		ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(town->id,SlotID(0)),CreatureID(0).toCreature(),300));
		BattleLayout layout;
		layout.units[BattleSide::ATTACKER][0]=BattleHex(origin);
		layout.units[BattleSide::DEFENDER][0]=BattleHex(target);
		gameHandler->battles->startBattle(attackerSideHero,town,town->visitablePos(),attackerSideHero,nullptr,layout,town);
		if(!keepAlive)
		{
			CatapultAttack tower; tower.battleID=battle()->getBattleID(); tower.attackedPart=EWallPart::KEEP; tower.damageDealt=100;
			tower.destinationTile=battle()->wallPartToBattleHex(EWallPart::KEEP).toInt();
			for(const auto & unit:battle()->stacks)
				if(unit->isTurret() && unit->getPosition()==BattleHex(BattleHex::CASTLE_CENTRAL_TOWER))
					tower.killedTowerShooter=unit->unitId();
			ASSERT_NE(tower.killedTowerShooter,-1);
			gameHandler->sendAndApply(tower);
			ASSERT_EQ(battle()->battleGetWallState(EWallPart::KEEP),EWallState::DESTROYED);
		}
		const auto liveTowers=std::count_if(battle()->stacks.begin(),battle()->stacks.end(),[](const auto & unit){return unit->isTurret() && unit->alive() && !unit->isGhost();});
		ASSERT_EQ(liveTowers,keepAlive?1:0);
		RecordProperty("live_towers",static_cast<int>(liveTowers));
		ASSERT_FALSE(battle()->obstacles.empty());
		for(const auto & unit:battle()->stacks) if(unit->getPosition().isValid() && unit->alive()) (unit->unitSide()==BattleSide::ATTACKER?actor:enemy)=unit.get();
		ASSERT_NE(actor,nullptr); ASSERT_NE(enemy,nullptr);
		ASSERT_FALSE(actor->isCatapult()); ASSERT_FALSE(enemy->isTurret());
		ASSERT_EQ(actor->doubleWide(),wide); ASSERT_FALSE(actor->hasBonusOfType(BonusType::FLYING));
		ASSERT_EQ(actor->getPosition(),BattleHex(origin)); ASSERT_EQ(enemy->getPosition(),BattleHex(target));
		ASSERT_GT(actor->getMovementRange(),enemy->getMovementRange());
		RecordProperty("actor_speed",actor->getMovementRange());RecordProperty("enemy_speed",enemy->getMovementRange());
		callback=std::make_shared<HoMM3MoatCallback>(actor->unitOwner()); callback->onBattleStarted(battle());
		view=callback->getBattle(battle()->getBattleID());
	}
	void activate()
	{
		ASSERT_FALSE(HasFatalFailure()); ASSERT_NE(actor,nullptr);

		ASSERT_EQ(battle()->activeStack,actor->unitId());
		std::vector<battle::Units> order; view->battleGetTurnOrder(order,2,1);
		ASSERT_FALSE(order.empty());  ASSERT_EQ(order.front().front(),actor);
	}
	void choose()
	{
		auto policy=AIFactory::createBattleAI("HoMM3OriginalTactics"); ASSERT_NE(policy,nullptr);
		const auto player=view->battleGetOwner(actor);
		auto environment=std::make_shared<HoMM3MoatEnvironment>(makeCallback(player),callback);
		AutocombatPreferences prefs; prefs.enableSpellsUsage=false;
		policy->playerID=player; policy->initBattleInterface(environment,callback,prefs);
		policy->battleStart(battle()->getBattleID(),battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,nullptr,view->playerToSide(player),false);
		callback->action.reset(); policy->activeStack(battle()->getBattleID(),actor); ASSERT_TRUE(callback->action);
	}
	void nextActorTurn()
	{
		for(int i=0;i<8 && battle()->battleActiveUnit()!=actor;++i)
		{
			const auto * active=battle()->battleActiveUnit(); ASSERT_NE(active,nullptr);
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),battle()->battleGetOwner(active),BattleAction::makeDefend(active)));
		}
		ASSERT_EQ(battle()->battleActiveUnit(),actor);
	}
	void breakUpperWall()
	{
		CatapultAttack wall; wall.battleID=battle()->getBattleID();wall.attackedPart=EWallPart::UPPER_WALL;wall.damageDealt=100;wall.destinationTile=view->wallPartToBattleHex(EWallPart::UPPER_WALL).toInt();gameHandler->sendAndApply(wall);
		ASSERT_EQ(view->battleGetWallState(EWallPart::UPPER_WALL),EWallState::DESTROYED);
	}
	void submit()
	{
		ASSERT_TRUE(callback->action);
		RecordProperty("last_action",callback->action->toString());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),actor->unitOwner(),*callback->action));
	}
	void breach(int target)
	{
		prepare(26,target,GetParam()); ASSERT_FALSE(HasFatalFailure());
		breakUpperWall(); activate(); ASSERT_FALSE(HasFatalFailure());
		ASSERT_EQ(battle()->obstacles.size(),1u);
		ASSERT_EQ(battle()->obstacles.front()->obstacleType,CObstacleInstance::MOAT);
		ASSERT_TRUE(battle()->obstacles.front()->getStoppingTile().contains(BattleHex(28)));
		ASSERT_FALSE(view->getReachability(actor).isReachable(BattleHex(29)));
		const auto health=actor->getAvailableHealth(); const auto enemyHealth=enemy->getAvailableHealth();
		choose(); ASSERT_FALSE(HasFatalFailure());
		ASSERT_EQ(callback->action->actionType,EActionType::WALK);
		ASSERT_FALSE(callback->action->target.empty());
		EXPECT_EQ(callback->action->target.front().hexValue,BattleHex(28));
		const auto selected=callback->action->target.front().hexValue;
		submit(); ASSERT_FALSE(HasFatalFailure());
		EXPECT_EQ(actor->getPosition(),selected);
		if(GetParam()) { EXPECT_EQ(actor->occupiedHex(),BattleHex(selected.toInt()-1)); }
		if(keepAlive) {EXPECT_LE(actor->getAvailableHealth(),health-70);}
		else {EXPECT_EQ(actor->getAvailableHealth(),health-70);}
		EXPECT_EQ(enemy->getAvailableHealth(),enemyHealth);
		ASSERT_EQ(server.castsOf(spellByName("core:castleMoatTrigger")).size(),1u);
		EXPECT_EQ(server.castsOf(spellByName("core:castleMoatTrigger")).front().damage,70);
		RecordProperty("first_head",actor->getPosition().toInt());RecordProperty("first_tail",actor->occupiedHex().toInt());
		nextActorTurn(); ASSERT_FALSE(HasFatalFailure());
		const auto reach=view->getReachability(actor);
		ASSERT_TRUE(reach.isReachable(BattleHex(29)));
		choose(); ASSERT_FALSE(HasFatalFailure());
		ASSERT_EQ(callback->action->actionType,EActionType::WALK_AND_ATTACK);
		const BattleHex landing(target==46?(GetParam()?30:29):(GetParam()?47:46));
		ASSERT_EQ(callback->action->target.front().hexValue,landing);
		if(GetParam() && target==46)
		{
			EXPECT_EQ(reach.predecessors[30],BattleHex(29));
			EXPECT_EQ(actor->occupiedHex(BattleHex(29)),BattleHex(28));
			EXPECT_EQ(actor->occupiedHex(landing),BattleHex(29));
		}
		submit(); ASSERT_FALSE(HasFatalFailure());
		EXPECT_EQ(actor->getPosition(),landing);
		if(GetParam()) { EXPECT_EQ(actor->occupiedHex(),BattleHex(landing.toInt()-1)); }
		EXPECT_LT(enemy->getAvailableHealth(),enemyHealth);
		RecordProperty("moat_triggers",static_cast<int>(server.castsOf(spellByName("core:castleMoatTrigger")).size()));
		RecordProperty("attack_head",actor->getPosition().toInt());RecordProperty("attack_tail",actor->occupiedHex().toInt());
	}
};

TEST_P(HoMM3MoatApproach, breachedWallEnablesEntryAndFollowingAttack)
{
 breach(46);
}
TEST_P(HoMM3MoatApproach, intactWallDoesNotInventCrossing)
{
 prepare(26,46,GetParam()); activate(); ASSERT_FALSE(HasFatalFailure());
 ASSERT_EQ(view->battleGetWallState(EWallPart::UPPER_WALL),EWallState::INTACT);
 const auto origin=actor->getPosition(); const auto health=enemy->getAvailableHealth();
 choose(); ASSERT_FALSE(HasFatalFailure());
 ASSERT_EQ(callback->action->actionType,EActionType::DEFEND);
 submit(); ASSERT_FALSE(HasFatalFailure());
 EXPECT_EQ(actor->getPosition(),origin); EXPECT_EQ(enemy->getAvailableHealth(),health);
 EXPECT_TRUE(server.castsOf(spellByName("core:castleMoatTrigger")).empty());
}
TEST_P(HoMM3MoatApproach, openGateKeepsExistingDirectAttack)
{
 prepare(93,97,GetParam()); activate(); ASSERT_FALSE(HasFatalFailure());
 BattleUpdateGateState gate; gate.battleID=battle()->getBattleID(); gate.state=EGateState::OPENED;
 gameHandler->sendAndApply(gate); ASSERT_EQ(view->battleGetGateState(),EGateState::OPENED);
 const auto enemyHealth=enemy->getAvailableHealth();
 choose(); ASSERT_FALSE(HasFatalFailure());
 ASSERT_EQ(callback->action->actionType,EActionType::WALK_AND_ATTACK);
 submit(); ASSERT_FALSE(HasFatalFailure());
 EXPECT_LT(enemy->getAvailableHealth(),enemyHealth);
 EXPECT_TRUE(server.castsOf(spellByName("core:castleMoatTrigger")).empty());
}
INSTANTIATE_TEST_SUITE_P(Footprint, HoMM3MoatApproach, ::testing::Bool(),
 [](const ::testing::TestParamInfo<bool> & p){return p.param?"Wide":"Single";});
