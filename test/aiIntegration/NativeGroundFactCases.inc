#include "homm3Ground/PinnedGround.h"
// Native fact admission tests; no ground-map lifecycle certificate is implied.
class AIIntegrationNativeGroundFacts : public AIIntegrationNativeRangedCensusBattle
{
protected:
	void prepareMelee()
	{
		prepareCensus(false, ETerrainId::SAND);
		ASSERT_FALSE(HasFatalFailure());
		shooters[0]->setPosition(BattleHex(93));
		targets[0]->setPosition(BattleHex(94));
		ASSERT_TRUE(censusView->battleCanAttackUnit(shooters[0], targets[0]));
	}

	void compareMelee(bool reverse, int offense, int armorer, int actorWound = 0, int targetWound = 0)
	{
		namespace Ref = PinnedExchange;
		using namespace AIIntegration::Ports::HoMM3;
		Ref::manager = {};
		Ref::game.m_setup.m_difficulty = 1;
		Ref::akCreatureTypeTraits[2] = {static_cast<double>(CreatureID(2).toCreature()->getFightValue()), 6, 3};
		Ref::akCreatureTypeTraits[6] = {static_cast<double>(CreatureID(6).toCreature()->getFightValue()), 10, 12};
		Ref::hero attackHero, defenseHero;
		attackHero.m_skillLevel[Ref::eSecSkillOffense] = offense;
		defenseHero.m_skillLevel[Ref::eSecSkillDefense] = armorer;
		Ref::army archer, swordsman;
		archer.m_creatureType = 2;
		archer.flags = Ref::creatureShootingArmy;
		archer.m_numTroops = 80;
		archer.m_monInfo.hitPoints = 10;
		archer.m_monInfo.damageLowBound = 2;
		archer.m_monInfo.damageHighBound = 3;
		archer.m_monInfo.attackSkill = 6;
		archer.m_monInfo.defenseSkill = 3;
		swordsman.m_creatureType = 6;
		swordsman.m_numTroops = 15;
		swordsman.m_monInfo.hitPoints = 35;
		swordsman.m_monInfo.damageLowBound = 6;
		swordsman.m_monInfo.damageHighBound = 9;
		swordsman.m_monInfo.attackSkill = 10;
		swordsman.m_monInfo.defenseSkill = 12;
		auto & sourceActor = reverse ? swordsman : archer;
		auto & sourceTarget = reverse ? archer : swordsman;
		sourceActor.controller = &attackHero;
		sourceTarget.controller = &defenseHero;
		sourceActor.m_topCreatureDamage = actorWound;
		sourceTarget.m_topCreatureDamage = targetWound;
		auto * actor = reverse ? targets[0] : shooters[0];
		auto * target = reverse ? shooters[0] : targets[0];
		const auto actorFacts = NativeExchangeFacts::unit(*censusView, *actor, 0, 0);
		const auto targetFacts = NativeExchangeFacts::unit(*censusView, *target, 0, 0);
		const auto attack = NativeExchangeFacts::meleeAttack(*censusView, *actor, *target);
		const auto retaliation = NativeExchangeFacts::meleeAttack(*censusView, *target, *actor);
		const auto canRetaliate = NativeExchangeFacts::retaliationAvailable(*actor, *target);
		ASSERT_TRUE(actorFacts);
		ASSERT_TRUE(targetFacts);
		ASSERT_TRUE(attack);
		ASSERT_TRUE(retaliation);
		ASSERT_TRUE(canRetaliate);
		EXPECT_TRUE(*canRetaliate);
		EXPECT_EQ(attack->controllerSkillFactor, attackHero.getOffenseFactor());
		EXPECT_EQ(attack->defenderReduction, static_cast<double>(defenseHero.getDefenseFactor()));
		EXPECT_EQ(attack->meleeShooterPenalty, !reverse);
		EXPECT_EQ(retaliation->meleeShooterPenalty, reverse);
		EXPECT_EQ(actorFacts->topWound, actorWound);
		EXPECT_EQ(targetFacts->topWound, targetWound);
		SourceOrdinaryExchangeInput input;
		input.attacker = *actorFacts;
		input.defender = *targetFacts;
		input.attack[0] = *attack;
		input.retaliation = *retaliation;
		input.canRetaliate = *canRetaliate;
		input.retaliationPolicy = true;
		const auto actual = evaluateSourceOrdinaryExchange(input);
		ASSERT_TRUE(actual);
		Ref::type_AI_combat_parameters parameters;
		EXPECT_EQ(actual->value, parameters.getSimpleAttackEffect(sourceActor, sourceTarget, false, 0));
	}
};

