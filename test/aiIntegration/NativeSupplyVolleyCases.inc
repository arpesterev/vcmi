/* HoMM3 host-engine supply regressions; native execution pending. */

TEST_F(AIIntegrationNativeBattle, homm3SupplyVolleysLiveCartRanksAndExecutes)
{
	CStack * shooter = nullptr;
	std::vector<CStack *> targets;
	prepareCombatValueShots(CreatureID(19), shooter, targets); // Grand Elf, two shots.
	ASSERT_FALSE(HasFatalFailure());
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(19)), BonusCustomSubtype::creatureDamageBoth));
	ASSERT_EQ(shooter->getTotalAttacks(true), 2);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->battleGetOwner(shooter);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	chooseCombatValueAction(shooter, callback);
	ASSERT_FALSE(HasFatalFailure());
	const auto initialTarget = callback->action->target.front().unitValue;
	auto * wounded = targets[0]->unitId() == initialTarget ? targets[1] : targets[0];
	const auto estimate = view->battleEstimateDamage(BattleAttackInfo(shooter, wounded, 0, true));
	const int64_t hit = estimate.damage.min + (estimate.damage.max - estimate.damage.min) / 2;
	const int64_t hp = wounded->getMaxHealth();
	ASSERT_GT(hit % hp, (2 * hit) % hp)
		<< "Each hit must cross the old wound, while the complete volley does not";
	int64_t wound = hp - hit % hp;
	wounded->damage(wound);
	const auto before = wounded->getAvailableHealth();
	shooter->shots.use(shooter->shots.available());
	ASSERT_EQ(shooter->shots.available(), 0);
	for(const auto * target : targets)
	{
		ASSERT_FALSE(view->battleIsUnitBlocked(target));
		ASSERT_TRUE(view->battleCanShoot(target));
		ASSERT_TRUE(view->battleCanShoot(target, shooter->getPosition()));
	}
	BattleHex supplyHex;
	for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
	{
		const BattleHex hex(i);
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex)
			&& std::none_of(targets.begin(), targets.end(), [&](const CStack * target)
			{ return target->getSurroundingHexes().contains(hex); }))
		{ supplyHex = hex; break; }
	}
	ASSERT_TRUE(supplyHex.isValid());
	auto * supplyCart = addStack(shooter->unitSide(), CreatureID::AMMO_CART, supplyHex, 1);
	ASSERT_NE(supplyCart, nullptr);
	ASSERT_FALSE(shooter->shots.isLimited());
	ASSERT_TRUE(shooter->shots.canUse(1));
	for(const auto * target : targets)
	{
		ASSERT_FALSE(view->battleIsUnitBlocked(target));
		ASSERT_TRUE(view->battleCanShoot(target));
		ASSERT_TRUE(view->battleCanShoot(target, shooter->getPosition()));
	}

	chooseCombatValueAction(shooter, callback);
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(callback->action->target.front().unitValue, initialTarget)
		<< "Unlimited supply must retain the two-volley target ranking";
	EXPECT_EQ(wounded->getAvailableHealth(), before);
	int shots = 0;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		if(attack.stackAttacking == static_cast<ui32>(shooter->unitId()) && attack.shot() && !attack.counter())
			++shots;
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(shots, 2);
	EXPECT_EQ(shooter->shots.available(), 0);
	EXPECT_TRUE(shooter->shots.canUse(1));
}

TEST_F(AIIntegrationNativeBattle, homm3SupplyVolleysArtifactRanksAndExecutes)
{
	CStack * shooter = nullptr;
	std::vector<CStack *> targets;
	prepareCombatValueShots(CreatureID(19), shooter, targets); // Grand Elf, two shots.
	ASSERT_FALSE(HasFatalFailure());
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(19)), BonusCustomSubtype::creatureDamageBoth));
	ASSERT_EQ(shooter->getTotalAttacks(true), 2);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->battleGetOwner(shooter);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	chooseCombatValueAction(shooter, callback);
	ASSERT_FALSE(HasFatalFailure());
	const auto initialTarget = callback->action->target.front().unitValue;
	auto * wounded = targets[0]->unitId() == initialTarget ? targets[1] : targets[0];
	const auto estimate = view->battleEstimateDamage(BattleAttackInfo(shooter, wounded, 0, true));
	const int64_t hit = estimate.damage.min + (estimate.damage.max - estimate.damage.min) / 2;
	const int64_t hp = wounded->getMaxHealth();
	ASSERT_GT(hit % hp, (2 * hit) % hp)
		<< "Each hit must cross the old wound, while the complete volley does not";
	int64_t wound = hp - hit % hp;
	wounded->damage(wound);
	const auto before = wounded->getAvailableHealth();
	shooter->shots.use(shooter->shots.available());
	ASSERT_EQ(shooter->shots.available(), 0);
	giveArtifact(attackerSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
	ASSERT_FALSE(shooter->shots.isLimited());
	ASSERT_TRUE(shooter->shots.canUse(1));

	chooseCombatValueAction(shooter, callback);
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(callback->action->target.front().unitValue, initialTarget)
		<< "Unlimited supply must retain the two-volley target ranking";
	EXPECT_EQ(wounded->getAvailableHealth(), before);
	int shots = 0;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		if(attack.stackAttacking == static_cast<ui32>(shooter->unitId()) && attack.shot() && !attack.counter())
			++shots;
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(shots, 2);
	EXPECT_EQ(shooter->shots.available(), 0);
	EXPECT_TRUE(shooter->shots.canUse(1));
}

