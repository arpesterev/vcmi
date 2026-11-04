/*
 * NativePortBattleTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 */
#include "StdInc.h"

#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/HoMM3TacticsBattleAI.h"
#include <vcmi/Environment.h>
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/callback/AIFactory.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/CBattleGameInterface.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/entities/building/TownFortifications.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"

namespace
{
class DecisionCallback final : public CBattleCallback
{
public:
	explicit DecisionCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
	std::optional<BattleAction> action;
	bool spellAction = false;

	void battleMakeUnitAction(const BattleID &, const BattleAction & value) override
	{
		if(action)
			throw std::logic_error("Backend submitted two commands for one activation");
		spellAction = false;
		action = value;
	}
	void battleMakeTacticAction(const BattleID &, const BattleAction & value) override { action = value; }
	void battleMakeSpellAction(const BattleID &, const BattleAction & value) override
	{
		if(action)
			throw std::logic_error("Backend submitted a unit command before spell acknowledgement");
		spellAction = true;
		action = value;
	}
};

class NativePortEnvironment final : public Environment
{
	std::shared_ptr<CCallback> gameCallback;
	std::shared_ptr<DecisionCallback> battleCallback;
public:
	NativePortEnvironment(std::shared_ptr<CCallback> game, std::shared_ptr<DecisionCallback> battle)
		: gameCallback(std::move(game)), battleCallback(std::move(battle)) {}
	const Services * services() const override { return LIBRARY; }
	const GameCb * game() const override { return gameCallback.get(); }
	const BattleCb * battle(const BattleID & battleID) const override { return battleCallback->getBattle(battleID).get(); }
};

class NativeBattleRecorder final : public RecordingGameServer
{
public:
	int64_t damage = 0;
	int shots = 0;
	int meleeAttacks = 0;
	int acceptedResults = 0;

	void applyPack(CPackForClient & pack) override
	{
		RecordingGameServer::applyPack(pack);
		if(const auto * attack = dynamic_cast<const BattleAttack *>(&pack))
		{
			if(attack->shot())
				++shots;
			else
				++meleeAttacks;
			for(const auto & injury : attack->bsa)
				damage += injury.damageAmount;
		}
		if(const auto * injuries = dynamic_cast<const StacksInjured *>(&pack))
		{
			for(const auto & injury : injuries->stacks)
				damage += injury.damageAmount;
		}
		if(dynamic_cast<const BattleResultAccepted *>(&pack))
			++acceptedResults;
	}
};
}

class AIIntegrationNativeBattle : public BattleTestFixture
{
protected:
	NativeBattleRecorder recorder;
	int ballistaDecisions = 0;
	int ballistaShots = 0;
	int ballistaDefendsWhileAllEnemiesBlind = 0;
	int heroSpellDecisions = 0;

	void startRecordedGame()
	{
		startGame();
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		gameHandler.reset();
		recorder.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(recorder, gameState());
		gameHandler->randomizer->setSeed(seed);
	}

	void setArmy(const CArmedInstance * army, CreatureID type, int count)
	{
		const StackLocation slot(army->id, SlotID(0));
		ASSERT_TRUE(gameHandler->changeStackType(slot, type.toCreature()));
		ASSERT_TRUE(gameHandler->changeStackCount(slot, count, ChangeValueMode::ABSOLUTE));
	}

	void prepareBattle(CreatureID attackerType, int attackerCount, CreatureID defenderType, int defenderCount)
	{
		startRecordedGame();
		ASSERT_FALSE(HasFatalFailure());
		setArmy(attackerSideHero, attackerType, attackerCount);
		setArmy(defenderSideHero, defenderType, defenderCount);
		ASSERT_FALSE(HasFatalFailure());
		gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	}

	void teachBattleSpell(CGHeroInstance * hero, SpellID spell, int power = 6)
	{
		teachSpell(hero, spell);
		hero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
		hero->mana = 100;
	}

