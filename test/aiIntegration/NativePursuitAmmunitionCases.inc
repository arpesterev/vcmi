// Pinned meleeUnitOffense isArchers() pursuit priority on native live states.
TEST_F(AIIntegrationNativeBattle, fheroes2SpentDistantArcherLosesUnavoidablePursuitPriority)
{
	prepareBattle(CreatureID(0), 1000, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * infantry = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			infantry = unit.get();
	ASSERT_NE(infantry, nullptr);
	infantry->setPosition(BattleHex(1, 5));
	const BattleHex origin = infantry->getPosition();
	const BattleHex meleeHex(5, 0);
	const BattleHex archerHex(5, 10);
	auto * melee = addStack(BattleSide::DEFENDER, CreatureID(0), meleeHex, 1);
	auto * archer = addStack(BattleSide::DEFENDER, CreatureID(2), archerHex, 65);
	ASSERT_NE(melee, nullptr);
	ASSERT_NE(archer, nullptr);
	melee->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACKS_SPEED, BonusSource::OTHER, -1, BonusSourceID()));
	ASSERT_LT(melee->getMovementRange(), infantry->getMovementRange());
	ASSERT_GE(archer->getMovementRange(), infantry->getMovementRange());
	ASSERT_GT(BattleHex::getDistance(origin, meleeHex), infantry->getMovementRange() + 1);
	ASSERT_GT(BattleHex::getDistance(origin, archerHex), infantry->getMovementRange() + 1);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	const auto decide = [&]() -> std::optional<BattleAction>
	{
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
		if(!policy)
			return std::nullopt;
		policy->playerID = player;
		auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
		AutocombatPreferences prefs;
		prefs.enableSpellsUsage = false;
		policy->initBattleInterface(environment, callback, prefs);
		policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
			battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
			battle()->getSideHero(BattleSide::ATTACKER), battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
		policy->activeStack(battleID, infantry);
		return callback->action;
	};
	ASSERT_TRUE(archer->shots.canUse(1));
	const auto armed = decide();
	ASSERT_TRUE(armed.has_value());
	ASSERT_EQ(armed->actionType, EActionType::WALK) << armed->toString();
	const BattleHex armedStep = armed->target.front().hexValue;
	EXPECT_LT(BattleHex::getDistance(armedStep, archerHex), BattleHex::getDistance(origin, archerHex));
	archer->shots.use(archer->shots.available());
	ASSERT_FALSE(archer->shots.canUse(1));
	ASSERT_TRUE(archer->hasBonusOfType(BonusType::SHOOTER));
	const auto after = decide();
	ASSERT_TRUE(after.has_value());
	ASSERT_EQ(after->actionType, EActionType::WALK) << after->toString();
	const BattleHex afterStep = after->target.front().hexValue;
	std::cerr << "PURSUIT_TEST origin=" << origin.toInt() << " armed=" << armedStep.toInt() << " after=" << afterStep.toInt() << std::endl;
	EXPECT_NE(afterStep, armedStep);
	EXPECT_LT(BattleHex::getDistance(afterStep, meleeHex), BattleHex::getDistance(armedStep, meleeHex));
	EXPECT_EQ(infantry->getPosition(), origin);
	EXPECT_EQ(archer->getPosition(), archerHex);
	const auto repeated = decide();
	ASSERT_TRUE(repeated.has_value());
	EXPECT_EQ(repeated->toString(), after->toString());
	battle()->activeStack = infantry->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *after));
	EXPECT_EQ(infantry->getPosition(), afterStep);
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2ZeroCapacityDistantArcherLosesUnavoidablePursuitPriority)
{
	prepareBattle(CreatureID(0), 1000, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * infantry = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			infantry = unit.get();
	ASSERT_NE(infantry, nullptr);
	infantry->setPosition(BattleHex(1, 5));
	const BattleHex origin = infantry->getPosition();
	const BattleHex meleeHex(5, 0);
	const BattleHex archerHex(5, 10);
	auto * melee = addStack(BattleSide::DEFENDER, CreatureID(0), meleeHex, 1);
	auto * archer = addStack(BattleSide::DEFENDER, CreatureID(2), archerHex, 65);
	ASSERT_NE(melee, nullptr);
	ASSERT_NE(archer, nullptr);
	melee->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACKS_SPEED, BonusSource::OTHER, -1, BonusSourceID()));
	ASSERT_LT(melee->getMovementRange(), infantry->getMovementRange());
	ASSERT_GE(archer->getMovementRange(), infantry->getMovementRange());
	ASSERT_GT(BattleHex::getDistance(origin, meleeHex), infantry->getMovementRange() + 1);
	ASSERT_GT(BattleHex::getDistance(origin, archerHex), infantry->getMovementRange() + 1);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	const auto decide = [&]() -> std::optional<BattleAction>
	{
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
		if(!policy)
			return std::nullopt;
		policy->playerID = player;
		auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
		AutocombatPreferences prefs;
		prefs.enableSpellsUsage = false;
		policy->initBattleInterface(environment, callback, prefs);
		policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
			battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
			battle()->getSideHero(BattleSide::ATTACKER), battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
		policy->activeStack(battleID, infantry);
		return callback->action;
	};
	ASSERT_TRUE(archer->shots.canUse(1));
	const auto armed = decide();
	ASSERT_TRUE(armed.has_value());
	ASSERT_EQ(armed->actionType, EActionType::WALK) << armed->toString();
	const BattleHex armedStep = armed->target.front().hexValue;
	EXPECT_LT(BattleHex::getDistance(armedStep, archerHex), BattleHex::getDistance(origin, archerHex));
	archer->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SHOTS, BonusSource::OTHER, -archer->shots.total(), BonusSourceID()));
	ASSERT_EQ(archer->shots.total(), 0);
	ASSERT_FALSE(archer->shots.canUse(1));
	ASSERT_TRUE(archer->hasBonusOfType(BonusType::SHOOTER));
	const auto after = decide();
	ASSERT_TRUE(after.has_value());
	ASSERT_EQ(after->actionType, EActionType::WALK) << after->toString();
	const BattleHex afterStep = after->target.front().hexValue;
	std::cerr << "PURSUIT_TEST origin=" << origin.toInt() << " armed=" << armedStep.toInt() << " after=" << afterStep.toInt() << std::endl;
	EXPECT_NE(afterStep, armedStep);
	EXPECT_LT(BattleHex::getDistance(afterStep, meleeHex), BattleHex::getDistance(armedStep, meleeHex));
	EXPECT_EQ(infantry->getPosition(), origin);
	EXPECT_EQ(archer->getPosition(), archerHex);
	const auto repeated = decide();
	ASSERT_TRUE(repeated.has_value());
	EXPECT_EQ(repeated->toString(), after->toString());
	battle()->activeStack = infantry->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *after));
	EXPECT_EQ(infantry->getPosition(), afterStep);
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2SuppliedDistantArcherRetainsUnavoidablePursuitPriority)
{
	prepareBattle(CreatureID(0), 1000, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * infantry = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			infantry = unit.get();
	ASSERT_NE(infantry, nullptr);
	infantry->setPosition(BattleHex(1, 5));
	const BattleHex origin = infantry->getPosition();
	const BattleHex meleeHex(5, 0);
	const BattleHex archerHex(5, 10);
	auto * melee = addStack(BattleSide::DEFENDER, CreatureID(0), meleeHex, 1);
	auto * archer = addStack(BattleSide::DEFENDER, CreatureID(2), archerHex, 65);
	ASSERT_NE(melee, nullptr);
	ASSERT_NE(archer, nullptr);
	melee->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACKS_SPEED, BonusSource::OTHER, -1, BonusSourceID()));
	ASSERT_LT(melee->getMovementRange(), infantry->getMovementRange());
	ASSERT_GE(archer->getMovementRange(), infantry->getMovementRange());
	ASSERT_GT(BattleHex::getDistance(origin, meleeHex), infantry->getMovementRange() + 1);
	ASSERT_GT(BattleHex::getDistance(origin, archerHex), infantry->getMovementRange() + 1);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	const auto decide = [&]() -> std::optional<BattleAction>
	{
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
		if(!policy)
			return std::nullopt;
		policy->playerID = player;
		auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
		AutocombatPreferences prefs;
		prefs.enableSpellsUsage = false;
		policy->initBattleInterface(environment, callback, prefs);
		policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
			battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
			battle()->getSideHero(BattleSide::ATTACKER), battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
		policy->activeStack(battleID, infantry);
		return callback->action;
	};
	ASSERT_TRUE(archer->shots.canUse(1));
	const auto armed = decide();
	ASSERT_TRUE(armed.has_value());
	ASSERT_EQ(armed->actionType, EActionType::WALK) << armed->toString();
	const BattleHex armedStep = armed->target.front().hexValue;
	EXPECT_LT(BattleHex::getDistance(armedStep, archerHex), BattleHex::getDistance(origin, archerHex));
	archer->shots.use(archer->shots.available());
	ASSERT_FALSE(archer->shots.canUse(1));
	ASSERT_NE(addStack(BattleSide::DEFENDER, CreatureID::AMMO_CART, BattleHex(15, 9), 1), nullptr);
	ASSERT_TRUE(archer->shots.canUse(1));
	ASSERT_TRUE(archer->hasBonusOfType(BonusType::SHOOTER));
	const auto after = decide();
	ASSERT_TRUE(after.has_value());
	ASSERT_EQ(after->actionType, EActionType::WALK) << after->toString();
	const BattleHex afterStep = after->target.front().hexValue;
	std::cerr << "PURSUIT_TEST origin=" << origin.toInt() << " armed=" << armedStep.toInt() << " after=" << afterStep.toInt() << std::endl;
	EXPECT_EQ(afterStep, armedStep);
	EXPECT_EQ(infantry->getPosition(), origin);
	EXPECT_EQ(archer->getPosition(), archerHex);
	const auto repeated = decide();
	ASSERT_TRUE(repeated.has_value());
	EXPECT_EQ(repeated->toString(), after->toString());
	battle()->activeStack = infantry->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *after));
	EXPECT_EQ(infantry->getPosition(), afterStep);
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}

