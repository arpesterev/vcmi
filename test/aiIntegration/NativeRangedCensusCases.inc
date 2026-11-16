#include "../../AI/Integration/Ports/HoMM3NativeRangedCensus.h"

class AIIntegrationNativeRangedCensusBattle : public AIIntegrationNativeBattle
{
protected:
	std::shared_ptr<DecisionCallback> censusCallback;
	std::shared_ptr<CPlayerBattleCallback> censusView;
	std::vector<CStack *> shooters, targets;

	void prepareCensus(bool groundPeer = false, TerrainId terrain = ETerrainId::GRASS, int archerCount = 80)
	{
		startRecordedGame();
		ASSERT_FALSE(HasFatalFailure());
		setArmy(attackerSideHero, CreatureID(2), archerCount);
		setArmy(defenderSideHero, CreatureID(6), 15);
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			for(const auto skill : {SecondarySkill::ARCHERY, SecondarySkill::OFFENCE, SecondarySkill::ARMORER})
				hero->setSecSkillLevel(SecondarySkill(skill), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
			hero->setPrimarySkill(PrimarySkill::ATTACK, 0, ChangeValueMode::ABSOLUTE);
			hero->setPrimarySkill(PrimarySkill::DEFENSE, 0, ChangeValueMode::ABSOLUTE);
		}
		if(groundPeer)
			ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 10));
		for(int slot = 1; slot <= 2; ++slot)
			ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(defenderSideHero->id, SlotID(slot)), CreatureID(6).toCreature(), 15));
		map()->getTile(defenderSideHero->visitablePos()).terrainType = terrain;
		gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		ASSERT_EQ(battle()->getTerrainType(), terrain);
		battle()->obstacles.clear();
		for(const auto & unit : battle()->stacks)
			(unit->unitSide() == BattleSide::ATTACKER ? shooters : targets).push_back(unit.get());
		const auto bySlot = [](const auto * lhs, const auto * rhs) { return lhs->unitSlot() < rhs->unitSlot(); };
		std::sort(shooters.begin(), shooters.end(), bySlot);
		std::sort(targets.begin(), targets.end(), bySlot);
		ASSERT_EQ(shooters.size(), groundPeer ? 2u : 1u);
		ASSERT_EQ(targets.size(), 3u);
		shooters[0]->setPosition(BattleHex(86));
		if(groundPeer) shooters[1]->setPosition(BattleHex(35));
		for(size_t i = 0; i < targets.size(); ++i)
			targets[i]->setPosition(BattleHex(94 + static_cast<int>(i) * 17));
		battle()->activeStack = shooters[0]->unitId();
		censusCallback = std::make_shared<DecisionCallback>(attackerSideHero->getOwner());
		censusCallback->onBattleStarted(battle());
		censusView = censusCallback->getBattle(battle()->getBattleID());
	}

	auto capture() const
	{
		return AIIntegration::Ports::HoMM3::captureNativeRangedCensus(*censusView, BattleSide::ATTACKER);
	}
};

TEST_F(AIIntegrationNativeRangedCensusBattle, homm3NativeRangedCensusKeepsStableMultiEnemyTies)
{
	prepareCensus();
	ASSERT_FALSE(HasFatalFailure());
	const auto snapshot = capture();
	ASSERT_TRUE(snapshot);
	ASSERT_EQ(snapshot->rows.size(), 1u);
	ASSERT_TRUE(snapshot->rows[0]);
	ASSERT_TRUE(snapshot->rows[0]->target);
	ASSERT_EQ(snapshot->enemyIds.size(), 3u);
	for(size_t i = 0; i < targets.size(); ++i) EXPECT_EQ(snapshot->enemyIds[i], targets[i]->unitId());
	EXPECT_EQ(snapshot->rows[0]->target->rosterIndex, 0);
	EXPECT_GT(snapshot->rows[0]->target->exchangeValue, 0);
	EXPECT_EQ(snapshot->rows[0]->possibleTargets, 7u);
	EXPECT_EQ(snapshot->committed(targets[0]->unitId()), std::optional<bool>(true));
	EXPECT_EQ(snapshot->committed(targets[1]->unitId()), std::optional<bool>(false));
}