	void prepareSiege()
	{
		startRecordedGame();
		ASSERT_FALSE(HasFatalFailure());
		setArmy(attackerSideHero, CreatureID(10), 30);
		ASSERT_FALSE(HasFatalFailure());
		const auto * town = findFirst<CGTownInstance>();
		ASSERT_NE(town, nullptr);
		if(town->fortificationsLevel().wallsHealth <= 0)
		{
			ASSERT_TRUE(gameHandler->buildStructure(town->id, BuildingID::FORT, true));
		}
		ASSERT_GT(town->fortificationsLevel().wallsHealth, 0);
		ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(town->id, SlotID(0)), CreatureID(0).toCreature(), 30));
		ASSERT_NE(attackerSideHero->getArt(ArtifactPosition::MACH4), nullptr);
		const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, town);
		gameHandler->battles->startBattle(attackerSideHero, town, town->visitablePos(),
			attackerSideHero, nullptr, layout, town);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		ASSERT_TRUE(std::any_of(battle()->stacks.begin(), battle()->stacks.end(),
			[](const auto & unit) { return unit->isCatapult(); })) << "Siege setup did not create the attacking catapult";
	}

	void runToCompletion(std::optional<BattleSide> expectedWinner = std::nullopt, bool spellsEnabled = true,
		const std::string & backendName = "FHeroes2Tactics")
	{
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		const auto battleID = battle()->getBattleID();
		const auto attackerPlayer = battle()->sideToPlayer(BattleSide::ATTACKER);
		std::map<PlayerColor, std::shared_ptr<DecisionCallback>> callbacks;
		std::map<PlayerColor, std::shared_ptr<CBattleGameInterface>> policies;
		for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			const auto player = battle()->sideToPlayer(side);
			auto callback = std::make_shared<DecisionCallback>(player);
			callback->onBattleStarted(battle());
			auto policy = AIFactory::createBattleAI(backendName);
			ASSERT_NE(policy, nullptr);
			EXPECT_EQ(policy->dllName, backendName);
			policy->playerID = player;
			auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
			AutocombatPreferences prefs;
			prefs.enableSpellsUsage = spellsEnabled;
			policy->initBattleInterface(environment, callback, prefs);
			policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
				battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
				battle()->getSideHero(BattleSide::ATTACKER), battle()->getSideHero(BattleSide::DEFENDER), side, false);
			callbacks.emplace(player, callback);
			policies.emplace(player, policy);
		}

		if(battle()->tacticDistance > 0)
		{
			const auto player = battle()->sideToPlayer(battle()->tacticsSide);
			auto & callback = callbacks.at(player);
			policies.at(player)->yourTacticPhase(battleID, battle()->tacticDistance);
			ASSERT_TRUE(callback->action.has_value());
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
		}

		int decisions = 0;
		std::vector<std::string> recentDecisions;
		while(recorder.battleResults.empty() && decisions < 512)
		{
			ASSERT_FALSE(gameState()->currentBattles.empty());
			const auto * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
			ASSERT_NE(active, nullptr) << "Battle stopped without a result or an active stack";
			const auto player = battle()->battleGetOwner(active);
			auto & callback = callbacks.at(player);
			callback->action.reset();
			callback->spellAction = false;
			policies.at(player)->activeStack(battleID, active);
			ASSERT_TRUE(callback->action.has_value()) << "Backend did not respond to the actual active-stack event";
			const auto action = *callback->action;
			if(callback->spellAction)
				++heroSpellDecisions;
			else if(active->creatureId() == CreatureID::BALLISTA)
			{
				++ballistaDecisions;
				if(action.actionType == EActionType::SHOOT)
					++ballistaShots;
				if(action.actionType == EActionType::DEFEND)
				{
					bool sawEnemy = false;
					bool allEnemiesBlind = true;
					for(const auto & unit : battle()->stacks)
					{
						if(unit->alive() && unit->unitSide() != active->unitSide())
						{
							sawEnemy = true;
							allEnemiesBlind &= unit->hasBonus(Selector::source(
								BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLIND))));
						}
					}
					if(sawEnemy && allEnemiesBlind)
						++ballistaDefendsWhileAllEnemiesBlind;
				}
			}
			std::ostringstream trace;
			trace << "activation=" << decisions << " active=" << active->unitId() << " " << action.toString();
			for(const auto & unit : battle()->stacks)
			{
				if(unit->alive())
					trace << " [unit=" << unit->unitId() << " creature=" << unit->creatureIndex()
						<< " side=" << static_cast<int>(unit->unitSide()) << " hex=" << unit->getPosition().toInt()
						<< " hp=" << unit->getAvailableHealth() << "]";
			}
			recentDecisions.push_back(trace.str());
			if(recentDecisions.size() > 20)
				recentDecisions.erase(recentDecisions.begin());
			for(auto & entry : policies)
				entry.second->actionStarted(battleID, action);
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action)) << "The engine rejected the selected action";
			++decisions;
			if(recorder.battleResults.empty())
			{
				for(auto & entry : policies)
					entry.second->actionFinished(battleID, action);
			}
		}
		std::ostringstream diagnostics;
		diagnostics << "damage=" << recorder.damage << " shots=" << recorder.shots
			<< " melee=" << recorder.meleeAttacks << " catapult=" << recorder.catapultAttacks.size()
			<< " spells=" << heroSpellDecisions;
		for(const auto & decision : recentDecisions)
			diagnostics << '\n' << decision;
		ASSERT_EQ(recorder.battleResults.size(), 1u) << "No unique result after 512 actual activations\n" << diagnostics.str();
		const auto result = recorder.battleResults.back();
		EXPECT_GT(decisions, 0);
		EXPECT_GT(recorder.damage, 0) << "No damage was applied by the engine";
		EXPECT_TRUE(result.winner == BattleSide::ATTACKER || result.winner == BattleSide::DEFENDER);
		if(expectedWinner)
		{
			EXPECT_EQ(result.winner, *expectedWinner);
		}
		for(auto & entry : policies)
			entry.second->battleEnd(battleID, &result, result.questionID);
		if(result.questionID != QuestionID::NONE)
		{
			ASSERT_TRUE(gameHandler->answerQuestion(result.questionID, 0, attackerPlayer));
		}
		EXPECT_TRUE(gameState()->currentBattles.empty()) << "The battle result was not applied to the adventure state";
		EXPECT_EQ(recorder.acceptedResults, 1);
		std::cout << "AI_NATIVE_BATTLE_RESULT backend=" << backendName
			<< " activations=" << decisions << " damage=" << recorder.damage
			<< " ranged_attacks=" << recorder.shots << " melee_attacks=" << recorder.meleeAttacks
			<< " spells=" << heroSpellDecisions
			<< " winner=" << (result.winner == BattleSide::ATTACKER ? "attacker" : "defender")
			<< '\n';
		for(auto & entry : callbacks)
			entry.second->onBattleEnded(battleID);
	}
};