TEST_F(AIIntegrationNativeBattle, fheroes2BlockedSuppliedDistantArcherRetainsUnavoidablePursuitPriority)
{
	prepareBattle(CreatureID(0), 1000, CreatureID(0), 1);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * infantry = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
			infantry = unit.get();
	ASSERT_NE(infantry, nullptr);
	infantry->setPosition(BattleHex(1, 5));
	const BattleHex origin = infantry->getPosition();
	const BattleHex meleeHex(5, 0);
	const BattleHex archerHex(5, 10);
	auto * melee = addStack(BattleSide::DEFENDER, CreatureID(0), meleeHex, 1);
	auto * archer = addStack(BattleSide::DEFENDER, CreatureID(2), archerHex, 65);
	ASSERT_NE(melee, nullptr);
	ASSERT_NE(archer, nullptr);
	melee->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACKS_SPEED, BonusSource::OTHER, -1, BonusSourceID()));
	ASSERT_LT(melee->getMovementRange(), infantry->getMovementRange());
	ASSERT_GE(archer->getMovementRange(), infantry->getMovementRange());
	ASSERT_GT(BattleHex::getDistance(origin, meleeHex), infantry->getMovementRange() + 1);
	ASSERT_GT(BattleHex::getDistance(origin, archerHex), infantry->getMovementRange() + 1);
	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	const auto decide = [&]() -> std::optional<BattleAction>
	{
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
		if(!policy)
			return std::nullopt;
		policy->playerID = player;
		auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
		AutocombatPreferences prefs;
		prefs.enableSpellsUsage = false;
		policy->initBattleInterface(environment, callback, prefs);
		policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
			battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
			battle()->getSideHero(BattleSide::ATTACKER), battle()->getSideHero(BattleSide::DEFENDER), BattleSide::ATTACKER, false);
		policy->activeStack(battleID, infantry);
		return callback->action;
	};
	ASSERT_TRUE(archer->shots.canUse(1));
	const auto armed = decide();
	ASSERT_TRUE(armed.has_value());
	ASSERT_EQ(armed->actionType, EActionType::WALK) << armed->toString();
	const BattleHex armedStep = armed->target.front().hexValue;
	EXPECT_LT(BattleHex::getDistance(armedStep, archerHex), BattleHex::getDistance(origin, archerHex));
	archer->shots.use(archer->shots.available());
	ASSERT_FALSE(archer->shots.canUse(1));
	ASSERT_NE(addStack(BattleSide::DEFENDER, CreatureID::AMMO_CART, BattleHex(15, 9), 1), nullptr);
	ASSERT_TRUE(archer->shots.canUse(1));
	ASSERT_NE(addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(archerHex.toInt() - 1), 1), nullptr);
	ASSERT_TRUE(battle()->battleIsUnitBlocked(archer));
	ASSERT_FALSE(battle()->battleCanShoot(archer));
	ASSERT_TRUE(archer->shots.canUse(1));
	ASSERT_TRUE(archer->hasBonusOfType(BonusType::SHOOTER));
	const auto after = decide();
	ASSERT_TRUE(after.has_value());
	ASSERT_EQ(after->actionType, EActionType::WALK) << after->toString();
	const BattleHex afterStep = after->target.front().hexValue;
	std::cerr << "PURSUIT_TEST origin=" << origin.toInt() << " armed=" << armedStep.toInt() << " after=" << afterStep.toInt() << std::endl;
	EXPECT_EQ(afterStep, armedStep);
	EXPECT_EQ(infantry->getPosition(), origin);
	EXPECT_EQ(archer->getPosition(), archerHex);
	const auto repeated = decide();
	ASSERT_TRUE(repeated.has_value());
	EXPECT_EQ(repeated->toString(), after->toString());
	battle()->activeStack = infantry->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *after));
	EXPECT_EQ(infantry->getPosition(), afterStep);
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
	EXPECT_GT(recorder.damage, 0);
}