TEST_F(AIIntegrationNativeRangedCensusBattle, homm3NativeRangedCensusPreservesDeadGhostSlotIdentity)
{
	prepareCensus();
	ASSERT_FALSE(HasFatalFailure());
	int64_t fatal = targets[0]->getAvailableHealth();
	targets[0]->damage(fatal);
	targets[0]->ghost = true;
	ASSERT_FALSE(targets[0]->alive());
	const auto snapshot = capture();
	ASSERT_TRUE(snapshot);
	ASSERT_EQ(snapshot->enemyIds.size(), 3u);
	EXPECT_EQ(snapshot->enemyIds[0], targets[0]->unitId());
	ASSERT_TRUE(snapshot->rows[0]);
	ASSERT_TRUE(snapshot->rows[0]->target);
	EXPECT_EQ(snapshot->rows[0]->target->rosterIndex, 1);
	EXPECT_EQ(snapshot->committed(targets[0]->unitId()), std::optional<bool>(false));
	EXPECT_EQ(snapshot->committed(targets[1]->unitId()), std::optional<bool>(true));
}

TEST_F(AIIntegrationNativeRangedCensusBattle, homm3NativeRangedCensusWitnessSurvivesUnknownGroundPeer)
{
	prepareCensus(true);
	ASSERT_FALSE(HasFatalFailure());
	auto snapshot = capture();
	ASSERT_TRUE(snapshot);
	ASSERT_EQ(snapshot->rows.size(), 2u);
	ASSERT_TRUE(snapshot->rows[0]);
	ASSERT_TRUE(snapshot->rows[0]->target);
	EXPECT_FALSE(snapshot->rows[1]);
	EXPECT_EQ(snapshot->committed(targets[0]->unitId()), std::optional<bool>(true));
	EXPECT_FALSE(snapshot->committed(targets[1]->unitId()).has_value());
	int64_t fatal = shooters[1]->getAvailableHealth();
	shooters[1]->damage(fatal);
	snapshot = capture();
	ASSERT_TRUE(snapshot);
	ASSERT_TRUE(snapshot->rows[1]);
	EXPECT_FALSE(snapshot->rows[1]->target);
	EXPECT_EQ(snapshot->rows[1]->possibleTargets, 0u);
	EXPECT_EQ(snapshot->committed(targets[1]->unitId()), std::optional<bool>(false));
}

TEST_F(AIIntegrationNativeRangedCensusBattle, homm3NativeSourceLastActionRequiresOtherFriendlyDone)
{
	prepareCensus(true);
	ASSERT_FALSE(HasFatalFailure());
	using AIIntegration::Ports::HoMM3::nativeSourceLastAction;
	EXPECT_FALSE(nativeSourceLastAction(*censusView, *shooters[0]));
	shooters[1]->setTurnStatus(battle::UnitTurnStatus::ACTED);
	EXPECT_TRUE(nativeSourceLastAction(*censusView, *shooters[0]));
	shooters[1]->setTurnStatus(battle::UnitTurnStatus::DEFENDED);
	EXPECT_TRUE(nativeSourceLastAction(*censusView, *shooters[0]));
}

TEST_F(AIIntegrationNativeRangedCensusBattle, homm3NativeMultiEnemyRayPreviewIsIsolatedAndAccepted)
{
	prepareCensus(false, ETerrainId::GRASS, 100);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(CreatureID(2).toCreature()->getFightValue(), 115);
	ASSERT_EQ(CreatureID(6).toCreature()->getFightValue(), 445);
	// All units share the +1 terrain modifier, normalized away in source values.
	// This fixture proves !killsOnly before requiring a positive Ray action.
	ASSERT_GE(2 * 100 * 115, 3 * 15 * 445);
	for(const auto & spell : LIBRARY->spellh->objects) attackerSideHero->removeSpellFromSpellbook(spell->id);
	teachBattleSpell(attackerSideHero, SpellID::DISRUPTING_RAY, 6);
	setMagicMastery(attackerSideHero, MasteryLevel::NONE);
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(attackerSideHero->getOwner()), censusCallback);
	const auto mana = attackerSideHero->mana;
	const auto packets = recorder.spellPackets;
	std::vector<int64_t> health;
	std::vector<int> defense;
	for(const auto * target : targets)
	{
		health.push_back(target->getAvailableHealth());
		defense.push_back(target->getDefense(false));
	}
	const auto selected = AIIntegration::HoMM3SpellPolicy::choose(censusView, environment.get(), *shooters[0]);
	EXPECT_EQ(attackerSideHero->mana, mana);
	EXPECT_EQ(recorder.spellPackets, packets);
	for(size_t i = 0; i < targets.size(); ++i)
	{
		EXPECT_EQ(targets[i]->getAvailableHealth(), health[i]);
		EXPECT_EQ(targets[i]->getDefense(false), defense[i]);
	}
	ASSERT_TRUE(selected);
	EXPECT_EQ(selected->spell, SpellID(SpellID::DISRUPTING_RAY));
	ASSERT_FALSE(selected->target.empty());
	EXPECT_TRUE(targets[0]->getHexes().contains(selected->target.front().hexValue));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), attackerSideHero->getOwner(), *selected));
	EXPECT_LT(attackerSideHero->mana, mana);
	EXPECT_GT(recorder.spellPackets, packets);
	EXPECT_LT(targets[0]->getDefense(false), defense[0]);
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
}

