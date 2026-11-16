/* Native ammunition regressions; included after AIIntegrationNativeBattle fixture. */
TEST_F(AIIntegrationNativeBattle, fheroes2SpentActiveArcherMeleesInsteadOfRetreating)
{
	prepareBattle(CreatureID(19), 25, CreatureID(16), 10);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * shooter = nullptr;
	CStack * remote = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(19))
			shooter = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(16))
			remote = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(remote, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	const BattleHex blockerHex(shooter->getPosition().toInt() + 1);
	ASSERT_TRUE(blockerHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(blockerHex), nullptr);
	const auto * blocker = addStack(BattleSide::DEFENDER, CreatureID(16), blockerHex, 10);
	ASSERT_NE(blocker, nullptr);
	ASSERT_FALSE(battle()->battleCanShoot(shooter));
	ASSERT_GT(static_cast<int>(shooter->getMovementRange()),
		static_cast<int>(blocker->getMovementRange()) + 2);
	CStack * depleted = shooter;

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
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
	std::vector<std::tuple<const CStack *, BattleHex, int64_t, int>> before;
	for(const auto & unit : battle()->stacks)
		before.emplace_back(unit.get(), unit->getPosition(), unit->getAvailableHealth(), unit->shots.available());
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK)
		<< "Armed control must retreat: " << callback->action->toString();

	for(const auto & [unit, position, health, ammunition] : before)
	{
		ASSERT_EQ(unit->getPosition(), position);
		ASSERT_EQ(unit->getAvailableHealth(), health);
		ASSERT_EQ(unit->shots.available(), ammunition);
	}
	const auto armedAction = callback->action->toString();
	callback->action.reset();
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->toString(), armedAction)
		<< "Repeating an unchanged armed decision must not alter the policy result";

	for(const auto & [unit, position, health, ammunition] : before)
	{
		ASSERT_EQ(unit->getPosition(), position);
		ASSERT_EQ(unit->getAvailableHealth(), health);
		ASSERT_EQ(unit->shots.available(), ammunition);
	}

	// Native ammunition depletion; no cart or hypothetical unit is involved.
	ASSERT_GT(depleted->shots.available(), 0);
	depleted->shots.use(depleted->shots.available());
	ASSERT_EQ(depleted->shots.available(), 0);
	ASSERT_FALSE(depleted->shots.canUse(1));
	ASSERT_TRUE(depleted->isShooter());
	ASSERT_FALSE(battle()->battleCanShoot(depleted));
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	callback->action.reset();
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "Spent ammunition must switch retreat to melee: " << callback->action->toString();
	const int damageBefore = recorder.damage;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	EXPECT_GT(recorder.damage, damageBefore);
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
}

TEST_F(AIIntegrationNativeBattle, fheroes2SpentRemoteArcherPreventsUnsafeRetreat)
{
	prepareBattle(CreatureID(19), 25, CreatureID(2), 10);
	ASSERT_FALSE(HasFatalFailure());
	battle()->obstacles.clear();
	CStack * shooter = nullptr;
	CStack * remote = nullptr;
	for(const auto & unit : battle()->stacks)
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(19))
			shooter = unit.get();
		if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(2))
			remote = unit.get();
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(remote, nullptr);
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	const BattleHex blockerHex(shooter->getPosition().toInt() + 1);
	ASSERT_TRUE(blockerHex.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(blockerHex), nullptr);
	const auto * blocker = addStack(BattleSide::DEFENDER, CreatureID(16), blockerHex, 10);
	ASSERT_NE(blocker, nullptr);
	ASSERT_FALSE(battle()->battleCanShoot(shooter));
	ASSERT_GT(static_cast<int>(shooter->getMovementRange()),
		static_cast<int>(blocker->getMovementRange()) + 2);
	ASSERT_GT(BattleHex::getDistance(shooter->getPosition(), remote->getPosition()), 1);
	remote->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACKS_SPEED, BonusSource::OTHER, 25, BonusSourceID()));
	ASSERT_GT(remote->getMovementRange(), shooter->getMovementRange());
	ASSERT_TRUE(remote->isShooter());
	ASSERT_TRUE(remote->shots.canUse(1));
	ASSERT_TRUE(battle()->battleCanShoot(remote));
	CStack * depleted = remote;

	const auto battleID = battle()->getBattleID();
	const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<DecisionCallback>(player);
	callback->onBattleStarted(battle());
	auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
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
	std::vector<std::tuple<const CStack *, BattleHex, int64_t, int>> before;
	for(const auto & unit : battle()->stacks)
		before.emplace_back(unit.get(), unit->getPosition(), unit->getAvailableHealth(), unit->shots.available());
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK)
		<< "Armed control must retreat: " << callback->action->toString();

	for(const auto & [unit, position, health, ammunition] : before)
	{
		ASSERT_EQ(unit->getPosition(), position);
		ASSERT_EQ(unit->getAvailableHealth(), health);
		ASSERT_EQ(unit->shots.available(), ammunition);
	}
	const auto armedAction = callback->action->toString();
	callback->action.reset();
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->toString(), armedAction)
		<< "Repeating an unchanged armed decision must not alter the policy result";

	for(const auto & [unit, position, health, ammunition] : before)
	{
		ASSERT_EQ(unit->getPosition(), position);
		ASSERT_EQ(unit->getAvailableHealth(), health);
		ASSERT_EQ(unit->shots.available(), ammunition);
	}

	// Native ammunition depletion; no cart or hypothetical unit is involved.
	ASSERT_GT(depleted->shots.available(), 0);
	depleted->shots.use(depleted->shots.available());
	ASSERT_EQ(depleted->shots.available(), 0);
	ASSERT_FALSE(depleted->shots.canUse(1));
	ASSERT_TRUE(depleted->isShooter());
	ASSERT_FALSE(battle()->battleCanShoot(depleted));
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	callback->action.reset();
	policy->activeStack(battleID, shooter);
	ASSERT_TRUE(callback->action.has_value());
	ASSERT_EQ(callback->action->actionType, EActionType::WALK_AND_ATTACK)
		<< "Spent ammunition must switch retreat to melee: " << callback->action->toString();
	const int damageBefore = recorder.damage;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *callback->action));
	EXPECT_GT(recorder.damage, damageBefore);
	policy.reset();
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
}