TEST_F(AIIntegrationNativeBattle, homm3SupplyVolleysFiniteLastShotRanksAndExecutes)
{
	CStack * shooter = nullptr;
	std::vector<CStack *> targets;
	prepareCombatValueShots(CreatureID(19), shooter, targets); // Grand Elf, two shots.
	ASSERT_FALSE(HasFatalFailure());
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(19)), BonusCustomSubtype::creatureDamageBoth));
	ASSERT_EQ(shooter->getTotalAttacks(true), 2);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->battleGetOwner(shooter);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	chooseCombatValueAction(shooter, callback);
	ASSERT_FALSE(HasFatalFailure());
	const auto initialTarget = callback->action->target.front().unitValue;
	auto * wounded = targets[0]->unitId() == initialTarget ? targets[1] : targets[0];
	const auto estimate = view->battleEstimateDamage(BattleAttackInfo(shooter, wounded, 0, true));
	const int64_t hit = estimate.damage.min + (estimate.damage.max - estimate.damage.min) / 2;
	const int64_t hp = wounded->getMaxHealth();
	ASSERT_GT(hit % hp, (2 * hit) % hp)
		<< "Each hit must cross the old wound, while the complete volley does not";
	int64_t wound = hp - hit % hp;
	wounded->damage(wound);
	const auto before = wounded->getAvailableHealth();
	shooter->shots.use(shooter->shots.available() - 1);
	ASSERT_EQ(shooter->shots.available(), 1);
	ASSERT_TRUE(shooter->shots.isLimited());
	ASSERT_TRUE(shooter->shots.canUse(1));

	chooseCombatValueAction(shooter, callback);
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(callback->action->target.front().unitValue, wounded->unitId())
		<< "A single available shot must prefer the wounded stack";
	EXPECT_EQ(wounded->getAvailableHealth(), before);
	int shots = 0;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		if(attack.stackAttacking == static_cast<ui32>(shooter->unitId()) && attack.shot() && !attack.counter())
			++shots;
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(shots, 1);
	EXPECT_EQ(shooter->shots.available(), 0);
	EXPECT_FALSE(shooter->shots.canUse(1));
}