TEST_F(AIIntegrationNativeRangedCensusBattle, homm3NativeMultiEnemyRayKeepsUnderdogUnavailable)
{
	prepareCensus();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(CreatureID(2).toCreature()->getFightValue(), 115);
	ASSERT_EQ(CreatureID(6).toCreature()->getFightValue(), 445);
	ASSERT_LT(2 * 80 * 115, 3 * 15 * 445);
	const auto census = capture();
	ASSERT_TRUE(census);
	ASSERT_EQ(census->committed(targets[0]->unitId()), std::optional<bool>(true));
	ASSERT_TRUE(AIIntegration::Ports::HoMM3::nativeSourceLastAction(*censusView, *shooters[0]));
	for(const auto & spell : LIBRARY->spellh->objects) attackerSideHero->removeSpellFromSpellbook(spell->id);
	teachBattleSpell(attackerSideHero, SpellID::DISRUPTING_RAY, 6);
	setMagicMastery(attackerSideHero, MasteryLevel::NONE);
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(attackerSideHero->getOwner()), censusCallback);
	EXPECT_FALSE(AIIntegration::HoMM3SpellPolicy::choose(censusView, environment.get(), *shooters[0]));
}

TEST_F(AIIntegrationNativeRangedCensusBattle, homm3NativeRayWaitsForGroundPeerThenUsesKnownWitness)
{
	prepareCensus(true);
	ASSERT_FALSE(HasFatalFailure());
	for(const auto & spell : LIBRARY->spellh->objects) attackerSideHero->removeSpellFromSpellbook(spell->id);
	teachBattleSpell(attackerSideHero, SpellID::DISRUPTING_RAY, 6);
	setMagicMastery(attackerSideHero, MasteryLevel::NONE);
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(attackerSideHero->getOwner()), censusCallback);
	EXPECT_FALSE(AIIntegration::HoMM3SpellPolicy::choose(censusView, environment.get(), *shooters[0]));
	shooters[1]->setTurnStatus(battle::UnitTurnStatus::ACTED);
	const auto census = capture();
	ASSERT_TRUE(census);
	ASSERT_TRUE(census->rows[0]);
	EXPECT_FALSE(census->rows[1]);
	const auto selected = AIIntegration::HoMM3SpellPolicy::choose(censusView, environment.get(), *shooters[0]);
	ASSERT_TRUE(selected);
	EXPECT_EQ(selected->spell, SpellID(SpellID::DISRUPTING_RAY));
}

#include "homm3Exchange/PinnedExchange.h"

class AIIntegrationNativeRangedMatrix : public AIIntegrationNativeRangedCensusBattle,
	public ::testing::WithParamInterface<int>
{
};