TEST_F(AIIntegrationNativeBattle, completesMeleeBattleThroughFactory)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 20);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion();
	EXPECT_GT(recorder.meleeAttacks, 0);
}

TEST_F(AIIntegrationNativeBattle, completesRangedBattleThroughFactory)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion();
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, appliesDefenderVictoryToAdventureState)
{
	prepareBattle(CreatureID(0), 2, CreatureID(0), 100);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(BattleSide::DEFENDER);
}

TEST_F(AIIntegrationNativeBattle, countsDamageFromTheLethalFirstAttack)
{
	prepareBattle(CreatureID(2), 100, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(BattleSide::ATTACKER);
	EXPECT_EQ(recorder.shots, 1);
}

TEST_F(AIIntegrationNativeBattle, completesDoubleWideGroundBattle)
{
	prepareBattle(CreatureID(10), 12, CreatureID(0), 40);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion();
}

TEST_F(AIIntegrationNativeBattle, completesFlyingBattle)
{
	prepareBattle(CreatureID(4), 20, CreatureID(2), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion();
}

TEST_F(AIIntegrationNativeBattle, completesRangedDuelWithoutDefenceDeadlock)
{
	prepareBattle(CreatureID(2), 30, CreatureID(2), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion();
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, completesFortifiedTownBattle)
{
	prepareSiege();
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(BattleSide::ATTACKER);
	EXPECT_FALSE(recorder.catapultAttacks.empty());
}

TEST_F(AIIntegrationNativeBattle, controlsBallistaAndCompletesBattle)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 40);
	setArmy(defenderSideHero, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	runToCompletion();
	EXPECT_GT(ballistaDecisions, 0);
	EXPECT_GT(ballistaShots, 0) << "Automatic server fire cannot substitute for an AI ballista decision";
}

TEST_F(AIIntegrationNativeBattle, completePortNameCannotSilentlySelectThePartialPolicy)
{
	EXPECT_THROW(AIFactory::createBattleAI("FHeroes2"), std::runtime_error);
	EXPECT_THROW(AIFactory::createBattleAI("HoMM3Original"), std::runtime_error);
	EXPECT_THROW(AIFactory::createBattleAI("TUMEnchanted"), std::runtime_error);
}

TEST_F(AIIntegrationNativeBattle, choosesOffensiveMagicAndCompletesTheBattle)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW);
	runToCompletion(BattleSide::ATTACKER);
	EXPECT_GT(heroSpellDecisions, 0);
	EXPECT_FALSE(recorder.castsOf(SpellID::MAGIC_ARROW).empty());
}