TEST_F(AIIntegrationNativeBattle, homm3SupplyVolleysDeadFirstCartLastShotRanksAndExecutes)
{
	CStack * shooter = nullptr;
	std::vector<CStack *> targets;
	prepareCombatValueShots(CreatureID(19), shooter, targets); // Grand Elf, two shots.
	ASSERT_FALSE(HasFatalFailure());
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(19)), BonusCustomSubtype::creatureDamageBoth));
	ASSERT_EQ(shooter->getTotalAttacks(true), 2);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->battleGetOwner(shooter);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	chooseCombatValueAction(shooter, callback);
	ASSERT_FALSE(HasFatalFailure());
	const auto initialTarget = callback->action->target.front().unitValue;
	auto * wounded = targets[0]->unitId() == initialTarget ? targets[1] : targets[0];
	const auto estimate = view->battleEstimateDamage(BattleAttackInfo(shooter, wounded, 0, true));
	const int64_t hit = estimate.damage.min + (estimate.damage.max - estimate.damage.min) / 2;
	const int64_t hp = wounded->getMaxHealth();
	ASSERT_GT(hit % hp, (2 * hit) % hp)
		<< "Each hit must cross the old wound, while the complete volley does not";
	int64_t wound = hp - hit % hp;
	wounded->damage(wound);
	const auto before = wounded->getAvailableHealth();
	shooter->shots.use(shooter->shots.available() - 1);
	ASSERT_EQ(shooter->shots.available(), 1);
	for(const auto * target : targets)
	{
		ASSERT_FALSE(view->battleIsUnitBlocked(target));
		ASSERT_TRUE(view->battleCanShoot(target));
		ASSERT_TRUE(view->battleCanShoot(target, shooter->getPosition()));
	}
	BattleHex supplyHex;
	for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
	{
		const BattleHex hex(i);
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex)
			&& std::none_of(targets.begin(), targets.end(), [&](const CStack * target)
			{ return target->getSurroundingHexes().contains(hex); }))
		{ supplyHex = hex; break; }
	}
	ASSERT_TRUE(supplyHex.isValid());
	auto * supplyCart = addStack(shooter->unitSide(), CreatureID::AMMO_CART, supplyHex, 1);
	ASSERT_NE(supplyCart, nullptr);
	int64_t cartHealth = supplyCart->getAvailableHealth();
	supplyCart->damage(cartHealth);
	ASSERT_FALSE(supplyCart->alive());
	BattleHex secondSupplyHex;
	for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
	{
		const BattleHex hex(i);
		if(hex.isAvailable() && hex != supplyHex && !battle()->battleGetUnitByPos(hex)
			&& std::none_of(targets.begin(), targets.end(), [&](const CStack * target)
			{ return target->getSurroundingHexes().contains(hex); }))
		{ secondSupplyHex = hex; break; }
	}
	ASSERT_TRUE(secondSupplyHex.isValid());
	ASSERT_NE(addStack(shooter->unitSide(), CreatureID::AMMO_CART, secondSupplyHex, 1), nullptr);
	giveArtifact(attackerSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
	ASSERT_TRUE(shooter->shots.isLimited());
	ASSERT_TRUE(shooter->shots.canUse(1));
	for(const auto * target : targets)
	{
		ASSERT_FALSE(view->battleIsUnitBlocked(target));
		ASSERT_TRUE(view->battleCanShoot(target));
		ASSERT_TRUE(view->battleCanShoot(target, shooter->getPosition()));
	}

	chooseCombatValueAction(shooter, callback);
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(callback->action->target.front().unitValue, wounded->unitId())
		<< "A single available shot must prefer the wounded stack";
	EXPECT_EQ(wounded->getAvailableHealth(), before);
	int shots = 0;
	recorder.onAttackBeforeApply = [&](const BattleAttack & attack)
	{
		if(attack.stackAttacking == static_cast<ui32>(shooter->unitId()) && attack.shot() && !attack.counter())
			++shots;
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(shots, 1);
	EXPECT_EQ(shooter->shots.available(), 0);
	EXPECT_FALSE(shooter->shots.canUse(1));
}

TEST_F(AIIntegrationNativeBattle, homm3SupplyVolleysLiveCartSplashHitsTwice)
{
	prepareBattle(CreatureID(3), 12, CreatureID(6), 60); // Marksmen vs Swordsmen
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * marksmen = nullptr;
	CStack * swordsmen = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			marksmen = unit.get();
		else
			swordsmen = unit.get();
	}
	ASSERT_NE(marksmen, nullptr);
	ASSERT_NE(swordsmen, nullptr);
	ASSERT_EQ(marksmen->getTotalAttacks(true), 2);
	ASSERT_GE(marksmen->shots.available(), 2);
	const BattleHex primaryPosition = swordsmen->getPosition();
	BattleHex adjacent;
	for(const auto & hex : primaryPosition.getNeighbouringTiles())
	{
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
		{
			adjacent = hex;
			break;
		}
	}
	ASSERT_TRUE(adjacent.isValid());
	CStack * neighbour = addStack(BattleSide::DEFENDER, CreatureID(6), adjacent, 60);
	ASSERT_NE(neighbour, nullptr);
	marksmen->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SHOOTS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));

	marksmen->shots.use(marksmen->shots.available());
	ASSERT_EQ(marksmen->shots.available(), 0);
	BattleHex supplyHex;
	for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
	{
		const BattleHex hex(i);
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex))
		{ supplyHex = hex; break; }
	}
	ASSERT_TRUE(supplyHex.isValid());
	auto * supplyCart = addStack(marksmen->unitSide(), CreatureID::AMMO_CART, supplyHex, 1);
	ASSERT_NE(supplyCart, nullptr);
	ASSERT_FALSE(marksmen->shots.isLimited());
	ASSERT_TRUE(marksmen->shots.canUse(1));
	battle()->activeStack = marksmen->unitId();

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(marksmen, swordsmen->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(marksmen, neighbour->getPosition()));
	const auto action = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *marksmen);
	ASSERT_EQ(action.actionType, EActionType::SHOOT);
	ASSERT_FALSE(action.target.empty());
	const CStack * primary = action.target.front().unitValue == swordsmen->unitId() ? swordsmen : neighbour;
	const CStack * secondary = primary == swordsmen ? neighbour : swordsmen;
	ASSERT_EQ(action.target.front().unitValue, primary->unitId());
	const auto nativeAffected = view->getAttackedBattleUnits(
		marksmen, primary, primary->getPosition(), true);
	ASSERT_NE(std::find(nativeAffected.begin(), nativeAffected.end(), secondary), nativeAffected.end());
	const auto primaryHealth = primary->getAvailableHealth();
	const auto secondaryHealth = secondary->getAvailableHealth();
	BattleAttackInfo opening(marksmen, primary, 0, true);
	const auto estimate = view->battleEstimateDamage(opening);
	ASSERT_LT(estimate.damage.max * 2, primaryHealth)
		<< "The primary must survive long enough for both engine volleys";
	BattleAttackInfo splash(marksmen, secondary, 0, true);
	ASSERT_LT(view->battleEstimateDamage(splash).damage.max * 2, secondaryHealth);

	int volleys = 0;
	int splashVolleys = 0;
	const auto shooterID = static_cast<ui32>(marksmen->unitId());
	recorder.onAttackBeforeApply = [&](const BattleAttack & packet)
	{
		if(packet.stackAttacking != shooterID || !packet.shot() || packet.counter())
			return;
		++volleys;
		for(const auto & victim : packet.bsa)
			if(victim.stackAttacked == secondary->unitId())
			{
				++splashVolleys;
				break;
			}
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "Native VCMI server rejected the independently selected HoMM3 splash shot";
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(volleys, 2);
	EXPECT_EQ(splashVolleys, 2) << "Both native shots must damage the same surviving secondary stack";
	EXPECT_LT(primary->getAvailableHealth(), primaryHealth);
	EXPECT_LT(secondary->getAvailableHealth(), secondaryHealth);
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

TEST_F(AIIntegrationNativeBattle, homm3SupplyVolleysArtifactLethalPrimaryStopsSecondShot)
{
	prepareBattle(CreatureID(3), 500, CreatureID(12), 1); // Marksmen vs Angel
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * marksmen = nullptr;
	CStack * angel = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			marksmen = unit.get();
		else
			angel = unit.get();
	}
	ASSERT_NE(marksmen, nullptr);
	ASSERT_NE(angel, nullptr);
	ASSERT_EQ(marksmen->getTotalAttacks(true), 2);
	const BattleHex targetHex = angel->getPosition();
	BattleHex neighbour;
	for(const auto & hex : targetHex.getNeighbouringTiles())
	{
		if(hex.isAvailable() && battle()->battleGetUnitByPos(hex) == nullptr)
		{
			neighbour = hex;
			break;
		}
	}
	ASSERT_TRUE(neighbour.isValid());
	CStack * bystander = addStack(BattleSide::DEFENDER, CreatureID(0), neighbour, 2);
	ASSERT_NE(bystander, nullptr);
	// Keep one distant enemy alive after the lethal splash so the battle
	// remains active while the server finishes delivering its first action.
	// A terminal shot can destroy the test callback during its own call.
	BattleHex reserveHex;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(candidate.isAvailable() && battle()->battleGetUnitByPos(candidate) == nullptr
			&& BattleHex::getDistance(candidate, targetHex) >= 5
			&& BattleHex::getDistance(candidate, marksmen->getPosition()) >= 3)
		{
			reserveHex = candidate;
			break;
		}
	}
	ASSERT_TRUE(reserveHex.isValid());
	CStack * reserve = addStack(BattleSide::DEFENDER, CreatureID(0), reserveHex, 1);
	ASSERT_NE(reserve, nullptr);
	marksmen->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SHOOTS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));

	marksmen->shots.use(marksmen->shots.available());
	ASSERT_EQ(marksmen->shots.available(), 0);
	giveArtifact(attackerSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
	ASSERT_FALSE(marksmen->shots.isLimited());
	ASSERT_TRUE(marksmen->shots.canUse(1));
	battle()->activeStack = marksmen->unitId();

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(marksmen, angel->getPosition()));
	const auto affected = view->getAttackedBattleUnits(
		marksmen, angel, angel->getPosition(), true);
	ASSERT_NE(std::find(affected.begin(), affected.end(), bystander), affected.end())
		<< "The first shot must have a real second enemy in its native area";
	BattleAttackInfo firstShot(marksmen, angel, 0, true);
	ASSERT_GE(view->battleEstimateDamage(firstShot).damage.min, angel->getAvailableHealth());
	const auto bystanderHealth = bystander->getAvailableHealth();
	const auto action = AIIntegration::HoMM3TacticsBattleAI::chooseAction(*view, *marksmen);
	ASSERT_EQ(action.actionType, EActionType::SHOOT);
	ASSERT_FALSE(action.target.empty());
	ASSERT_EQ(action.target.front().unitValue, angel->unitId())
		<< "The valuable guaranteed-kill Angel must be the chosen first target";

	int volleys = 0;
	int splashVolleys = 0;
	const auto shooterID = static_cast<ui32>(marksmen->unitId());
	recorder.onAttackBeforeApply = [&](const BattleAttack & packet)
	{
		if(packet.stackAttacking != shooterID || !packet.shot() || packet.counter())
			return;
		++volleys;
		for(const auto & victim : packet.bsa)
			if(victim.stackAttacked == bystander->unitId())
			{
				++splashVolleys;
				break;
			}
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "VCMI rejected the independent HoMM3 lethal area shot";
	recorder.onAttackBeforeApply = {};
	EXPECT_EQ(volleys, 1) << "The second volley cannot target a killed Angel";
	EXPECT_EQ(splashVolleys, 1);
	EXPECT_LT(bystander->getAvailableHealth(), bystanderHealth);
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_FALSE(recorder.battleResults.empty());
}