TEST_P(AIIntegrationNativeRangedMatrix, homm3NativeRangedRowsMatchIndependentSourceOracle)
{
	namespace Ref = PinnedExchange;
	const int pairing = GetParam() % 36;
	const int terrainModifier = GetParam() >= 36 ? 1 : 0;
	const TerrainId terrain = terrainModifier ? ETerrainId::GRASS : ETerrainId::SAND;
	const int friendlyCount = 2 + pairing / 6;
	const int enemyCount = 2 + pairing % 6;
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(2), 17);
	setArmy(defenderSideHero, CreatureID(6), 11);
	for(auto * hero : {attackerSideHero, defenderSideHero})
	{
		for(const auto skill : {SecondarySkill::ARCHERY, SecondarySkill::OFFENCE, SecondarySkill::ARMORER})
			hero->setSecSkillLevel(SecondarySkill(skill), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::ATTACK, 0, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::DEFENSE, 0, ChangeValueMode::ABSOLUTE);
	}
	for(int i = 1; i < friendlyCount; ++i)
		ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id, SlotID(i)), CreatureID(2).toCreature(), 17 + i * 7));
	for(int i = 1; i < enemyCount; ++i)
		ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(defenderSideHero->id, SlotID(i)), CreatureID(6).toCreature(), 11 + i * 9));
	map()->getTile(defenderSideHero->visitablePos()).terrainType = terrain;
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	ASSERT_EQ(battle()->getTerrainType(), terrain);
	battle()->obstacles.clear();
	shooters.resize(friendlyCount);
	targets.resize(enemyCount);
	for(const auto & unit : battle()->stacks)
		(unit->unitSide() == BattleSide::ATTACKER ? shooters : targets).at(unit->unitSlot().getNum()) = unit.get();
	for(int i = 0; i < friendlyCount; ++i) shooters[i]->setPosition(BattleHex(17 * (i + 2) + 1));
	for(int i = 0; i < enemyCount; ++i) targets[i]->setPosition(BattleHex(17 * (i + 2) + (i % 2 ? 15 : 8)));
	// Reverse storage, preserving army slots and a corpse inside the original roster.
	std::reverse(battle()->stacks.begin(), battle()->stacks.end());
	int64_t fatal = targets[0]->getAvailableHealth();
	targets[0]->damage(fatal);
	targets[0]->ghost = true;
	censusCallback = std::make_shared<DecisionCallback>(attackerSideHero->getOwner());
	censusCallback->onBattleStarted(battle());
	censusView = censusCallback->getBattle(battle()->getBattleID());
	const auto snapshot = capture();
	ASSERT_TRUE(snapshot);
	ASSERT_EQ(snapshot->rows.size(), static_cast<size_t>(friendlyCount));
	ASSERT_EQ(snapshot->enemyIds.size(), static_cast<size_t>(enemyCount));
	Ref::manager = {};
	Ref::hero sourceHero;
	Ref::type_AI_combat_parameters parameters;
	parameters.m_lowestAttack = terrainModifier;
	parameters.m_lowestDefense = terrainModifier;
	// Fixture base creature attributes, not production mapper outputs.
	Ref::akCreatureTypeTraits[2] = {static_cast<double>(CreatureID(2).toCreature()->getFightValue()), 6, 3};
	Ref::akCreatureTypeTraits[6] = {static_cast<double>(CreatureID(6).toCreature()->getFightValue()), 10, 12};
	for(int i = 0; i < friendlyCount; ++i)
	{
		SCOPED_TRACE(i);
		ASSERT_EQ(shooters[i]->getAttack(true), 6 + terrainModifier);
		ASSERT_EQ(shooters[i]->getDefense(true), 3 + terrainModifier);
		ASSERT_EQ(shooters[i]->getInitiative(0), 4 + terrainModifier);
		Ref::army actor;
		actor.m_creatureType = 2;
		actor.controller = &sourceHero;
		actor.flags = Ref::creatureShootingArmy;
		actor.shooting = true;
		actor.speed = 4 + terrainModifier;
		actor.m_numTroops = 17 + i * 7;
		actor.m_monInfo.hitPoints = 10;
		actor.m_monInfo.damageLowBound = 2;
		actor.m_monInfo.damageHighBound = 3;
		actor.m_monInfo.attackSkill = 6 + terrainModifier;
		actor.m_monInfo.defenseSkill = 3 + terrainModifier;
		std::array<Ref::army, 7> enemies;
		for(int j = 0; j < enemyCount; ++j)
		{
			auto & enemy = enemies[j];
			enemy.index = j;
			enemy.m_creatureType = 6;
			enemy.controller = &sourceHero;
			enemy.m_numTroops = j == 0 ? 0 : 11 + j * 9;
			enemy.m_monInfo.hitPoints = 35;
			enemy.m_monInfo.damageLowBound = 6;
			enemy.m_monInfo.damageHighBound = 9;
			enemy.m_monInfo.attackSkill = 10 + terrainModifier;
			enemy.m_monInfo.defenseSkill = 12 + terrainModifier;
			ASSERT_EQ(targets[j]->getAttack(true), 10 + terrainModifier);
			ASSERT_EQ(targets[j]->getDefense(true), 12 + terrainModifier);
			Ref::manager.distancePenalty = BattleHex::getDistance(shooters[i]->getPosition(), targets[j]->getPosition()) > 10;
			const auto effect = parameters.getSimpleAttackEffect(actor, enemy, true, 1);
			using namespace AIIntegration::Ports::HoMM3;
			const auto nativeActor = NativeExchangeFacts::unit(*censusView, *shooters[i], terrainModifier, terrainModifier);
			const auto nativeTarget = NativeExchangeFacts::unit(*censusView, *targets[j], terrainModifier, terrainModifier);
			const auto nativeShot = NativeExchangeFacts::rangedAttack(*censusView, *shooters[i], *targets[j]);
			ASSERT_TRUE(nativeActor);
			ASSERT_TRUE(nativeTarget);
			ASSERT_TRUE(nativeShot);
			SourceOrdinaryExchangeInput candidate;
			candidate.attacker = *nativeActor;
			candidate.defender = *nativeTarget;
			candidate.attack[1] = *nativeShot;
			candidate.ranged = true;
			const auto actualEffect = evaluateSourceOrdinaryExchange(candidate);
			ASSERT_TRUE(actualEffect);
			EXPECT_EQ(actualEffect->value, effect) << "target slot " << j;
			actor.considerAttack(&enemy, effect, 1);
			EXPECT_EQ(snapshot->enemyIds[j], targets[j]->unitId());
		}
		ASSERT_TRUE(snapshot->rows[i]);
		ASSERT_TRUE(snapshot->rows[i]->target);
		ASSERT_NE(actor.AI_target, nullptr);
		EXPECT_EQ(snapshot->rows[i]->target->rosterIndex, actor.AI_target->index);
		EXPECT_EQ(snapshot->rows[i]->target->exchangeValue, actor.AI_target_value);
		EXPECT_EQ(snapshot->rows[i]->possibleTargets, static_cast<uint32_t>(actor.AI_possible_targets));
		EXPECT_EQ(snapshot->friendlyIds[i], shooters[i]->unitId());
	}
}