TEST_F(AIIntegrationNativeBattle, disabledMagicKeepsAutocombatPreferences)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW);
	runToCompletion(std::nullopt, false);
	EXPECT_EQ(heroSpellDecisions, 0);
	EXPECT_TRUE(recorder.casts.empty());
}

TEST_F(AIIntegrationNativeBattle, lethalSpellFinishesBeforeAnyStaleUnitCommand)
{
	prepareBattle(CreatureID(2), 1, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW, 20);
	runToCompletion(BattleSide::ATTACKER);
	EXPECT_EQ(heroSpellDecisions, 1);
	EXPECT_EQ(recorder.castsOf(SpellID::MAGIC_ARROW).size(), 1u);
	EXPECT_EQ(recorder.shots, 0);
	EXPECT_EQ(recorder.meleeAttacks, 0);
}

TEST_F(AIIntegrationNativeBattle, noManaStillCompletesUsingUnitActions)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW);
	attackerSideHero->mana = 0;
	runToCompletion();
	EXPECT_EQ(heroSpellDecisions, 0);
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, choosesAreaDamageOnLegalBattlefieldTargets)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 50);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::FIREBALL);
	runToCompletion();
	EXPECT_FALSE(recorder.castsOf(SpellID::FIREBALL).empty());
}

TEST_F(AIIntegrationNativeBattle, rejectsGlobalDamageThatWouldDestroyItsOwnArmy)
{
	prepareBattle(CreatureID(2), 1, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::ARMAGEDDON, 20);
	runToCompletion();
	EXPECT_TRUE(recorder.castsOf(SpellID::ARMAGEDDON).empty());
	EXPECT_EQ(heroSpellDecisions, 0);
}

TEST_F(AIIntegrationNativeBattle, bothSidesCastWithoutRepeatedCommandsInTheSameRound)
{
	prepareBattle(CreatureID(0), 150, CreatureID(0), 150);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW, 2);
	teachBattleSpell(defenderSideHero, SpellID::LIGHTNING_BOLT, 2);
	runToCompletion();
	EXPECT_FALSE(recorder.castsOf(SpellID::MAGIC_ARROW).empty());
	EXPECT_FALSE(recorder.castsOf(SpellID::LIGHTNING_BOLT).empty());
	EXPECT_GT(heroSpellDecisions, 1);
}

TEST_F(AIIntegrationNativeBattle, overwhelmingArmyConservesMana)
{
	prepareBattle(CreatureID(0), 1000, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::MAGIC_ARROW, 1);
	runToCompletion(BattleSide::ATTACKER);
	EXPECT_EQ(heroSpellDecisions, 0);
}