TEST_F(AIIntegrationNativeBattle, homm3SupplyVolleysLiveCartFirstStrikePrecedesBothShots)
{
	prepareBattle(CreatureID(3), 20, CreatureID(2), 35);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * shooter = nullptr;
	CStack * defender = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			shooter = unit.get();
		else
			defender = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(defender, nullptr);
	shooter->shots.use(shooter->shots.available());
	ASSERT_EQ(shooter->shots.available(), 0);
	BattleHex supplyHex;
	for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
	{
		const BattleHex hex(i);
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex))
		{ supplyHex = hex; break; }
	}
	ASSERT_TRUE(supplyHex.isValid());
	auto * supplyCart = addStack(shooter->unitSide(), CreatureID::AMMO_CART, supplyHex, 1);
	ASSERT_NE(supplyCart, nullptr);
	ASSERT_FALSE(shooter->shots.isLimited());
	ASSERT_TRUE(shooter->shots.canUse(1));

	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::RANGED_RETALIATION,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2))));
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::FIRST_STRIKE,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(2))));
	ASSERT_TRUE(defender->hasBonusOfType(BonusType::FIRST_STRIKE));
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto view = callback->getBattle(battleID);
	ASSERT_TRUE(view->battleCanShoot(shooter, defender->getPosition()));
	ASSERT_TRUE(view->battleCanShoot(defender, shooter->getPosition()));
	ASSERT_TRUE(defender->ableToRetaliate());
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);

	BattleAttackInfo opening(shooter, defender, 0, true);
	const auto oldShot = view->battleEstimateDamage(opening);
	BattleAttackInfo counter(defender, shooter, 0, true);
	const auto firstCounter = view->battleEstimateDamage(counter);
	ASSERT_GT(firstCounter.damage.min, 0);
	auto reducedShooter = shooter->acquireState();
	int64_t expectedCounter = firstCounter.damage.min
		+ (firstCounter.damage.max - firstCounter.damage.min) / 2;
	expectedCounter = std::clamp<int64_t>(
		expectedCounter, 0, reducedShooter->getAvailableHealth());
	reducedShooter->damage(expectedCounter);
	ASSERT_TRUE(reducedShooter->alive());
	ASSERT_LT(reducedShooter->getCount(), shooter->getCount());
	BattleAttackInfo reducedOpening(reducedShooter.get(), defender, 0, true);
	reducedOpening.attackerPos = shooter->getPosition();
	EXPECT_LT(view->battleEstimateDamage(reducedOpening).damage.max, oldShot.damage.max)
		<< "FIRST_STRIKE makes the surviving attacking shooters weaker";
	const int64_t originalShooterHealth = shooter->getAvailableHealth();
	const int64_t originalDefenderHealth = defender->getAvailableHealth();

	auto policy = AIFactory::createBattleAI("HoMM3OriginalTactics");
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
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::SHOOT);
	ASSERT_FALSE(callback->action->target.empty());
	EXPECT_EQ(callback->action->target.front().unitValue, defender->unitId());
	EXPECT_EQ(shooter->getAvailableHealth(), originalShooterHealth);
	EXPECT_EQ(defender->getAvailableHealth(), originalDefenderHealth)
		<< "A tactical forecast must never mutate live stacks";

	std::vector<int> orderedShotVictims;
	recorder.onAttackBeforeApply = [&](const BattleAttack & hit)
	{
		if(!hit.shot())
			return;
		for(const auto & victim : hit.bsa)
			if(victim.stackAttacked == shooter->unitId() || victim.stackAttacked == defender->unitId())
				orderedShotVictims.push_back(victim.stackAttacked);
	};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action))
		<< "VCMI server rejected the independent HoMM3 ranged command";
	ASSERT_EQ(orderedShotVictims.size(), 3u)
		<< "The authoritative engine must execute both the countershot and original shot";
	EXPECT_EQ(orderedShotVictims.front(), shooter->unitId())
		<< "The defender's FIRST_STRIKE must land before the initial shot";
	EXPECT_EQ(orderedShotVictims[1], defender->unitId());
	EXPECT_EQ(orderedShotVictims[2], defender->unitId());
	recorder.onAttackBeforeApply = {};
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}