TEST_F(AIIntegrationNativeGroundFacts, homm3NativeMeleeOffenceArmorerAndShooterPenaltyMatchPinnedSource)
{
	prepareMelee();
	ASSERT_FALSE(HasFatalFailure());
	for(bool reverse : {false, true})
	for(int offense = 0; offense <= 3; ++offense)
	for(int armorer = 0; armorer <= 3; ++armorer)
	{
		SCOPED_TRACE("reverse=" + std::to_string(reverse) + " offense=" + std::to_string(offense) + " armorer=" + std::to_string(armorer));
		auto * attacker = reverse ? defenderSideHero : attackerSideHero;
		auto * defender = reverse ? attackerSideHero : defenderSideHero;
		attacker->setSecSkillLevel(SecondarySkill(SecondarySkill::OFFENCE), offense, ChangeValueMode::ABSOLUTE);
		attacker->setSecSkillLevel(SecondarySkill(SecondarySkill::ARMORER), 0, ChangeValueMode::ABSOLUTE);
		defender->setSecSkillLevel(SecondarySkill(SecondarySkill::OFFENCE), 0, ChangeValueMode::ABSOLUTE);
		defender->setSecSkillLevel(SecondarySkill(SecondarySkill::ARMORER), armorer, ChangeValueMode::ABSOLUTE);
		compareMelee(reverse, offense, armorer);
		ASSERT_FALSE(HasFatalFailure());
	}
}

TEST_F(AIIntegrationNativeGroundFacts, homm3NativeMeleeWoundedStacksMatchPinnedSource)
{
	prepareMelee();
	ASSERT_FALSE(HasFatalFailure());
	int64_t damage = 3;
	shooters[0]->damage(damage);
	damage = 7;
	targets[0]->damage(damage);
	compareMelee(false, 0, 0, 3, 7);
	ASSERT_FALSE(HasFatalFailure());
	compareMelee(true, 0, 0, 7, 3);
}

TEST_F(AIIntegrationNativeGroundFacts, homm3NativeRetaliationAvailabilityTracksServerConsumptionAndNewRound)
{
	using AIIntegration::Ports::HoMM3::NativeExchangeFacts;
	prepareMelee();
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(NativeExchangeFacts::retaliationAvailable(*shooters[0], *targets[0]), std::optional<bool>(true));
	const auto beforeMelees = recorder.meleeAttacks;
	const auto targetHealth = targets[0]->getAvailableHealth();
	const auto actorHealth = shooters[0]->getAvailableHealth();
	const auto action = BattleAction::makeMeleeAttack(shooters[0], targets[0], shooters[0]->getPosition());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), attackerSideHero->getOwner(), action));
	ASSERT_TRUE(targets[0]->alive());
	ASSERT_TRUE(shooters[0]->alive());
	EXPECT_GT(recorder.meleeAttacks, beforeMelees);
	EXPECT_LT(targets[0]->getAvailableHealth(), targetHealth);
	EXPECT_LT(shooters[0]->getAvailableHealth(), actorHealth);
	EXPECT_EQ(NativeExchangeFacts::retaliationAvailable(*shooters[0], *targets[0]), std::optional<bool>(false));
	endRound();
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(NativeExchangeFacts::retaliationAvailable(*shooters[0], *targets[0]), std::optional<bool>(true));
}

TEST_F(AIIntegrationNativeGroundFacts, homm3NativeMeleeRejectsUnsupportedStrikeMechanics)
{
	using AIIntegration::Ports::HoMM3::NativeExchangeFacts;
	prepareMelee();
	ASSERT_FALSE(HasFatalFailure());
	for(const auto type : {BonusType::FIRST_STRIKE, BonusType::FEROCITY, BonusType::LONG_WEAPON})
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, 1, BonusSourceID());
		targets[0]->addNewBonus(bonus);
		EXPECT_FALSE(NativeExchangeFacts::meleeAttack(*censusView, *shooters[0], *targets[0]));
		EXPECT_FALSE(NativeExchangeFacts::retaliationAvailable(*shooters[0], *targets[0]));
		targets[0]->removeBonus(bonus);
		EXPECT_TRUE(NativeExchangeFacts::meleeAttack(*censusView, *shooters[0], *targets[0]));
	}
}

TEST_F(AIIntegrationNativeGroundFacts, homm3NativeGroundSpeedAcceptsSandAndRejectsUnknownSpeedEffect)
{
	using AIIntegration::Ports::HoMM3::NativeExchangeFacts;
	prepareCensus(false, ETerrainId::SAND);
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(NativeExchangeFacts::groundSpeed(*censusView, *shooters[0]), std::optional<int32_t>(4));
	EXPECT_EQ(NativeExchangeFacts::groundSpeed(*censusView, *targets[0]), std::optional<int32_t>(5));
	auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 1, BonusSourceID());
	targets[0]->addNewBonus(bonus);
	EXPECT_FALSE(NativeExchangeFacts::groundSpeed(*censusView, *targets[0]));
}

