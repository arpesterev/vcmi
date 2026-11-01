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