TEST_F(AIIntegrationNativeBattle, homm3SupplyVolleysEnemyLiveCartThreatExecutesNativeBattle)
{
	prepareBattle(CreatureID(7), 15, CreatureID(3), 50); // Crusaders vs Marksmen
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	const CStack * crusader = nullptr;
	CStack * marksmen = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(7))
			crusader = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(3))
			marksmen = unit.get();
	}
	ASSERT_NE(crusader, nullptr);
	ASSERT_NE(marksmen, nullptr);
	ASSERT_EQ(marksmen->getTotalAttacks(true), 2);
	ASSERT_GE(marksmen->shots.available(), 2);
	marksmen->shots.use(marksmen->shots.available());
	ASSERT_EQ(marksmen->shots.available(), 0);
	BattleHex supplyHex;
	for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
	{
		const BattleHex hex(i);
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex))
		{ supplyHex = hex; break; }
	}
	ASSERT_TRUE(supplyHex.isValid());
	auto * supplyCart = addStack(marksmen->unitSide(), CreatureID::AMMO_CART, supplyHex, 1);
	ASSERT_NE(supplyCart, nullptr);
	ASSERT_FALSE(marksmen->shots.isLimited());
	ASSERT_TRUE(marksmen->shots.canUse(1));

	ASSERT_EQ(battle()->battleActiveUnit(), crusader);
	BattleHex adjacent;
	for(const auto & hex : crusader->getSurroundingHexes())
	{
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex))
		{
			adjacent = hex;
			break;
		}
	}
	ASSERT_TRUE(adjacent.isValid());
	const CStack * peasant = addStack(BattleSide::DEFENDER, CreatureID(0), adjacent, 100);
	ASSERT_NE(peasant, nullptr);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(view->battleCanShoot(marksmen, crusader->getPosition()));
	const auto crusaderHealth = crusader->getAvailableHealth();
	const auto marksmenHealth = marksmen->getAvailableHealth();
	auto policy = AIFactory::createBattleAI("HoMM3OriginalTactics");
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
	policy->activeStack(battleID, crusader);
	ASSERT_TRUE(callback->action.has_value());
	const auto action = *callback->action;
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_GE(action.target.size(), 2u);
	EXPECT_EQ(view->battleGetUnitByPos(action.target[1].hexValue), peasant);
	EXPECT_EQ(crusader->getAvailableHealth(), crusaderHealth);
	EXPECT_EQ(marksmen->getAvailableHealth(), marksmenHealth)
		<< "Ranged threat projections must not mutate either live stack";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action))
		<< "Native VCMI server rejected the HoMM3 melee decision";
	policy.reset();
	runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_GT(recorder.shots, 0);
	EXPECT_GT(recorder.damage, 0);
	EXPECT_FALSE(recorder.battleResults.empty());
}