TEST_F(AIIntegrationNativeBattle, resurrectionUsesRealCasualtiesAndCompletesBattle)
{
	prepareBattle(CreatureID(0), 60, CreatureID(0), 60);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::RESURRECTION, 4);
	setMagicMastery(attackerSideHero, MasteryLevel::EXPERT);
	const CStack * injured = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			injured = unit.get();
	ASSERT_NE(injured, nullptr);
	injure(injured, 300);
	runToCompletion();
	EXPECT_FALSE(recorder.castsOf(SpellID::RESURRECTION).empty());
}

TEST_F(AIIntegrationNativeBattle, homm3TacticsCompletesRealMeleeBattle)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 20);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

TEST_F(AIIntegrationNativeBattle, homm3TacticsCompletesRealRangedBattle)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, homm3RangedPrefersActiveTargetOverBlindAndCompletesBattle)
{
	// The decompiled HoMM3 scorer reduces shots into incapacitated targets.
	// A strong but blinded stack must not distract from a weaker active enemy.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(2), 200);
	setArmy(defenderSideHero, CreatureID(0), 35);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 14));
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::BLIND);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	const CStack * shooter = nullptr;
	const CStack * stronger = nullptr;
	const CStack * active = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(2))
			shooter = unit.get();
		else if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
		{
			if(unit->getCount() == 35)
				stronger = unit.get();
			else if(unit->getCount() == 14)
				active = unit.get();
		}
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(stronger, nullptr);
	ASSERT_NE(active, nullptr);
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto view = std::make_shared<DecisionCallback>(player);
	view->onBattleStarted(battle());
	const auto playerBattle = view->getBattle(battle()->getBattleID());
	ASSERT_NE(playerBattle, nullptr);
	const auto before = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*playerBattle, *shooter);
	ASSERT_EQ(before.actionType, EActionType::SHOOT);
	ASSERT_FALSE(before.target.empty());
	EXPECT_EQ(before.target.front().unitValue, stronger->unitId())
		<< "The strong target must attract the ordinary HoMM3 ranged scorer";
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, stronger));
	ASSERT_TRUE(stronger->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BLIND)))));
	const auto after = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*playerBattle, *shooter);
	ASSERT_EQ(after.actionType, EActionType::SHOOT);
	ASSERT_FALSE(after.target.empty());
	EXPECT_EQ(after.target.front().unitValue, active->unitId())
		<< "The incapacitated stack should receive the original HoMM3 shot-value penalty";
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, homm3TacticsAppliesDefenderVictory)
{
	prepareBattle(CreatureID(0), 2, CreatureID(0), 100);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(BattleSide::DEFENDER, false, "HoMM3OriginalTactics");
}

TEST_F(AIIntegrationNativeBattle, homm3TacticsHandlesFlyingAndDoubleWideUnits)
{
	prepareBattle(CreatureID(10), 12, CreatureID(4), 20);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
}

TEST_F(AIIntegrationNativeBattle, tumTacticsCompletesRealMeleeBattle)
{
	prepareBattle(CreatureID(0), 30, CreatureID(0), 20);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

TEST_F(AIIntegrationNativeBattle, tumTacticsCompletesRealRangedBattle)
{
	prepareBattle(CreatureID(2), 20, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.shots, 0);
}

TEST_F(AIIntegrationNativeBattle, tumSiegeDefenderHoldsInsideIntactCastleAndCompletesBattle)
{
	prepareSiege();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(battle()->battleGetGateState(), EGateState::CLOSED);
	const CStack * defender = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->alive() && unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
		{
			defender = unit.get();
			break;
		}
	}
	ASSERT_NE(defender, nullptr);
	ASSERT_TRUE(battle()->battleIsInsideWalls(defender->getPosition()));
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::DEFENDER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("TUMEnchantedTactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::DEFENDER, false);
	policy->activeStack(battleID, defender);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, EActionType::DEFEND)
		<< "The castle defender must hold while the gate is closed and nobody has breached";
	// Resolve through the engine as well: holding must not trap the siege
	// in a battle without a winner.
	runToCompletion(BattleSide::ATTACKER, false, "TUMEnchantedTactics");
	EXPECT_FALSE(recorder.catapultAttacks.empty());
}