INSTANTIATE_TEST_SUITE_P(TwoThroughSevenPerSide, AIIntegrationNativeRangedMatrix, ::testing::Range(0, 72));

TEST_F(AIIntegrationNativeRangedCensusBattle, homm3NativeRangedCensusRejectsUnrecognizedDamageModifier)
{
	prepareCensus();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(capture());
	auto modifier = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::JOUSTING,
		BonusSource::OTHER, 5, BonusSourceID());
	shooters[0]->addNewBonus(modifier);
	const auto after = capture();
	if(after)
	{
		EXPECT_FALSE(after->rows[0]);
		EXPECT_FALSE(after->committed(targets[0]->unitId()).has_value());
	}
}

TEST_F(AIIntegrationNativeRangedCensusBattle, homm3NativePublicArcheryAndArmorerMatchPinnedHeroFactors)
{
	namespace Ref = PinnedExchange;
	using namespace AIIntegration::Ports::HoMM3;
	prepareCensus(false, ETerrainId::SAND);
	ASSERT_FALSE(HasFatalFailure());
	Ref::manager = {};
	Ref::akCreatureTypeTraits[2] = {static_cast<double>(CreatureID(2).toCreature()->getFightValue()), 6, 3};
	Ref::akCreatureTypeTraits[6] = {static_cast<double>(CreatureID(6).toCreature()->getFightValue()), 10, 12};
	Ref::hero attacker, defender;
	Ref::army actor, target;
	actor.m_creatureType = 2;
	actor.controller = &attacker;
	actor.flags = Ref::creatureShootingArmy;
	actor.shooting = true;
	actor.m_numTroops = 80;
	actor.m_monInfo.hitPoints = 10;
	actor.m_monInfo.damageLowBound = 2;
	actor.m_monInfo.damageHighBound = 3;
	actor.m_monInfo.attackSkill = 6;
	actor.m_monInfo.defenseSkill = 3;
	target.m_creatureType = 6;
	target.controller = &defender;
	target.m_numTroops = 15;
	target.m_monInfo.hitPoints = 35;
	target.m_monInfo.damageLowBound = 6;
	target.m_monInfo.damageHighBound = 9;
	target.m_monInfo.attackSkill = 10;
	target.m_monInfo.defenseSkill = 12;
	Ref::type_AI_combat_parameters parameters;
	for(int archery = 0; archery <= 3; ++archery)
	for(int armorer = 0; armorer <= 3; ++armorer)
	{
		SCOPED_TRACE("Archery=" + std::to_string(archery) + ", Armorer=" + std::to_string(armorer));
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARCHERY), archery, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::ARMORER), armorer, ChangeValueMode::ABSOLUTE);
		attacker.m_skillLevel[Ref::eSecSkillArchery] = archery;
		defender.m_skillLevel[Ref::eSecSkillDefense] = armorer;
		const auto actualActor = NativeExchangeFacts::unit(*censusView, *shooters[0], 0, 0);
		const auto actualTarget = NativeExchangeFacts::unit(*censusView, *targets[0], 0, 0);
		const auto actualShot = NativeExchangeFacts::rangedAttack(*censusView, *shooters[0], *targets[0]);
		ASSERT_TRUE(actualActor);
		ASSERT_TRUE(actualTarget);
		ASSERT_TRUE(actualShot);
		EXPECT_EQ(actualShot->controllerSkillFactor, attacker.getArcheryFactor());
		EXPECT_EQ(actualShot->defenderReduction, static_cast<double>(defender.getDefenseFactor()));
		SourceOrdinaryExchangeInput input;
		input.attacker = *actualActor;
		input.defender = *actualTarget;
		input.attack[1] = *actualShot;
		input.ranged = true;
		const auto actual = evaluateSourceOrdinaryExchange(input);
		ASSERT_TRUE(actual);
		EXPECT_EQ(actual->value, parameters.getSimpleAttackEffect(actor, target, true, 1));
	}
}