TEST_F(AIIntegrationNativeGroundFacts, homm3NativeGroundSpeedIncludesCanonicalNativeTerrain)
{
	using AIIntegration::Ports::HoMM3::NativeExchangeFacts;
	prepareCensus(false, ETerrainId::GRASS);
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(NativeExchangeFacts::groundSpeed(*censusView, *shooters[0]), std::optional<int32_t>(5));
	EXPECT_EQ(NativeExchangeFacts::groundSpeed(*censusView, *targets[0]), std::optional<int32_t>(6));
}

#include "../../AI/Integration/Ports/HoMM3NativeGroundCensus.h"

TEST_F(AIIntegrationNativeGroundFacts, homm3NativeRangedFactsRejectSharedFirstStrikeAndRangedRetaliation)
{
	using AIIntegration::Ports::HoMM3::NativeExchangeFacts;
	prepareCensus(false, ETerrainId::SAND);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(NativeExchangeFacts::rangedAttack(*censusView, *shooters[0], *targets[0]));
	for(const auto type : {BonusType::FIRST_STRIKE, BonusType::RANGED_RETALIATION})
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, 1, BonusSourceID());
		targets[0]->addNewBonus(bonus);
		EXPECT_FALSE(NativeExchangeFacts::rangedAttack(*censusView, *shooters[0], *targets[0]));
		targets[0]->removeBonus(bonus);
		ASSERT_TRUE(NativeExchangeFacts::rangedAttack(*censusView, *shooters[0], *targets[0]));
	}
}

TEST_F(AIIntegrationNativeGroundFacts, homm3NativeVisibleMapPublishesOrdinaryGrassOccupancy)
{
	using namespace AIIntegration::Ports::HoMM3;
	prepareCensus(true, ETerrainId::GRASS);
	ASSERT_FALSE(HasFatalFailure());
	const auto map = captureNativeVisibleGroundMap(*censusView);
	ASSERT_TRUE(map);
	ASSERT_EQ(map->roster[0].size(), 2u);
	ASSERT_EQ(map->roster[1].size(), 3u);
	EXPECT_EQ(map->cells[86].occupant, (std::optional<SourceGroundIdentity>{{0, 0}}));
	EXPECT_EQ(map->cells[35].occupant, (std::optional<SourceGroundIdentity>{{0, 1}}));
	for(int j = 0; j < 3; ++j)
		EXPECT_EQ(map->cells[94 + j * 17].occupant, (std::optional<SourceGroundIdentity>{{1, j}}));
}

TEST_F(AIIntegrationNativeGroundFacts, homm3NativeVisibleMapIncludesRealStaticObstacle)
{
	using namespace AIIntegration::Ports::HoMM3;
	prepareCensus(false, ETerrainId::SAND);
	ASSERT_FALSE(HasFatalFailure());
	auto obstacle = std::make_shared<CObstacleInstance>();
	obstacle->ID = 0;
	obstacle->uniqueID = 100;
	obstacle->obstacleType = CObstacleInstance::USUAL;
	bool placed = false;
	for(int head = 18; head < 169 && !placed; ++head)
	{
		obstacle->pos = BattleHex(head);
		const auto blocked = obstacle->getBlockedTiles();
		if(blocked.empty()) continue;
		bool valid = true;
		for(const auto hex : blocked)
		{
			if(!hex.isAvailable()) { valid = false; break; }
			for(const auto & stack : battle()->stacks)
				if(stack->getHexes().contains(hex)) valid = false;
		}
		placed = valid;
	}
	ASSERT_TRUE(placed);
	battle()->obstacles.push_back(obstacle);
	const auto visible = censusView->battleGetAllObstacles();
	ASSERT_EQ(visible.size(), 1u);
	const auto map = captureNativeVisibleGroundMap(*censusView);
	ASSERT_TRUE(map);
	for(const auto hex : obstacle->getBlockedTiles()) EXPECT_TRUE(map->cells[hex.toInt()].blocked);
}