TEST_F(AIIntegrationNativeBattle, tumTacticsFinishesMixedFastAndSlowEnemyBattle)
{
	// Mix one faster flyer with slower infantry so the movement-only
	// anti-distraction policy has the information it needs. The source
	// predicate is tested independently in CI/source_tactics_test.cpp.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 80);
	setArmy(defenderSideHero, CreatureID(4), 15);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 25));
	ASSERT_FALSE(HasFatalFailure());
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	const CStack * fast = nullptr;
	const CStack * slow = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() != BattleSide::DEFENDER)
			continue;
		if(unit->creatureId() == CreatureID(4))
			fast = unit.get();
		if(unit->creatureId() == CreatureID(0))
			slow = unit.get();
	}
	ASSERT_NE(fast, nullptr);
	ASSERT_NE(slow, nullptr);
	EXPECT_GT(fast->getMovementRange(), slow->getMovementRange());
	// A genuine server-controlled finish is required, not just an
	// invocation of an isolated route-selection predicate.
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

TEST_F(AIIntegrationNativeBattle, tumIgnoresNearbyFastDecoyForMovement)
{
	// The HoMM3 baseline chases the nearby fast decoy. The TUM variant
	// should advance toward the slower enemy instead, without preventing
	// a legal attack when one is available. This is a native battle state.
	prepareBattle(CreatureID(0), 80, CreatureID(0), 35);
	ASSERT_FALSE(HasFatalFailure());
	const CStack * own = nullptr;
	const CStack * slow = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->creatureId() != CreatureID(0))
			continue;
		if(unit->unitSide() == BattleSide::ATTACKER)
			own = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER)
			slow = unit.get();
	}
	ASSERT_NE(own, nullptr);
	ASSERT_NE(slow, nullptr);
	const auto origin = own->getPosition();
	const int baitY = origin.getY() >= 4 ? origin.getY() - 3 : origin.getY() + 3;
	const BattleHex baitHex(static_cast<si16>(origin.getX() + 5), static_cast<si16>(baitY));
	ASSERT_TRUE(baitHex.isAvailable());
	ASSERT_LT(BattleHex::getDistance(origin, baitHex), BattleHex::getDistance(origin, slow->getPosition()));
	const CStack * fast = addStack(BattleSide::DEFENDER, CreatureID(4), baitHex, 12);
	ASSERT_NE(fast, nullptr);
	const int speed = static_cast<int>(own->getMovementRange());
	ASSERT_GE(static_cast<int>(fast->getMovementRange()), speed + 2);
	ASSERT_LE(static_cast<int>(slow->getMovementRange()), speed);
	const int distance = BattleHex::getDistance(origin, fast->getPosition());
	ASSERT_GT(distance, speed + 1) << "The decoy must not be immediately attackable";
	ASSERT_LE(distance, speed * 2 + 1) << "The decoy must fall inside the guarded movement range";

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto playerView = callback->getBattle(battleID);
	ASSERT_NE(playerView, nullptr);
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*playerView, *own);
	ASSERT_EQ(baseline.actionType, EActionType::WALK)
		<< "Fixture must make the original HoMM3 baseline pursue rather than attack";

	auto policy = AIFactory::createBattleAI("TUMEnchantedTactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, own);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK);
	ASSERT_FALSE(baseline.target.empty());
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_NE(callback->action->target.front().hexValue, baseline.target.front().hexValue)
		<< "TUM should advance toward slower infantry rather than the faster decoy";
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
}

TEST_F(AIIntegrationNativeBattle, tumTacticsPropagatesDefenderVictory)
{
	prepareBattle(CreatureID(0), 2, CreatureID(0), 100);
	ASSERT_FALSE(HasFatalFailure());
	runToCompletion(BattleSide::DEFENDER, false, "TUMEnchantedTactics");
}