TEST_F(AIIntegrationNativeRangedCensusBattle, homm3NativeRangedCensusRejectsDeadFriendlyCombatEvent)
{
	prepareCensus(true);
	ASSERT_FALSE(HasFatalFailure());
	int64_t fatal = shooters[1]->getAvailableHealth();
	shooters[1]->damage(fatal);
	ASSERT_FALSE(shooters[1]->alive());
	const auto ordinary = capture();
	ASSERT_TRUE(ordinary);
	ASSERT_TRUE(ordinary->rows[1]);
	EXPECT_FALSE(ordinary->rows[1]->target);
	auto event = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ON_COMBAT_EVENT,
		BonusSource::OTHER, 1, BonusSourceID());
	shooters[1]->addNewBonus(event);
	EXPECT_FALSE(capture());
}

TEST_F(AIIntegrationNativeRangedCensusBattle, homm3NativeDefendBonusesRemainOutsideOrdinaryFacts)
{
	prepareCensus();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(capture());
	endRound();
	ASSERT_FALSE(HasFatalFailure());
	bool sawDefending = false, sawPercentageDefense = false;
	for(const auto & stack : battle()->stacks)
	{
		sawDefending |= stack->hasBonusOfType(BonusType::UNIT_DEFENDING);
		const auto records = stack->getAllBonuses(Selector::type()(BonusType::PRIMARY_SKILL));
		for(const auto & bonus : *records)
			if(bonus->source == BonusSource::OTHER
				&& bonus->duration == BonusDuration::STACK_GETS_TURN
				&& bonus->subtype == BonusSubtypeID(PrimarySkill::DEFENSE)
				&& bonus->valType == BonusValueType::PERCENT_TO_ALL && bonus->val == 20)
				sawPercentageDefense = true;
	}
	ASSERT_TRUE(sawDefending);
	ASSERT_TRUE(sawPercentageDefense);
	EXPECT_FALSE(capture());
}