TEST_F(AIIntegrationNativeGroundFacts, homm3NativeCertifiedGroundRowMatchesIndependentPinnedPathAndExchange)
{
	namespace Ref = PinnedExchange;
	namespace Path = PinnedGround;
	using namespace AIIntegration::Ports::HoMM3;
	prepareCensus(true, ETerrainId::SAND);
	ASSERT_FALSE(HasFatalFailure());
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(attackerSideHero->getOwner()), censusCallback);
	const auto ranged = captureNativeRangedCensus(*censusView, BattleSide::ATTACKER);
	const auto uncertified = captureNativeOrdinaryCensus(*censusView, BattleSide::ATTACKER, environment.get(), false);
	ASSERT_TRUE(ranged);
	ASSERT_TRUE(uncertified);
	ASSERT_TRUE(ranged->rows[0]);
	ASSERT_TRUE(uncertified->rows[0]);
	ASSERT_TRUE(ranged->rows[0]->target);
	ASSERT_TRUE(uncertified->rows[0]->target);
	EXPECT_EQ(uncertified->rows[0]->target->exchangeValue, ranged->rows[0]->target->exchangeValue);
	EXPECT_EQ(uncertified->rows[0]->target->rosterIndex, ranged->rows[0]->target->rosterIndex);
	EXPECT_FALSE(uncertified->rows[1]);
	// Authoritative fixture-created static map, not a production lifecycle proof.
	const auto actual = captureNativeOrdinaryCensus(*censusView, BattleSide::ATTACKER, environment.get(), true);
	ASSERT_TRUE(actual);
	ASSERT_TRUE(actual->rows[1]);
	ASSERT_TRUE(actual->rows[1]->target);
	ASSERT_EQ(nativeRetaliationPolicy(*censusView, BattleSide::ATTACKER, environment.get()), std::optional<bool>(true));
	Path::manager = {};
	Path::manager.setupAdjacencyArray();
	const auto place = [](int side, int slot, int head)
	{
		auto & cell = Path::manager.m_cells[head];
		cell.m_armySide = side;
		cell.m_armySlot = slot;
		auto & unit = Path::manager.m_armies[side][slot];
		unit.group = side;
		unit.index = slot;
		unit.m_gridIndex = head;
	};
	place(0, 0, 86);
	place(0, 1, 35);
	for(int j = 0; j < 3; ++j) place(1, j, 94 + j * 17);
	auto & pathActor = Path::manager.m_armies[0][1];
	pathActor.speed = 4;
	Path::searchArray path;
	path.seedCombatPosition(&pathActor, 0, 127, false, -1);
	Ref::manager = {};
	Ref::game.m_setup.m_difficulty = 1;
	Ref::hero sourceHero;
	Ref::army actor;
	actor.m_creatureType = 0;
	actor.controller = &sourceHero;
	actor.m_numTroops = 10;
	actor.speed = 4;
	actor.m_monInfo.hitPoints = 10;
	actor.m_monInfo.damageLowBound = 1;
	actor.m_monInfo.damageHighBound = 3;
	actor.m_monInfo.attackSkill = 4;
	actor.m_monInfo.defenseSkill = 5;
	Ref::akCreatureTypeTraits[0] = {static_cast<double>(CreatureID(0).toCreature()->getFightValue()), 4, 5};
	Ref::akCreatureTypeTraits[6] = {static_cast<double>(CreatureID(6).toCreature()->getFightValue()), 10, 12};
	Ref::type_AI_combat_parameters parameters;
	std::array<Ref::army, 3> enemy;
	for(int j = 0; j < 3; ++j)
	{
		auto & target = enemy[j];
		target.index = j;
		target.m_creatureType = 6;
		target.controller = &sourceHero;
		target.m_numTroops = 15;
		target.m_monInfo.hitPoints = 35;
		target.m_monInfo.damageLowBound = 6;
		target.m_monInfo.damageHighBound = 9;
		target.m_monInfo.attackSkill = 10;
		target.m_monInfo.defenseSkill = 12;
		const int head = 94 + j * 17;
		if(!Path::manager.m_cells[head].m_validMove) continue;
		const int distance = path.getHex(head)->m_cost;
		const auto effect = parameters.getSimpleAttackEffect(actor, target, false, distance > 4 ? 0 : distance);
		actor.considerAttack(&target, effect, distance);
	}
	ASSERT_NE(actor.AI_target, nullptr);
	EXPECT_EQ(actual->rows[1]->target->rosterIndex, actor.AI_target->index);
	EXPECT_EQ(actual->rows[1]->target->exchangeValue, actor.AI_target_value);
	EXPECT_EQ(actual->rows[1]->target->attackDistance, actor.AI_target_distance);
	EXPECT_EQ(actual->rows[1]->target->attackTime, actor.getAITargetTime());
	EXPECT_EQ(actual->rows[1]->possibleTargets, static_cast<uint32_t>(actor.AI_possible_targets));
}