TEST_F(AIIntegrationNativeBattle, tumTacticsMakesRealBallistaDecisions)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 40);
	setArmy(defenderSideHero, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(ballistaDecisions, 0);
	EXPECT_GT(ballistaShots, 0);
}
TEST_F(AIIntegrationNativeBattle, tumBallistaDefendsRatherThanBreakingBlind)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 55);
	setArmy(defenderSideHero, CreatureID(0), 30);
	ASSERT_FALSE(HasFatalFailure());
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::BALLISTA), ArtifactPosition::MACH1);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARTILLERY),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	teachBattleSpell(attackerSideHero, SpellID::BLIND);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	const CStack * blinded = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
			blinded = unit.get();
	ASSERT_NE(blinded, nullptr);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, blinded));
	ASSERT_TRUE(blinded->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BLIND)))));
	const CStack * attackingBallista = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID::BALLISTA)
			attackingBallista = unit.get();
	ASSERT_NE(attackingBallista, nullptr);
	const auto view = std::make_shared<DecisionCallback>(battle()->sideToPlayer(BattleSide::ATTACKER));
	view->onBattleStarted(battle());
	const auto viewedBattle = view->getBattle(battle()->getBattleID());
	ASSERT_NE(viewedBattle, nullptr);
	const auto baselineDecision = AIIntegration::HoMM3TacticsBattleAI::chooseAction(
		*viewedBattle, *attackingBallista);
	EXPECT_EQ(baselineDecision.actionType, EActionType::SHOOT)
		<< "The baseline must shoot so that the TUM target protection is a real behavioral override";
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(ballistaDecisions, 0);
	EXPECT_GT(ballistaDefendsWhileAllEnemiesBlind, 0)
		<< "TUM ballista must not shoot the only remaining blinded target";
}

TEST_F(AIIntegrationNativeBattle, tumMeleeHoldsPositionToProtectShootersAgainstRealNeutralStack)
{
	// Construct an actual neutral map object, not a player-owned hero whose
	// color is mocked. The TUM protection rule explicitly requires neutrals.
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(0), 60);
	ASSERT_TRUE(gameHandler->insertNewStack(
		StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 25));
	ASSERT_FALSE(HasFatalFailure());
	gameHandler->createWanderingMonster({9, 9, 0}, CreatureID(0), 40);
	const auto * neutral = findFirst<CGCreature>();
	ASSERT_NE(neutral, nullptr);
	// A wandering monster is UNFLAGGABLE on the adventure map;
	// SideInBattle normalizes that ownership to NEUTRAL for combat.
	ASSERT_EQ(neutral->getOwner(), PlayerColor::UNFLAGGABLE);
	gameHandler->battles->startBattle(attackerSideHero, neutral);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);

	const CStack * melee = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			melee = unit.get();
	}
	ASSERT_NE(melee, nullptr);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto playerView = callback->getBattle(battleID);
	ASSERT_NE(playerView, nullptr);
	EXPECT_EQ(battle()->sideToPlayer(BattleSide::DEFENDER), PlayerColor::NEUTRAL);
	const auto baseline = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*playerView, *melee);
	EXPECT_NE(baseline.actionType, EActionType::DEFEND)
		<< "The HoMM3 baseline must advance for a meaningful TUM differential";
	auto policy = AIFactory::createBattleAI("TUMEnchantedTactics");
	ASSERT_NE(policy, nullptr);
	policy->playerID = player;
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
	AutocombatPreferences prefs;
	prefs.enableSpellsUsage = false;
	policy->initBattleInterface(environment, callback, prefs);
	policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
		battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
		battle()->getSideHero(BattleSide::ATTACKER),
		battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
	policy->activeStack(battleID, melee);
	ASSERT_TRUE(callback->action.has_value());
	EXPECT_EQ(callback->action->actionType, EActionType::DEFEND)
		<< "TUM melee must protect friendly shooters before neutral contact";
	// In addition to the differential decision above, complete the battle
	// through the real server. A fabricated winner is never accepted.
	runToCompletion(std::nullopt, false, "TUMEnchantedTactics");
	EXPECT_GT(recorder.shots, 0);
}
